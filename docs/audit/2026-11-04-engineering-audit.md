# 工程化审计与修复记录（2026-11-04）

> 范围：启动资源、启动稳定性、运行稳定性、代码缺陷。
> 方法：4 个只读审计子智能体（启动/代码/运行时/实测）+ Lead 汇总修复；所有结论以代码与实测为准。
> 环境：2 vCPU / 7.9GB / Ubuntu 22.04 容器；5 个服务 + MySQL/Redis/etcd/RabbitMQ/FastDFS tracker（storage 未运行）。

## 一、结论

审计确认 3 个 **P0** 级问题，并已修复：

1. **etcd 保活失败导致进程自杀**：`SvcProvider` 在 KeepAlive 回调线程里调用 `registry()`，`_keepalive.reset()` 析构自身并 `join` 当前线程 → `std::system_error("Resource deadlock avoided")` → `terminate`。实测启动后 13–43s 随机崩溃（user/video 日志有实证）。
2. **分页参数下溢（客户端可触发）**：`pageindex() - 1` 在 `pageIndex=0` 时变成 `SIZE_MAX`，随后 `page * count` 溢出并越界读，可能崩溃/脏数据。
3. **MQ 启动罚时与声明丢通知**：每个 `declare/consume` 串行等 30s，MQ 异常时 video 最坏启动约 360s；健康时也可能因 `notify_all` 未持锁丢通知，白等 30s（实测 file 出现 12s→42s）。

另外修复了 4 个启动资源问题（线程数 236→82、启动 3.0s 全就绪、ES 不再阻塞 4s），以及 5 个 P1 代码缺陷。

## 二、修复清单

| # | 位置 | 改动 | 解决 |
| --- | --- | --- | --- |
| 1 | `scaffold/etcd.cc/.h` | 保活失败只置标志，**延迟线程**重注册；`_keepalive` 加锁替换；析构置停止位 | 自 join 崩溃 |
| 2 | `scaffold/etcd.cc/.h` | `wait_for_connection` 加 10s 超时；watch 断开改为延迟线程重连（不在回调内递归） | etcd 不可用时永久挂起 / 栈递归 |
| 3 | `scaffold/etcd.cc` | `resync` 复用客户端 + 全量 diff：新节点上线、消失节点下线 | 只加不删导致死节点残留、每 5s 新建 gRPC 连接 |
| 4 | `scaffold/mq.cc/.h` | 声明/订阅改为 3s 谓词等待 + 超时后全局降级；notify 持锁 | 30s×N 启动罚时 / 丢通知 |
| 5 | `scaffold/mq.cc/.h` | `publish` 入队 + `ev_async`，由事件循环线程真正发布；队列上限 10000 | brpc 多线程并发操作同一 AMQP channel 的数据竞争 |
| 6 | `scaffold/mq.cc` | 消费回调 try/catch：成功 ack，失败 `reject(requeue=true)` | 异常路径消息静默丢失 |
| 7 | 5 个 `svc_*/main.cc` | `crossplat::threadpool::initialize_with_threads(4)` | etcd-cpp-apiv3 经 cpprest 默认创建 **40** 线程/进程 |
| 8 | `svc_video/svc_rpc.cc`、`svc_user/svc_rpc.cc`、`svc_video/svc_data.cc`、`server/data/video_idlist.cc` | `pageIndex=0` 钳到 0；`page>1e5` 返回空；`count` 限 1–100；`int i`→`size_t i` | 分页下溢/越界 |
| 9 | `svc_transcode/svc_worker.cc` | 析构先 `stop()` 再 join；wait 谓词改 `stop || !empty` | 退出时永久 hang |
| 10 | `svc_transcode/svc_mq.cc` | 上传失败日志不再解引用空 optional，改打 video_id | UB/崩溃日志 |
| 11 | `svc_file/svc_data.cc` | `getFileMeta` 先 `catch(VPException){throw;}` | FILE_NOT_EXIST 被误报成 DATABASE_OP_FAILED |
| 12 | `svc_video/svc_data.cc` | 删除文件的 MQ 消息移到 `mtx.commit()` 之后 | 回滚后文件已被删 |
| 13 | `svc_user/svc_data.cc` | 登录失败日志不再打印明文密码 | 凭据泄漏 |
| 14 | `svc_video/main.cc`、`svc_server.cc`、`data/video_search.cc` | `--es_url` 默认空；空则跳过建索引并置 `_enabled=false` | ES 不在时启动 +4s、搜索路径空指针 |
| 15 | `scripts/run-dev.sh` | 异步日志 + info 默认；池 3；`bthread_concurrency=4`；stop 等退出、start 探端口、status 查端口 | 启动/停止不可靠、线程与 IO 偏高 |

> `bthread_concurrency` 的 gflags 校验要求 ≥4（设 3 会被拒），故取 4。

## 三、实测对比

| 指标 | 修复前 | 修复后 |
| --- | --- | --- |
| 启动（5 服务全部就绪） | 3.03s 脚本返回，真实 ~5.3s；transcode 未探活 | **3.2s**（每个服务端口/MQ 日志就绪后才算成功） |
| 单服务线程数 | 54–58 | **18–19**（transcode 9） |
| 5 服务总线程 | ~236 | **82** |
| 总 RSS（空闲） | ~226–265MB | ~**250MB**（持平；线程栈 VA 大幅下降） |
| 空闲日志增长 | debug 全开 | **15s 0 行**（info + 异步） |
| 启动崩溃 | 1/4 次 `Resource deadlock avoided` | 3 轮 stop/start + 60s 运行，**0 次** |
| `pageIndex=0` | 可能越界/崩溃 | 返回第一页，`errorCode=0` |
| 故障恢复 | — | kill user → `SERVICE_NOT_FOUND(28)`；重启后 ≤7s 恢复（5s resync 兜底） |
| 冒烟 | 4/4 | **4/4** |
| E2E | 18/18（10 月环境） | **阻塞**：`dev-storage:23000` 未启动，上传报 `fdfs_recv_response fail, result: 2` |

## 四、未修复（P1/P2，建议下一步）

1. **MQ 无重连/confirm/持久化**：RabbitMQ 重启后发布订阅不会自愈；建议 `onLost` 重连 + durable + publisher confirm + 死信重试。
2. **网关同步阻塞转发**：上传 handler 内阻塞 `CallMethod`（300s）+ body 多份拷贝；建议异步 RPC + 独立上传线程池/流式转发/大小限制。
3. **重试无上限**：`svc_sync.cc`、`video_count.cc` 的 `while(true)` + 无退避；建议限次 + 抖动 + 每轮重建事务。
4. **缓存击穿/N+1**：3s 列表缓存固定过期无 single-flight；详情逐视频查 Redis/DB；建议单飞重建 + TTL 抖动 + 批量查询。
5. **客户端**：启动页无超时；`QSharedMemory` 崩溃残留；建议 `TransferTimeout` + 重试 + `QLockFile`。
6. **工程化**：无 CI、无 `-Wall -Wextra`、无 sanitizer；测试仅 8 个 gtest，分页边界/鉴权/错误码路径无覆盖。

## 五、复测命令

```bash
cd /home/dev/workspace
./scripts/build.sh
./scripts/run-dev.sh stop; time ./scripts/run-dev.sh start
for p in $(pgrep -f 'build/bin/.*_server'); do \
  echo "$p threads=$(ls /proc/$p/task|wc -l) rss=$(awk '/VmRSS/{print $2}' /proc/$p/status)KB"; done
./scripts/smoke-test.sh
grep -c 'Resource deadlock avoided' logs/*.log
# 分页防御：pageIndex=0 应正常返回第一页
SID=$(curl -s -X POST http://127.0.0.1:10000/HttpService/tempLogin -H 'Content-Type: application/json' -d '{"requestId":"R1"}' | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["sessionId"])')
curl -s -X POST http://127.0.0.1:10000/HttpService/allVideoList -H 'Content-Type: application/json' -d "{\"requestId\":\"P0\",\"sessionId\":\"$SID\",\"pageIndex\":0,\"pageCount\":10}"
```

> E2E 复测前，需在**宿主机**启动 FastDFS storage：`docker start dev-storage`（确认 `dev-storage:23000` 可连）。
