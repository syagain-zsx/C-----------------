# 笺流 · 学习手册（6 周执行版）

> 适用 commit：`bf4faf5`。本手册 2026-10-24 汇总自原 `docs/learning/` 13 篇学习文档，并吸收 `docs/01–08`、`docs/notes/`、`ROADMAP` 第六节的有效内容；被吸收的旧学习文档已删除（git 历史可恢复）。
> 口径：✅ 已核对/已执行；⚠️ 未在本次环境验证；❌ 文档与代码不一致（以代码为准）；🧠 由代码推断、需实验确认。
> 画像：有 C++/Linux 基础，每周 8–10 小时；目标 6 周做到「能跑、能定位一条请求、能做小改造、能讲清取舍」。时间不足：砍【选做】，6 周拉成 8 周；没有 Windows/Qt：第 5 周只做契约与阅读，GUI 标「未验证」。
> 读法：§1 把环境跑起来 → §2 建立地图 → §3 术语与难点 → §4 按周执行 → §6 做检查点 → §5 面试前突击；§7 是速查。
>
> 目录：[0 结论](#0-先看结论6-周怎么走) · [1 环境](#1-环境与开工) · [2 地图](#2-项目地图阅读主线与-4-条链路) · [3 术语与难点](#3-术语小抄与-10-个难点) · [4 六周计划](#4-六周执行计划) · [5 面试](#5-20-个面试问题与回答骨架) · [6 检查点](#6-进度周记与检查点) · [7 附录](#7-附录)

## 0. 先看结论：6 周怎么走

一句话路线：**第 1 周把请求跑通，第 2 周把服务发现讲透，第 3 周把缓存/DB 讲出数据，第 4 周把异步链路和失败分支走通，第 5 周补客户端契约，第 6 周做全套验证与面试输出。**

| 周 | 一句话目标 | 必须留下的证据 | CP |
| --- | --- | --- | --- |
| 1 | 独立启动 5 个服务，走通登录/列表/下载，能画登录链路 | 3 份 JSON + 登录链路图 + 1 段录音 + 1 个 diff | 为 CP1 做准备 |
| 2 | 讲清 etcd 注册、watch 发现、租约失效，并改 TTL 观察 | etcd 节点记录 + TTL 对比表 + 5 分钟白板录音 | CP1 |
| 3 | 讲清 cache-aside、延迟双删、计数回写，并做一次压测对比 | 压测表 + 命中日志 + 1 页结论 | 为 CP2 做准备 |
| 4 | 走通上传→MQ→转码→HLS→审核，补一个失败分支 | 4 队列表 + 时序图 + 失败 diff + 10 分钟录音 | CP2 |
| 5 | 讲清客户端页面↔接口，完成一个大文件或崩溃相关小改造 | 接口映射表 + 客户端 diff + 复盘 | 为 CP3 做准备 |
| 6 | 全新构建 + 全套验证 + 15 分钟模拟面试 | 测试输出 + 优化报告 + 录音 + 简历一页 | CP3 |

三条执行原则：

1. **一次只追一条请求**：先看日志，再定位函数，最后画图；不要从 `svc_rpc.cc` 第一行读到尾。
2. **改动必须可回滚**：每周先建分支 `git switch -c learn/wN`；回滚用 `git diff > /tmp/wN.patch && git apply -R /tmp/wN.patch`，不要用 `git checkout -- .` 覆盖工作区已有改动。
3. **没有证据不算学会**：图、diff、录音、周记四件套至少留下三件；时间不够时砍【选做】，不砍「跑」和「讲」。

时间分配：每周 3 个 2 小时单元（读 45min + 跑 30min + 改 45min；讲 60min），另留 1 小时周记。第 1 周按 Day 1–7 拆开，合计约 9.5 小时。

**现在就开始（第 1 天，30 分钟）**：

```bash
cd /home/dev/workspace
./scripts/learn-check.sh          # 先看环境缺什么（只读）
./scripts/run-dev.sh start        # 启动 5 个服务
./scripts/smoke-test.sh           # 期望 通过 4 / 失败 0
```

## 1. 环境与开工

### 1.1 体检结论（2026-10-24 只读体检）

**代码、构建产物、静态检查可用；中间件除 ES 外均在；5 个业务服务当时未启动**——不是代码坏了，是学习环境尚未开工。

| 检查项 | 结果 | 说明 |
| --- | --- | --- |
| 仓库/提交 | ✅ | `main`，HEAD `bf4faf5`；`git status --short` 有 `examples/` 移除改动，与本手册无关 |
| 构建产物 | ✅ | `build/bin/` 有 5 个 `*_server` + `file_client` |
| 单元测试 | ✅ | `ctest --test-dir build`：8/8 通过 |
| 客户端接口一致性 | ✅ | `check-client-api.py`：服务端 rpc 38、客户端调用 38，两项 PASS |
| 文档链接 | ✅ | `check-doc-links.py`：合并前 33 个 Markdown，0 broken（合并后见 §7.4） |
| 中间件 TCP | ✅ | MySQL/Redis/etcd/RabbitMQ/FastDFS tracker 可连；**storage 需确认 23000** |
| Elasticsearch | ⚠️ | `dev-icsearch:9200` 关闭；代码降级为搜索空结果，属预期 |
| 5 个业务服务 | ⚠️ | `run-dev.sh status` 当时全部「已停止」；学习第一步就是启动 |
| 客户端 / Docker | ⚠️ | 本容器未验证 Qt/Windows GUI；无 `docker` 命令，Compose 未实机 |
| 学习自检 | ✅ | `learn-check.sh`：通过 9 / 失败 6 / 跳过 1，6 个失败全是服务未启动 |

> 面试时把「文档记录」和「我实测」分开说；表里的 ⚠️ 就是不能吹的部分。

### 1.2 开工 7 步（按顺序）

| # | 命令 | 期望 | 失败先看 |
| --- | --- | --- | --- |
| 1 | `./scripts/run-dev.sh start` | 末尾 5 行均为「运行中」 | `logs/<svc>.log` 末尾、端口占用 |
| 2 | `curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:10000/status` | `200` | gateway 是否起来、10000 是否被占 |
| 3 | `./scripts/smoke-test.sh` | `通过 4 / 失败 0` | 网关日志、user/video 是否注册到 etcd |
| 4 | 按 §7.1 取 `SID`，再 `python3 scripts/e2e-test.py --session "$SID" --admin` | 18/18（需 storage） | storage 23000、transcode 日志、管理员身份 |
| 5 | `ss -ltnp` | 10000–10003 在监听（transcode 不监听） | `run-dev.sh stop` 后重试 |
| 6 | 宿主机 `docker start dev-storage`（若 23000 不通） | 23000 open | 当前容器无 docker，需在宿主机做 |
| 7 | ES 可保持关闭 | video 日志出现「搜索功能降级」，其余正常 | 不要为了 ES 阻塞主线 |

### 1.3 故障排查表

| 现象 | 原因 / 处理 |
| --- | --- |
| 启动报 `Fail to listen 0.0.0.0:1000x` | 端口被残留进程占用。先 `./scripts/run-dev.sh stop`；仍占用时按 `logs/<svc>.pid` 的 PID `kill <pid>` 再 `start`。 |
| 请求返回 `SERVICE_NOT_FOUND`（错误码 28） | 目标服务未注册/已下线。查 `run-dev.sh status`、etcd range 是否有该节点、`logs/<svc>.log`。 |
| 上传失败，日志 `fdfs_recv_response fail, result: 2` | FastDFS **storage** 未启动。宿主机 `docker start dev-storage`，确认 `dev-storage:23000` 可连。 |
| 日志 `Elasticsearch 连接失败...搜索降级` | ES 未启动，属预期降级；搜索返回空，其余功能正常。 |
| 客户端一直停在启动页 | `user` 服务未启动或 `tempLogin` 失败；看 `run-dev.sh status` 与网关日志。 |
| 上传成功但首页看不到（`status=1`） | 待审核；审核通过置 2 才上架；首页列表还有 3s 缓存，稍等刷新。 |
| 播放黑屏、进度 00:00 | 视频过短或分片异常。先 `curl` 取 m3u8 确认 200 且含分片，再查 `logs/transcode.log`。 |
| 客户端连不上 | 运行目录缺 `config.ini`，或 `serverIp/serverPort` 不是 `127.0.0.1:10000`。 |
| etcd range 结果 `count=0` | 5 个服务都没启动/注册失败。租约 10s，停服后最多约 10s 节点消失；先 `start` 并看服务日志。 |

### 1.4 两套学习路径

| 路径 | 适用 | 能完成 | 不能完成 |
| --- | --- | --- | --- |
| 最小路径 | 服务起不来/时间碎 | 读代码、`ctest`、`check-client-api`、`check-doc-links`、画 4 条链路、准备面试口述 | CP1–CP3 的「跑」证据、E2E、压测、真实日志观察 |
| 完整路径 | 中间件齐全 | 6 周计划全部、CP1–CP3、压测与失败分支 | 仍需 Windows 实机才能验证客户端 GUI |

## 2. 项目地图、阅读主线与 4 条链路

### 2.1 一页纸定位

- **是什么**：笺流（JianLiu）是用 C++17 写的微服务化视频点播系统，单仓库统一 CMake；服务端 5 个进程 + Qt6 客户端，中间件封装在 `scaffold/JianLiuScaffold`（11 类）。
- **技术栈**：brpc + protobuf（对外 HTTP/JSON、内部 RPC）、etcd（注册发现）、RabbitMQ（DLX + TTL 延迟队列）、Redis（会话/缓存/计数）、MySQL 8 + ODB、FastDFS（文件）、FFmpeg/libavformat（MP4→HLS）、Elasticsearch（不可用时降级）、Qt6 + libmpv。
- **五个服务**：gateway（唯一对外入口 10000）、file（10001）、user（10002）、video（10003）、transcode（只消费 MQ，不监听 HTTP 端口）。
- **读完的标志**：能画出「客户端 → 网关 → 子服务 → 中间件」四层，并说清每条失败路径的降级行为。

### 2.2 代码地图

| 层 | 目录/文件 | 职责 | 先看什么 |
| --- | --- | --- | --- |
| 客户端 | `client/JianLiuPlayer/` | Qt6 UI、HTTP 异步客户端、libmpv 播放 | `main.cpp` 单实例；`startuppage.cpp` `startTimer`；`netclient/netclient.cpp` `sendHttpRequest` |
| 网关 | `server/svc_gateway/` | 唯一 HTTP 入口：鉴权、路由、JSON↔protobuf | `svc_server.cc` Builder；`svc_rpc.cc` `tempLogin`（L17）、`allVideoList`（L1349）、`downloadVideo`（L855） |
| user | `server/svc_user/` | 会话、验证码/密码登录、用户资料、关注、管理员 | `svc_rpc.cc` `tempLogin`（L8）；`svc_data.cc` `tempLogin`（L11） |
| video | `server/svc_video/` | 视频元信息、列表、点赞/播放、弹幕、审核、搜索 | `svc_data.cc` `newVideo`（L33）、`checkVideo`（L255）、`getMainPageList`（L504）、`getDetailVideoList`（L409） |
| file | `server/svc_file/` | 上传下载、FastDFS 读写、文件元信息 | `svc_rpc.cc` `uploadVideo`（L97）、`downloadVideo`（L161）；`svc_data.cc` `newFileMeta`（L29） |
| transcode | `server/svc_transcode/` | 消费转码消息：下载、HLS、上传、更新状态 | `svc_mq.cc` `transcode`（L22）、`callback`（L110）；`svc_worker.cc` 线程池 |
| 数据层 | `server/data/` | ODB 实体、DAO、Redis 缓存、ES 检索 | `data.h` `VideoStatus`（L31）/`Video`；`video.cc` `insert` L20、`get` L36、`update` L50 |
| 协议 | `server/proto/` | 6 个 proto：对外 HTTP、内部服务、消息体 | `http.proto` L9–49（38 个 rpc）；`base.proto` 52 个 `optional`；`message.proto` 4 类 MQ 消息 |
| 基础库 | `scaffold/JianLiuScaffold/` | 11 个中间件封装 | `etcd.cc` 注册/发现；`mq.cc` 声明/发布/订阅；`hls.cc` remux；`fdfs.cc` 上传下载 |
| 部署 | `deploy/` | Dockerfile、Compose、配置模板、13 张表 SQL | `deploy/sql/data.sql`；`deploy/conf/global.conf`；Compose 未验证 |
| 脚本 | `scripts/` | 构建、启停、冒烟、E2E、压测、自检 | `build.sh`、`run-dev.sh`、`smoke-test.sh`、`e2e-test.py`、`bench.py`、`learn-check.sh` |
| 测试 | `server/tests/` | 纯逻辑 gtest | `test_json.cc` 6 个 case；`test_m3u8.cc` 2 个；合计 8 |

### 2.3 9 步阅读主线（一次只追一条请求）

1. **入口** · `client/JianLiuPlayer/main.cpp`、`startuppage.cpp`、`netclient/netclient.cpp` → 单实例锁、启动页等待登录、`tempLogin`/`sendHttpRequest` → 问：`config.ini` 的 serverUrl 如何变成第一个 HTTP 请求并驱动界面切换？
2. **网关** · `svc_gateway/source/svc_server.cc`、`svc_rpc.cc` 的 `tempLogin`/`allVideoList`/`downloadVideo` → brpc Server 注册、取 sessionId、找节点、转发、组 JSON → 问：网关怎么知道 user/video 在哪，为什么 handler 里必须有 `ClosureGuard`？
3. **服务发现** · `scaffold/JianLiuScaffold/etcd.cc`、`rpc.cc` → 10s 租约、put 注册、KeepAlive、watch 先于 list、5s 周期全量、轮询 → 问：同地址新旧实例并存时，为什么不能按地址删节点？
4. **视频业务** · `svc_video/source/svc_data.cc` 的 `getMainPageList` → 3s 列表缓存、未命中回源、写缓存、分页 → 问：缓存 key 是什么，命中与回源各走哪些函数？
5. **数据层** · `server/data/data.h`（先看 `Video`）、`video.cc`、`video_idlist.cc`、`build/server/generated/data.sql` → ODB 实体/表映射、DAO 的 cache-aside、`_cache_delete_sync` → 问：改一个字段要动哪几处，缓存什么时候删？
6. **异步链路** · `svc_file/source/svc_rpc.cc` → `svc_video` 的 `newVideo` → `mq.cc` → `svc_transcode/source/svc_mq.cc` → `hls.cc`/`fdfs.cc` → 问：为什么上传不等转码，转码失败状态停在哪？
7. **中间件** · `redis.cc`、`mq.cc`、`odb.cc`、`elastic.cc` → 连接池、DLX + TTL、事务工厂、ES 的 `_enabled` 降级 → 问：MQ/ES 异常时服务为什么不崩，代价分别是什么？
8. **客户端再深入** · `netclient.cpp`、`playerpage.cpp`、`uploadvideopage.cpp`、`mpv/mpvplayer.cpp` → 38 个 async 与接口一一对应、m3u8 URL、libmpv 事件循环与截帧 → 问：`downloadVideo` 为什么免会话鉴权，m3u8 里的 ts 地址怎么来的？
9. **测试与脚本** · `server/tests/*.cc`、`smoke-test.sh`、`e2e-test.py`、`bench.py` → 8 项 gtest、4 项冒烟、18 项端到端、压测口径 → 问：哪条命令能在 5 分钟内证明主链路没坏？

### 2.4 登录：client → gateway → user → MySQL/Redis

1. 客户端 `startuppage.cpp` 的 `StartupPage::startTimer` 判断本地会话：没有或仍是游客走 `loginTempUserAsync`，否则走 `loginSessionAsync`；`netclient.cpp` 的 `NetClient::loginTempUser` 发 `POST /HttpService/tempLogin`。
2. 网关 `svc_rpc.cc` 的 `HttpServiceImpl::tempLogin`（L17）不鉴权；`_channels->getNode("user")`（`rpc.cc` L122）从本地节点表拿 Channel，没有节点就抛 `SERVICE_NOT_FOUND`。
3. brpc `UserService_Stub::tempLogin` 到 `svc_user/source/svc_rpc.cc` 的 `UserServiceImpl::tempLogin`（L8）。
4. `svc_user/source/svc_data.cc` 的 `SvcData::tempLogin`（L11）生成 20 位 sessionId，`SessionData::insert`（`data/session.cc` L14）写 `tbl_session_meta`。
5. 第一次 `SessionData::selectBySid` 回填 Redis `vp_session_<sid>`，TTL 3600 秒（`session.cc` L6/L151）；不是登录时立刻写缓存。
6. 响应 `result.sessionId`；客户端 `DataCenter::setSessionId`，启动页每 2 秒检查 `loginSuccess && mySelfInfoSuccess`，满足才 `close()`（`startuppage.cpp` L49–60）。
7. 日志关键词：网关 `收到临时用户登录请求...`、`临时用户登录失败!`；user `临时登录处理完毕: <rid>`；etcd `user 服务上线节点: ...:10002`。

### 2.5 首页列表：client → gateway → video → Redis → MySQL

1. 客户端 `NetClient::getAllVideoList`（L110）把 `pageIndex` 从 1 开始递增；服务端约定 0 会得到空列表。
2. 网关 `HttpServiceImpl::allVideoList`（L1349）先 `judgePermission(ssid, uri)`；`/HttpService/allVideoList` 在 `tbl_operator_role_meta` 中角色为 0（`deploy/sql/data.sql` L356），游客也放行。
3. 网关 `_channels->getNode("video")` 选一个 Channel，转发到 video 服务的 `VideoServiceImpl::allVideoList`。
4. video 的 `SvcData::getMainPageList`（L504）先读 Redis `cache_main_page_<page>_<count>`；值是 `AllVideoListResult` 的 protobuf 字节，TTL 3 秒（`setex` L539）。
5. 未命中才回源：`VideoIdListData::getMainPageList`（`data/video_idlist.cc` L14）先读 zset `main_page_list`（只含 `ONLINE(2)`，最多 1000 条，TTL 600–1200 秒），未命中再查 MySQL。
6. `getDetailVideoList`（L409）按 id 批量取视频、上传者、计数，组装 `VideoInfo`；这里仍有多次 DAO 查询，是后续优化点。
7. 日志/键关键词：网关 `收到获取主页视频列表请求...`；video `读取首页列表缓存失败`、`获取主页视频列表失败`；Redis `cache_main_page_*`、`main_page_list`、`cahce_video_*`。

### 2.6 上传：client → gateway → file → FastDFS → video 落库 → MQ

1. 客户端 `uploadvideopage.cpp` 选视频/封面；`NetClient::uploadPhoto`（L299）和 `uploadVideoAsync`（L376）把整个文件读进 `QByteArray`（L394），再 POST。
2. 网关 `uploadPhoto`（L664）要求 sessionId 并鉴权；`uploadVideo`（L777）从 `request_attachment()` 取裸字节，转成 protobuf attachment，用 HTTP 发到 file 节点，超时 300 秒（L811）。
3. file 的 `FileServiceImpl::uploadVideo`（L97）反序列化，`SvcData::getSessionUser`（L11）取上传者，`FDFSClient::upload_from_buff`（`fdfs.cc` L33）存 FastDFS，`newFileMeta`（L29）写 `tbl_file_meta`，返回 20 位项目 fileId。
4. 客户端 `NetClient::uploadVideoDesc`（L1018）再发 `POST /HttpService/newVideo`，body 是 `videoInfo`（videoFileId/photoFileId/title/desc/duration/type/tag）。
5. 网关 `HttpServiceImpl::newVideo`（L905）鉴权后转发；video 的 `SvcData::newVideo`（L33）生成 video_id、插入 `tbl_video_meta`、写分类标签，然后提交事务。
6. `Video` 默认状态是 `TRANSCODING(5)`（`data.cc` L160–165）；提交后向 `hls_transcode_queue` 发布 `HLSTranscodeMsg`；`VideoData::insert`（L20）只写 DB，详情缓存等读时回填。
7. 日志/数据关键词：file `视频上传用户`；video `新增视频元信息失败`；transcode `视频转码任务添加成功`；DB 中 `tbl_video_meta.status=5`。

### 2.7 转码/播放：MQ → transcode → HLS → FastDFS → client 播放

1. transcode 的 `TranscodeMQ::callback`（L110）反序列化消息，把每个 video_id 丢进 `SvcWorker` 线程池（`svc_worker.cc` L15），避免阻塞 amqpcpp 事件循环。
2. `TranscodeMQ::transcode`（L22）查 video/file 元信息，取 FastDFS path，`download_to_file` 下载到 `temp/transcode/<video_id>`（L34）。
3. `HLSTranscoder::transcode`（`hls.cc` L61）打开输入、复制编码参数（L88），设置 `hls_time=10`、`hls_base_url=/HttpService/downloadVideo/?fileId=`、`hls_playlist_type=vod`，写 HLS 头（L99）。
4. 解析生成的 m3u8，逐个上传 ts（L63）并写 `tbl_file_meta`；改写 m3u8 中的分片地址后上传 m3u8（L82），更新文件 path/mime。
5. 视频状态置 `AUDITING(1)`（L93），删除源文件（L96）；成功日志 `视频转码成功: <vid>`，失败日志 `视频转码失败: ...`，失败时状态停在 5。
6. 管理员 `checkVideo`（`svc_video/source/svc_data.cc` L255）置 `ONLINE(2)` 或 `REJECT(3)`，并 `syncPageListCache`；首页最多 3 秒后可见。
7. 播放：`playerpage.cpp` L119–126 拼 `serverUrl + /HttpService/downloadVideo?fileId=<m3u8>` 交给 mpv；网关 `downloadVideo`（L855）不校验 session，file 服务 `downloadVideo`（L161）从 FastDFS 下载，mpv 再按 m3u8 里的 URL 逐片拉 ts。日志关键词：`收到视频审核请求...`、`下载视频请求失败!`、`视频转码成功`。

## 3. 术语小抄与 10 个难点

### 3.0 术语小抄（先懂这 20 个）

| 术语 | 大白话（带类比） | 项目里看哪 |
| --- | --- | --- |
| 网关 | 所有请求的统一入口，先验会话/权限再转给子服务；像公司前台。 | `svc_gateway/source/svc_rpc.cc` `tempLogin`（L17）；监听 `:10000` |
| RPC | 像调本地函数一样调另一台机器的函数；像打电话让同事查个数。 | `scaffold/JianLiuScaffold/rpc.cc`；网关用 Stub 调子服务 |
| protobuf | 先写字段模板再自动生成收发代码，比 JSON 省空间；像固定格式的快递单。 | `server/proto/http.proto`（38 个 rpc）；`check-client-api.py` 验契约 |
| brpc（Channel/Closure） | Channel 是通往某服务的电话线，Closure 是打完电话执行的回调。 | `rpc.cc` `getNode`（L122）、`ClosureFactory::create`（L136）；`ClosureGuard` |
| etcd | 注册中心就是电话簿：服务启动写地址，调用方来查。 | `etcd.cc`；`curl` 查 `/user/...`、`/video/...` |
| lease（租约） | 不是永久户口，而是 10 秒有效期的门票，服务要续期，挂了自动删。 | `etcd.cc` `leasegrant(10)`（L37）+ `KeepAlive(...,10,...)`（L55） |
| watch（订阅） | 订阅后 etcd 主动通知，不用轮询；像关注公众号。 | `etcd.cc` `SvcWatcher::watch`（L95，先 watch 再 `ls`） |
| resync（全量同步） | 每 5 秒重拉整本电话簿兜底，防漏通知。 | `etcd.cc` `resync`（L135）、5 秒循环（L123） |
| cache-aside | 读：先查缓存，miss 再查库并回填；写：改库后删缓存；像先翻冰箱再买菜。 | `svc_data.cc` `getMainPageList`（L504） |
| TTL | 缓存/消息的保质期，到点自动失效；像牛奶的到期日。 | 同文件 `setex(cache_key, 3, bytes)`（L539） |
| 延迟双删 | 改库后删缓存，过 3 秒再删一次，堵住并发读回填旧值的窗口。 | `data/video.cc` `update`（L50）；`svc_sync.cc` `CacheDelete::sync`（L14）；`delete_cache_queue` 3s |
| DLX 延迟队列 | 消息先投普通队列设 TTL，到期变死信转到 `dlx_` 队列，消费者从死信队列取；像定时快递柜。 | `mq.cc` `declare`（L111）；`run-dev.sh` 两段 3s/30s JSON |
| 幂等 | 同一操作做多次结果和一次一样；像按开关，不是按「加一」。 | `mq.cc` `consume` 后 `ack`（L136）；`svc_transcode/svc_mq.cc`；项目没有去重表 |
| remux（转封装） | 只换外壳、不重压画面，快且画质不变；像换包装盒。 | `hls.cc` `transcode`（L61）、`avcodec_parameters_copy`（L88） |
| m3u8 / ts | 视频切成 ts 小段，m3u8 是带时长的播放清单；像分集 + 播放列表。 | `hls.cc` `M3U8Info::parse/write`；`--hls_time` 默认 10 |
| FastDFS fileId | tracker 指路、storage 存文件；上传得 FastDFS 路径，项目另发 20 位 fileId 存库。 | `fdfs.cc` `upload_from_buff`；`svc_file/svc_rpc.cc` `uploadVideo`（L97） |
| ODB / ORM | 用 C++ 类操作表、不手写 SQL，按 `#pragma db` 生成建表和读写代码。 | `server/data/data.h`（13 张表）；`odb.cc`；`build/server/generated/data.sql` |
| 事务 / 连接池 | 事务是一组操作全成功或全回滚；连接池是预建连接反复用。 | `svc_data.cc` 的 `odb::transaction`；`run-dev.sh` 池大小 5 |
| p99 | 99% 的请求都在这个毫秒数内完成；像 100 人排队看第 99 人的等待。 | `python3 scripts/bench.py --total 500 --concurrency 20` |
| 降级 | 依赖坏了先关相关功能，保住主流程；像停电只开应急灯。 | `video_search.cc` `_enabled=false` 返回空；`mq.cc` 声明超时 30s 后继续跑 |

> 加餐｜状态机：`VideoStatus` 5 转码中 → 1 待审核 → 2 已上架（6 转码失败，3 驳回，4 下架），见 `server/data/data.h` L31；CP2 会讲。

### 3.1 etcd lease + watch：服务为什么「自己消失」

- 大白话/卡点：注册不是永久户口，而是 10 秒租约；服务定时续租，进程死了就等租约过期自动删 key；容易把 lease 当普通 TTL；watch 与全量 list 有竞态；同一地址重启会产生新旧两个实例键。
- 代码锚点：`etcd.cc` `SvcProvider::registry`（L32，`leasegrant(10)` L37、`put` L44、`KeepAlive` L55）；`SvcWatcher::watch`（L95，先 watch 再 `ls`）；`resync`（L135，5 秒兜底）；`rpc.cc` `addNode`（L73）/`delNode`（L96）。
- 实验/自检：`curl -s -X POST http://dev-etcd:2379/v3/kv/range -d '{"key":"Lw==","range_end":"MA=="}' | python3 -m json.tool`；启动服务后应能看到 `/user/...`、`/video/...`、`/file/...`；自检：etcd 整个挂了，已启动的服务还能互相调用吗？新服务还能上线吗？（前者靠内存 Channel，后者不能。）
- 历史：最初 3s 租约在高负载下频繁过期，导致节点反复上下线，后改为 10s。

### 3.2 brpc Channel 与服务发现：网关怎么「找到」user

- 大白话/卡点：网关本地维护「服务名→地址列表」，每次 RPC 轮询选一条 Channel，像从电话簿里轮流拨号；Channel、Stub、Closure 三层容易混；`done` 只能 `Run()` 一次；没有节点时错误码是 `SERVICE_NOT_FOUND` 而不是连接超时。
- 代码锚点：`rpc.cc` `Channels::insert`（L7，去重 + `ChannelOptions` 30s 超时 L19）、`select`（L35，`_idx++ % size`）、`SvcChannels::getNode`（L122）、`ClosureFactory::create`（L136）。
- 实验/自检：`grep -n "timeout_ms" scaffold/JianLiuScaffold/rpc.cc server/svc_gateway/source/svc_rpc.cc`；停 user 后 `curl tempLogin`，看网关日志 `临时用户登录失败!`；重启 user 再试；自检：为什么 `getNode` 返回 `shared_ptr<Channel>` 而不是地址？
- 历史：`SERVICE_NOT_FOUND` 偶发（重启时旧 key 删除误删新实例）的修复是「按 etcd 实例键跟踪 + 同地址仍有实例则保留 + 5s resync」。

### 3.3 proto3 optional 与 JSON 契约：为什么改一个字段要动三处

- 大白话/卡点：proto 是客户端和服务端的「合同」；brpc 把 JSON 的驼峰字段映射到 proto 的 snake_case，字段缺失/存在靠 `optional` 的 presence；只改客户端不改 proto，`check-client-api.py` 会报；只加 rpc 不配 `tbl_operator_role_meta`，网关会 `PERMISSION_DENIED`。
- 代码锚点：`server/proto/http.proto` L9–49（38 个 rpc）；`base.proto` 的 52 个 `optional`；`scripts/check-client-api.py`；`svc_gateway/source/svc_data.cc` `judgePermission`（L7）。
- 实验/自检：`python3 scripts/check-client-api.py` → 38/38；`grep -rn "oneof" server/proto | wc -l` → 0；自检：`pageIndex=0` 为什么返回空？新增 `/HttpService/foo` 要改哪 4 个位置？（proto、gateway handler、子服务、权限表。）

### 3.4 ODB 对象关系：表结构为什么不写在 SQL 里

- 大白话/卡点：在 `data.h` 的类上写 `#pragma db`，构建时生成 `data-odb.*` 和建表 SQL；业务代码用对象和 `odb::query`，不手写 SQL；生成代码不在源码里；枚举/`nullable`/视图要按 ODB 语法写；连接池和事务是两回事。
- 代码锚点：`server/data/data.h` `VideoStatus`（L31）、`Video`（约 L330）；`scaffold/JianLiuScaffold/odb.cc` `DBFactory::mysql`（L5）；`build/server/generated/data.sql`。
- 实验/自检：`grep -c "#pragma db object table" server/data/data.h` → 13；`ls build/server/generated/data-odb.* build/server/generated/data.sql`；自检：加一张表要改哪几步？为什么 `data` 层不感知 RPC？

### 3.5 Redis 缓存一致性：为什么删一次还不够

- 大白话/卡点：读走 cache-aside；写先更 MySQL，再删缓存；3 秒后再删一次，防止并发读把旧值回填；计数只写 Redis，30 秒后回写 DB；项目里有两层列表缓存（3 秒整页快照、10 分钟 id zset）和一层详情缓存 `cahce_video_`（历史拼写）；「删缓存」与「回写计数」是两条不同链路。
- 代码锚点：`server/data/video.cc` `get`（L36）、`update`（L50）；`svc_video/source/svc_sync.cc` `CacheDelete::sync`（L14）、`CacheToDB::callback`（L75）；`svc_data.cc` `getMainPageList`（L504）/`setex`（L539）；`data/video_count.cc` `updateVideoPlayCount`（L21）/`syncCache`（L104）。
- 实验/自检：按 §7.1 的 Redis RESP 连上，请求一次列表后 `KEYS cache_main_page_*`、`TTL <key>`；自检：3 秒窗口内旧值怎么被清掉？如果 Redis 挂了，读路径和计数路径分别怎样？

### 3.6 DLX + TTL 延迟队列：消费的是「死信队列」

- 大白话/卡点：消息先投普通队列，队列设 `x-message-ttl` 和死信交换机；到期后转到 `dlx_` 队列，消费者订阅的是死信队列；发布仍投普通 exchange，消费却订阅 `dlx_` 队列；TTL 是队列级，队头阻塞会让后面的消息一起晚到。
- 代码锚点：`mq.cc` `declare`（L111）、`_declared`（L62，30 秒超时降级 L107）、`Subscriber::consume`；`scripts/run-dev.sh` 四段 JSON（L57–60）；`svc_video/source/svc_sync.cc`、`svc_file/source/svc_sync.cc`。
- 实验/自检：`grep -n "delayed_ttl\|_queue_info" scripts/run-dev.sh`，确认 3/30；启动后 `grep "订阅队列消息成功" logs/transcode.log`；自检：为什么删除缓存也要走队列？消息积压时 p99 会怎样？

### 3.7 消息幂等：项目里其实没有「幂等键」

- 大白话/卡点：同一条消息投两次，业务结果要一样；可靠 MQ 通常是 at-least-once，重复不可避免；`transcode` 用 video_id 做业务键，但没有消息 ID/去重表；第一次成功后 `video_fid` 已变成 m3u8，再消费会把 m3u8 当源文件，行为可能变成失败（待实验）。
- 代码锚点：`mq.cc` `consume`（L127，回调后 `ack` L136）；`svc_transcode/source/svc_mq.cc` `transcode`（L22）；全仓 `message_id|dedup|idempot|幂等` 搜索 0 处。
- 实验/自检：`grep -rni "message_id\|dedup\|idempot\|幂等" server scaffold | wc -l` → 0；自检：重复消费会覆盖哪些表/文件？你会把幂等键放在消息体还是 DB 唯一索引？

### 3.8 FastDFS file_id：项目 fileId 与 FastDFS path 是两套 ID

- 大白话/卡点：FastDFS 返回 `group/M00/...` 存储路径；项目又生成一个 20 位 `file_id` 写进 `tbl_file_meta`，客户端只见后者；下载时用项目 fileId 查 DB 拿 path，再用 path 找 FastDFS；storage 没起时错误是 `fdfs_recv_response fail, result: 2`。
- 代码锚点：`fdfs.cc` `upload_from_buff`（L33）、`download_to_buff`（L58）；`svc_file/source/svc_rpc.cc` `uploadVideo`（L97）、`downloadVideo`（L161）；`svc_data.cc` `getFileMeta`（L51）。
- 实验/自检：`grep -n "file_id\|path" server/data/file.cc | head`；有 MySQL 客户端时查 `select file_id,path,mime from tbl_file_meta limit 5;`；自检：为什么不直接把 FastDFS path 返回给客户端？fileId 当访问凭证的风险是什么？

### 3.9 FFmpeg remux 与 HLS：为什么「转码」其实没重编码

- 大白话/卡点：remux 只换容器（MP4→TS），复制编码参数，不重新压画面；HLS 再按关键帧切成 10 秒左右的 ts，并写 m3u8 清单；`hls_time` 是目标值，实际分片受关键帧影响；`hls_base_url` 决定 m3u8 里 ts 的 URL；源编码不被播放器支持时，remux 也会「成功」但播不了。
- 代码锚点：`hls.cc` `HLSTranscoder::transcode`（L61）、`avcodec_parameters_copy`（L88）、`hls_time`（L94）、`write_header`（L99）；`svc_mq.cc` L40/L63/L82。
- 实验/自检：`grep -n "hls_time\|hls_base_url" scripts/run-dev.sh`；跑一次 e2e 后 `curl -s 'http://127.0.0.1:10000/HttpService/downloadVideo?fileId=<m3u8>' | grep -c EXTINF`；自检：`hls_time=5` 分片数一定翻倍吗？为什么 ts 也要单独建 `tbl_file_meta`？

### 3.10 Qt 信号槽与异步网络：为什么启动页要等登录

- 大白话/卡点：`QNetworkAccessManager` 是异步的，请求发出后函数立刻返回；结果通过 `finished` 信号和 lambda 回到 UI 线程；启动页必须等登录信号才关；在 `finished` 回调外读 `QNetworkReply` 会拿到空/悬空数据；跨线程直接操作 UI 会崩；libmpv 事件在 mpv 线程，必须转回主线程。
- 代码锚点：`netclient.cpp` `sendHttpRequest`（L30）、`loginTempUser`（L80）、`uploadVideoAsync`（L376）；`startuppage.cpp` L49–60；`playerpage.cpp` `onPlayPositionChanged`（L214）；`playslider.cpp` `setPlayStep`（L20）；`mpv/mpvplayer.cpp` L111。
- 实验/自检：`grep -n "connect(.*finished\|QNetworkReply\|emit dataCenter" client/JianLiuPlayer/netclient/netclient.cpp | head -20`；自检：为什么 sessionLogin 非游客时要等 `getMyselfInfoDone`？`duration=0` 时不做保护会发生什么？

## 4. 六周执行计划

通用约定：命令都从仓库根目录执行；标【必做】的必须留下证据，标【选做】的时间不够可跳过；每周先 `git switch -c learn/wN`，改动后用 `git diff` 留档，回滚用 `/tmp/wN.patch`。第 2/4/6 周的 A/B/C 全部【必做】（对应 CP1/CP2/CP3）；第 3 周 C（ODB 查询）、第 5 周 C（0 时长复盘）为【选做】；第 1 周 Day5 的上传在 storage 不可用时降为【选做】。

### 第 1 周：跑起来 + 一次请求的全链路

**本周目标**：不看文档能启动 5 个服务，走通 `tempLogin → allVideoList → downloadPhoto`，画出登录链路并录 5 分钟讲解。

**前置检查**：`./scripts/run-dev.sh status` 期望 5 行「运行中」；`curl .../status` 期望 200；`./scripts/smoke-test.sh` 期望 4/4。

| Day | 1–2 小时动作 | 当天验收 |
| --- | --- | --- |
| Day 1 | 读 `README.md`、`docs/01-architecture.md` §1–2、`docs/06-code-walkthrough.md` §1–3；列 5 个进程/端口/日志文件 | 不看文档说出 gateway=10000、file=10001、user=10002、video=10003、transcode 无端口；知道 `main.cc` 默认 9000 与脚本 10000 的区别 |
| Day 2 | `./scripts/run-dev.sh start`；`status`；`./scripts/smoke-test.sh`；`tail -20 logs/video.log` | 5/5 运行、冒烟 4/4；说出 video 启动时做的 3 件事（建 MySQL/Redis、起 brpc、注册 etcd） |
| Day 3 | 按 §7.1 取 `SID`；发 `tempLogin`、`sessionLogin`；存 `/tmp/login.json` | `sessionId` 20 位、`isGuest=true`；能在日志指出网关→user 的两行 |
| Day 4 | 发 `allVideoList`；按 §7.1 看 Redis `cache_main_page_*`、`main_page_list`；发 `downloadPhoto` | 列表 `errorCode=0`，指出 `videoFileId` 与 `photoFileId`；能解释 3 秒缓存 |
| Day 5 | 上传一张图片/一个小视频，或跑 `e2e-test.py --session "$SID" --admin`；`tail -20 logs/transcode.log` | 拿到两个 fileId；`tbl_video_meta.status=5`；日志出现转码任务/成功；storage 未起则记录失败原因 |
| Day 6 | 在 `HttpServiceImpl::tempLogin`（L17 起）加 `std::chrono` 耗时日志，`DBG("tempLogin cost={}ms", ...)`；`./scripts/build.sh`；`./scripts/run-dev.sh start gateway`；再发一次 | diff ≤50 行、可编译；日志出现耗时；`git diff > /tmp/w1.patch` 可回滚 |
| Day 7 | 不看稿录 5 分钟登录链路；回答本周 3 个自测题；写周记 | 录音 ≥5 分钟；图上有 client→gateway→user→MySQL/Redis；周记 5 行 |

**单元 1（2h）· 入口/启动（Day1–2）**<br>读：`README`、`GatewayServerBuilder::build`、`run-dev.sh`；跑：`status`/`smoke`/`tail`；改：`tempLogin` 入口加 1 行 `DBG`；讲：为什么网关只做入口/路由/鉴权？；验收：日志出现 DBG，tempLogin 仍返回成功。
**单元 2（2h）· session/列表（Day3–4）**<br>读：user `tempLogin`、`SessionData::insert`、video `getMainPageList`；跑：3 个 curl + Redis `KEYS`；改：user `tempLogin` 打印 `ssid.size()`；讲：session 存哪、列表为什么 3 秒？；验收：ssid 长度为 20，sessionLogin 返回 isGuest=true。
**单元 3（2h）· 上传/转码/录音（Day5–7）**<br>读：file `uploadVideo`、video `newVideo`、transcode `svc_mq.cc`；跑：上传/e2e、`tail transcode.log`、`curl m3u8`；改：Day6 耗时日志（本周唯一保留 diff）；讲：上传为什么异步、状态 5→1→2？；验收：两个 fileId、status=5、转码日志/m3u8 可查（storage 可用时）。

**本周产出**：`/tmp/login.json`、`/tmp/list.json`、`/tmp/cover.png`；登录链路图；`/tmp/w1.patch`；5 分钟录音；周记。
**常见坑**：只重启 gateway 不重启 user；`config.ini` 放错目录；`downloadVideo` 不带 session 也能下；`pageIndex=0` 返回空。
**与 CP 的关系**：本周是 CP1 的「跑」和「讲」素材；CP1 第 2 周末做（见 §6.3）。

### 第 2 周：etcd + brpc 服务发现

**本周目标**：讲清「注册→发现→失效→兜底」，改 lease TTL 用数据证明下线延迟变化，完成 CP1。

**前置检查**：5 个服务运行；etcd range 查询期望 3 个节点；`/status` 200。

**单元 A（2h）· 注册与 watch**<br>读：`etcd.cc` `registry`（L32）、`KeepAlive`（L55）、`watch`（L95）、`resync`（L135）；`rpc.cc` `addNode`（L73）/`delNode`（L96）；回答「为什么 watch 先于 ls」「同地址新旧实例怎么去重」；跑：etcd range、`tail -20 logs/gateway.log` 看 `服务上线节点`；`kill $(cat logs/user.pid)` 后 2 秒再查；改：在 `SvcChannels::addNode` 加 `DBG("add node {} {}", svc_name, addr)`（≤3 行），重启 gateway；讲：lease 和 watch 各解决什么？watch 漏事件靠什么兜底？为什么不用 DNS/Nginx？；验收：能画出「启动→lease→put→KeepAlive→watch→本地表」的时序；节点表无重复地址。

**单元 B（2h）· 把 TTL 从 10s 改成 5s**<br>读：`etcd.cc` L37 `leasegrant(10)`、L55 `KeepAlive(...,10,...)`；`rpc.cc` 轮询选择；跑：改动前 `kill` video 并计时，记录到节点消失的秒数；`./scripts/build.sh && ./scripts/run-dev.sh start video` 恢复；改：两处 10→5（≤2 行），重建/重启 video；重复计时；填「10s/5s」对比表；讲：为什么不能设 1s？KeepAlive 失败会怎样？；验收：diff 可编译；两次测量有对比；能解释 5s 不是精确值。

**单元 C（2h）· CP1 三件套**<br>读：本手册 §6.3 CP1；`docs/notes/03-brpc-etcd.md`；跑：`start`/`status`/`stop`、3 个 curl、etcd 查询全部成功；改：沿用 B 的 TTL diff；讲：5 分钟白板：client→gateway→etcd→user→MySQL，覆盖注册、发现、失效；录音；验收：CP1 的「跑/改/讲」三件套齐全。

**本周产出**：etcd 节点记录、TTL 对比表、上下线时序图、CP1 录音、周记。
**常见坑**：`kill -9` 后要等租约过期；改 TTL 忘记重启；watch 与全量 list 的重复事件；同地址新旧实例误删。
**与 CP 的关系**：CP1（第 2 周末），必须过。

### 第 3 周：Redis 缓存 + MySQL/ODB

**本周目标**：讲清 cache-aside、延迟双删、计数回写，做一次有数据的压测对比，并写一个 ODB 查询验证接口。

**前置检查**：gateway/video 运行；`python3 scripts/bench.py --concurrency 20 --total 100` 能跑完；Redis `KEYS cache_main_page_*` 有键。

**单元 A（2h）· 读缓存与列表**<br>读：`svc_data.cc` `getMainPageList`（L504）、`setex`（L539）、`getDetailVideoList`（L409）；`video_idlist.cc` `getMainPageList`（L14）/`main_page_list`；`video.cc` `get`（L36）；跑：`bench.py` 记录 QPS/p50/p99；连续两次 `allVideoList`，看第二次是否命中；改：在 `getMainPageList` 命中/未命中分支各加一条 `DBG("main page cache hit/miss: {}", cache_key)`（≤4 行）；重建/重启 video；`grep -c` 计数；讲：缓存 key 是什么？3 秒 TTL 的代价？为什么列表 ID 还有一层 10 分钟 zset？；验收：日志能数出 hit/miss；能解释「第一次 miss、3 秒内 hit、过期再 miss」。

**单元 B（2h）· TTL 对比与延迟双删**<br>读：`video.cc` `update`（L50）、`svc_sync.cc` `CacheDelete::sync`（L14）、`CacheToDB::callback`（L75）、`video_count.cc` L21/L104；跑：基线 bench；把 `setex(cache_key, 3, bytes)` 改 30，重建/重启 video，再 bench；填 QPS/p50/p99/错误数表；改：只改 L539 的 3→30（1 行）；改回 3 并重启；讲：为什么先更库后删缓存？3 秒和 30 秒各自买什么？计数为什么只写 Redis？；验收：对比表有数字、有结论；能解释「为什么 QPS 可能变化不大」（瓶颈在 Redis/网络/序列化，参考 `docs/05-performance.md`）。

**单元 C（2h）· ODB 查询（可延到下周）**<br>读：`data.h` `VideoStatus`（L31）、`Video`（约 L330）；`data/video_idlist.cc` 的 ODB `Query`；跑：`ctest --test-dir build`；有普通用户会话时请求 `userVideoList`；改：给 `SvcData` 加 `countUserVideos(uid, status, mtx)`（≤20 行），在 `getUserPageList` 打印计数；验收日志计数 == 响应 `totalCount`；讲：ODB 相比手写 SQL 的取舍？`status` 是 tinyint 怎么比较？事务边界在哪？；验收：查询可编译；能解释 1/2/3/4/5/6 状态；没有测试用户时标记「未验证」。

**本周产出**：压测对比表、hit/miss 日志、ODB diff、1 页结论、周记。
**常见坑**：改完只 build 不重启；缓存里是 protobuf 不是 JSON；计数只写 Redis 导致直接查库「少了」；ODB 枚举要显式转换。
**与 CP 的关系**：为 CP2 的「缓存/数据」追问准备，无独立 CP。

### 第 4 周：MQ → transcode → HLS → 审核

**本周目标**：完整讲清上传→转码→HLS→审核，画 4 个队列，补一个失败分支，完成 CP2。

**前置检查**：`dev-storage:23000` open；transcode 运行；e2e 能走通；`tail -f logs/transcode.log` 可见订阅成功。

**单元 A（2h）· 四队列与一次转码**<br>读：`run-dev.sh` `DEL_FILE_Q/DEL_CACHE_Q/SYNC_CACHE_Q/TRANSCODE_Q`（L57–60）；`mq.cc` `declare`（L111）、`_declared`（L62）、`Subscriber::consume`；`svc_mq.cc` `transcode`（L22）；跑：e2e 触发上传/转码；`tail -20 logs/transcode.log`；从日志拿 m3u8 fileId，`curl` 看 `#EXTINF` 和 ts；改：在 `Publisher::publish` 加一条 `DBG("publish {} {}", exchange, routing_key)`（≤3 行）；讲：四个队列各谁发布/谁消费？延迟队列为什么消费 `dlx_`？消息积压会怎样？；验收：填出 4 行队列表；m3u8 有分片；状态 5→1 可解释。

**单元 B（2h）· hls_time 与分片**<br>读：`hls.cc` `transcode`（L61）、`avcodec_parameters_copy`（L88）、`hls_time`（L94）；`svc_mq.cc` L40/L63/L82；跑：记录当前 e2e 视频的 `#EXTINF` 数；`./scripts/run-dev.sh start transcode` 重启；改：把 `run-dev.sh` 的 `--hls_time=10` 改 5（1 行），重启 transcode，再 e2e/curl 数分片；改回 10；讲：`hls_time` 与关键帧的关系？为什么分片数不是严格倍数？remux 和重编码区别？；验收：改动前后分片数有记录；能解释 `hls_base_url` 如何拼出 ts URL。

**单元 C（2h）· 失败分支 + CP2**<br>读：`svc_mq.cc` 的 `download_to_file`/`transcode` 失败分支（L34/L40/L99）；`svc_data.cc` `getVideo` 可能返回空指针；跑：正常 e2e 一次作对照；构造「video_id 不存在」的消息（没有工具则用代码审查 + 单测）；改：在 `transcode` 取到 video/file 后加空指针检查（≤10 行）：`ERR` 带 video_id，直接返回 false，不更新状态、不删源文件；验收日志明确、状态仍 5、正常 e2e 18/18；讲：转码失败停在哪？消息丢了怎么发现/补？重复消费会怎样？10 分钟 CP2 录音；验收：失败分支 diff 可编译；正常链路不回归；CP2 三件套齐全。

**本周产出**：4 队列表、hls_time diff、失败分支 diff、转码时序图、CP2 录音、周记。
**常见坑**：storage 没起导致上传全失败；remux 成功但播放器不支持源编码；e2e 等 m3u8 有超时；转码无幂等键；审核后 3 秒才可见。
**与 CP 的关系**：CP2（第 4 周末），必须过。

### 第 5 周：客户端与接口契约

**本周目标**：说清每个页面调哪些接口，完成一个客户端小改造（大文件上限或 0 时长复盘），能解释契约检查在防什么。

**前置检查**：`python3 scripts/check-client-api.py` → 38/38；Windows/Qt 环境或 Linux+Xvfb 可用（本容器未验证）；`config.ini` 放运行目录。

**单元 A（2h）· 页面↔接口**<br>读：`netclient/netclient.cpp` 的 `sendHttpRequest`（L30）和各 `xxxAsync`；`model/datacenter.cpp` 的 `config.ini` 读取（L146–158，`QDir::currentPath()`）；`docs/07-client-integration.md` 页面映射表；跑：`check-client-api.py`；故意把一个 URL 改错，重跑看报错，再恢复；改：这次「破坏-恢复」就是改动（0 行净变更）；验收恢复后 38/38；讲：38 个 rpc 与 38 个调用如何一一对应？JSON 字段怎么映射到 proto？；验收：能画出登录页/首页/播放页/上传页/后台各调哪些接口。

**单元 B（2h）· 大文件上传上限**<br>读：`netclient.cpp` `uploadVideoAsync`（L376）、`loadFileToByteArray`（L394）；`uploadvideopage.cpp` 的提交流程；跑：准备一个 >50MB 视频；正常小视频上传一次；改：读取 `QFileInfo.size()`，>50MB 时 `Toast::showMessage("视频过大，请压缩后再上传")` 并 return（≤20 行）；验收大文件有提示、不崩溃、小文件仍成功；讲：整文件进内存的问题？流式/分片/预签名 URL 各怎么做？；验收：diff 可编译；有提示；能回滚。

**单元 C（2h）· 0 时长崩溃复盘**<br>读：`git show 92142e5 -- client/JianLiuPlayer/playerpage.cpp client/JianLiuPlayer/playslider.cpp`；`playerpage.cpp` `onPlayPositionChanged`（L214）、`playslider.cpp` `setPlayStep`（L20）；跑：有 GUI 时用 0.85 秒视频复现/验证；没有 GUI 时只读 diff，标记「未验证」；改：可选：在 `setPlayStep` 前加一条 `qWarning` 记录非法 ratio（≤3 行）；或只写复盘；讲：`duration=0 → playTime/duration=inf → double→int UB → setGeometry 非法宽度` 的完整因果链；为什么不要自动重播？；验收：能脱稿讲因果链；能指出两处保护；不把「推断」说成「实测」。

**本周产出**：页面↔接口表、客户端 diff、崩溃复盘、录音、周记。
**常见坑**：`config.ini` 不在工作目录；`QNetworkReply` 生命周期；跨线程信号槽；libmpv 线程操作 UI；共享内存残留导致「已在运行」。
**与 CP 的关系**：为 CP3 的「改 + 讲」积累素材。

### 第 6 周：测试 / 性能 / 部署 + 讲出来

**本周目标**：全新构建并跑全套验证，做一次有数据的优化对比，完成 15 分钟模拟面试和 CP3。

**前置检查**：`ctest --test-dir build` 8/8；`./scripts/smoke-test.sh` 4/4；有可登录的普通用户 + 管理员；`SID` 已取。

**单元 A（2h）· 全套验证**<br>读：`scripts/e2e-test.py` 覆盖项、`scripts/bench.py` 参数、`deploy/Dockerfile`/`docker-compose.yml`/`global.conf`（Compose 未实机验证）；跑：`rm -rf build && ./scripts/build.sh`；`ctest`；`smoke`；`e2e --session "$SID" --admin`；`check-client-api.py`；`check-doc-links.py`；`learn-check.sh`；改：可选：修一个文档断链或补一条测试注释（≤10 行）；讲：8/8、4/4、18/18、38/38 分别证明什么？哪些是「没测到」的？；验收：关键输出贴进周记；失败项有原因，不伪造通过。

**单元 B（2h）· 一次优化对比**<br>读：`docs/05-performance.md` 的基线与结论；`bench.py` 的并发/总量参数；跑：Debug 基线 `bench.py --concurrency 20 --total 300`；`BUILD_TYPE=Release ./scripts/build.sh` 后重启再跑（或连接池 5→20）；改：二选一：Release 构建，或 `--mysql_connection_pool_size` 5→20（1 行）；验收同一脚本、同一并发、有前后表；回滚；讲：为什么提升/没提升？瓶颈在哪？Release 无显著提升说明什么？；验收：1 页报告：假设→改动→方法→结果→结论→下一步；数字只来自本次实测，未测就写「未验证」。

**单元 C（2h）· 模拟面试 + CP3**<br>读：`docs/04-interview.md` 12 组追问；`docs/08-project-summary.md` 简历一页；本手册 §5；跑：从 §5 抽 10 题（架构 3 + 缓存 2 + MQ 2 + 排障 2 + 优化 1），手机录音 15 分钟，不看稿；改：不再改代码；把卡住的题补进周记；整理 1 个可演示 diff；讲：15 分钟模拟面试；回听并记录「卡住/说错」的点；CP3 自评；验收：10 题至少 8 题答到点；有 diff + 测试/验证；CP3 录音。

**本周产出**：全套测试输出、优化报告、15 分钟录音、简历一页、周记。
**常见坑**：`rm -rf build` 前没确认工作区；压测环境和文档不一致却直接对比；e2e 用游客会话导致上传步骤 SKIP；把文档里的 800 QPS 当成自己刚测的。
**与 CP 的关系**：CP3（第 6 周末），必须过。

## 5. 20 个面试问题与回答骨架

> 用法：先按「30 秒骨架」说，再补代码锚点；追问时只承诺自己验证过的内容。性能数字若引用 `docs/05-performance.md`，要说明「文档记录、本次未复测」。

### 架构

**Q1. 为什么拆成 5 个服务？网关的边界在哪？**
- 30 秒骨架：按业务能力+数据归属拆；user/video/file 各管自己的表，file 独占 FastDFS，transcode 是 CPU 密集且可异步；gateway 只做 HTTP 入口、鉴权、路由、协议转换，不持有业务数据；代价是网络调用、部署和排障复杂度。
- 代码锚点与追问：`svc_gateway/source/svc_server.cc` `GatewayServerBuilder::build`；`svc_gateway/source/svc_data.cc` `judgePermission`（L7）；`run-dev.sh` 的 5 个启动分支；追问：网关会不会成瓶颈？答：无状态可水平扩，会话在 Redis；当前单进程 brpc，需要再加 LB/限流/熔断。

**Q2. 网关怎么把 HTTP 变成内部 RPC？`ClosureGuard` 干什么？**
- 30 秒骨架：brpc Server 把 `/HttpService/xxx` 映射到 `HttpServiceImpl::xxx`；handler 从 etcd 节点表拿 Channel，构造 Stub 发异步 RPC，回调只负责收尾，由 brpc 把 protobuf 响应序列化为 JSON；`ClosureGuard` 用 RAII 保证 `done->Run()` 恰好一次。
- 代码锚点与追问：`svc_rpc.cc` `tempLogin`（L17）、`allVideoList`（L1349）；`rpc.cc` `ClosureFactory::create`（L136）、`asyncCallback`（L141）；追问：done 调两次会怎样？答：未定义/框架断言，必须靠 guard 和单一所有权；异常分支也要保证恰好一次。

**Q3. 38 个接口怎么保证客户端和服务端不漂移？**
- 30 秒骨架：`http.proto` 定义 38 个 rpc，客户端 38 个 `xxxAsync` 一一对应；`check-client-api.py` 检查接口存在性和 JSON 字段能否在 proto 找到；新增接口还要补 `tbl_operator_role_meta`，否则鉴权拒绝。
- 代码锚点与追问：`server/proto/http.proto` L9–49；`client/JianLiuPlayer/netclient/netclient.cpp`；`scripts/check-client-api.py`；`deploy/sql/data.sql` L301 起；追问：字段改名怎么办？答：proto 保留旧字段号、加新字段，客户端灰度；脚本只能查静态一致，查不了运行时语义。

### 缓存

**Q4. 首页列表 3 秒缓存怎么做的？一致性代价是什么？**
- 30 秒骨架：`getMainPageList` 先读 `cache_main_page_<page>_<count>`（protobuf，TTL 3s），命中直接返回；未命中查 `main_page_list` zset（ONLINE，1000 条）再查 DB，组装后回填；代价是上下架/审核最多 3 秒不可见。
- 代码锚点与追问：`svc_data.cc` `getMainPageList`（L504）、`setex`（L539）；`data/video_idlist.cc` `getMainPageList`（L14）、`insertMainPageListToCache`（L99）；追问：为什么不全量缓存 30 秒？答：可见性和一致性；3 秒是 e2e/产品可接受的折中。

**Q5. 什么是延迟双删？项目真的删了两次吗？**
- 30 秒骨架：更新 DB 后先删一次缓存，再发 3 秒延迟消息删第二次；第一次降低「旧值被读回」的窗口，第二次清掉窗口内被回填的旧值；不是强一致，只是缩小窗口。
- 代码锚点与追问：`data/video.cc` `update`（L50）；`svc_video/source/svc_sync.cc` `CacheDelete::sync`（L14，立即 `del` + 发消息）、`callback`（L35）；追问：3 秒够吗？答：没有理论上界；高并发要配合版本号/binlog/延迟消息重试，或接受最终一致。

**Q6. 播放/点赞计数为什么只写 Redis？30 秒后怎么回写？**
- 30 秒骨架：`setPlay`/`setLike` 只更新 `video_count_<vid>` 的 hash，发 30 秒延迟消息；消费者用 `WATCH` 监控 key，读 Redis 当前值，更新 MySQL，事务删除 key；`WatchError` 重试，避免覆盖并发更新。
- 代码锚点与追问：`svc_data.cc` `playVideo`（L172）、`likeVideo`（L142）；`data/video_count.cc` `updateVideoPlayCount`（L21）、`syncCache`（L104）；`svc_sync.cc` `CacheToDB::callback`（L75）；追问：消息丢了计数会怎样？答：Redis 有值但 DB 不更新，重启/淘汰后丢；需要补偿对账或 outbox。

**Q7. 缓存穿透/击穿/雪崩，项目做了哪些？还缺什么？**
- 30 秒骨架：项目做了 cache-aside、3 秒短 TTL、zset 10 分钟 + 随机 TTL；没有空值缓存、布隆过滤器、热点 key 互斥；单 Redis，没有集群/高可用；ES 挂了只是搜索降级。
- 代码锚点与追问：`data/video.cc` `getVideoFromCache`（L132，未命中返回空，不回填空值）；`svc_data.cc` `getMainPageList`；`video_idlist.cc` `_expire_time=600` + `Random::number`；追问：先做哪个？答：先空值短缓存 + 热点互斥 + TTL 抖动，再做 Redis 高可用；要补监控命中率和回源 QPS。

### MQ

**Q8. 四个队列分别做什么？延迟队列怎么实现？**
- 30 秒骨架：`hls_transcode_queue`（direct，转码）、`delete_file_queue`（direct，删 FastDFS 文件）、`delete_cache_queue`（TTL 3s + DLX，删缓存）、`cache_sync_to_db_queue`（TTL 30s + DLX，计数回写）；延迟队列是普通队列设 TTL/DLX，消费者订阅 `dlx_` 队列。
- 代码锚点与追问：`scripts/run-dev.sh` L57–60；`mq.cc` `declare`（L111）、`_declared`（L62）、`Subscriber::consume`；追问：为什么不用 `x-delayed-message` 插件？答：少一个插件依赖，部署简单；代价是固定 TTL、队头阻塞。

**Q9. 消息会丢吗？publisher confirm / ack / 持久化各解决什么？**
- 30 秒骨架：当前 publish 直接返回，未开 confirm；消费在回调后手动 ack；`declareQueue` 未显式设置 durable；所以不能承诺不丢；confirm 解决「到 broker」，durable 解决「broker 重启」，ack 解决「消费完成」，重试/DLQ 解决「消费失败」。
- 代码锚点与追问：`mq.cc` `publish`（L124）、`consume`（L127）、`ack`（L136）；追问：怎么补？答：confirm + durable + 手动 ack + 失败重试/死信 + 本地消息表/outbox；先做可观测（未确认计数）。

**Q10. 重复消费怎么办？转码真的幂等吗？**
- 30 秒骨架：没有消息 ID/去重表，不能直接说幂等；转码按 video_id 处理，但成功后 `video_fid` 已指向 m3u8，再消费会把 m3u8 当源文件，可能失败或产生孤儿 ts（待实验）；计数回写是「覆盖当前值」，相对更接近幂等。
- 代码锚点与追问：`svc_mq.cc` `transcode`（L22，先读 `video->video_fid()`，成功后 `set_status(AUDITING)`）；`mq.cc` ack（L136）；追问：怎么设计？答：消息带 event_id + 去重表/唯一索引；转码前检查状态必须是 `TRANSCODING(5)`；产物按 video_id 版本化，成功后清理旧文件。

### 服务治理

**Q11. lease、watch、resync 各解决什么问题？为什么 watch 先于 list？**
- 30 秒骨架：lease 让死实例自动过期；watch 让网关实时感知上下线；resync 每 5 秒全量兜底漏事件；先 watch 再 list，避免「list 之后、watch 之前」注册的节点丢失，重复部分靠实例键去重。
- 代码锚点与追问：`etcd.cc` `registry`（L32，lease 10s）、`watch`（L95）、`resync`（L135）；`rpc.cc` `addNode`（L73）/`delNode`（L96）；追问：同一地址重启，旧 key 删除会不会误删新实例？答：`_instances` 按 etcd 实例键跟踪，只有同地址没有其它实例才移除 Channel。

**Q12. etcd 整个挂了，系统会怎样？**
- 30 秒骨架：已建立 RPC 的网关仍用内存里的 Channel，能继续调；新服务无法注册，网关无法感知新上下线；`wait_for_connection` 会阻塞启动；运行时不会自动切到 DNS/静态配置。
- 代码锚点与追问：`etcd.cc` `wait_for_connection`（L11）、`registry`；`rpc.cc` 的 `_maps`；追问：怎么降级？答：etcd 3/5 节点集群、客户端本地缓存、静态 seed、健康检查；网关侧限流/熔断。

**Q13. 轮询负载均衡、超时、重试分别怎么配？**
- 30 秒骨架：`Channels::select` 用 `_idx++ % size` 轮询；内部 RPC 默认 30 秒超时；大文件上传单独 300 秒；没有自动重试，避免非幂等接口重复执行。
- 代码锚点与追问：`rpc.cc` `Channels::select`（L35）、`options.timeout_ms=30000`（L19）；`svc_rpc.cc` `options.timeout_ms=300000`（L811）；追问：30 秒合理吗？答：内部短调用应更短（如 1s）+ 有界重试；长上传单独通道；按接口幂等性决定重试。

### 音视频

**Q14. 上传→转码→HLS→审核的状态怎么流转？**
- 30 秒骨架：上传只落 FastDFS + `tbl_file_meta`；`newVideo` 写 `tbl_video_meta` 默认 `TRANSCODING(5)` 并发 MQ；transcode 下载、remux、上传 ts/m3u8，置 `AUDITING(1)`；管理员审核通过 `ONLINE(2)`/驳回 `REJECT(3)`；下架 `OFFLINE(4)`；失败当前停在 5，不置 6。
- 代码锚点与追问：`svc_file/source/svc_rpc.cc` `uploadVideo`（L97）；`svc_video/source/svc_data.cc` `newVideo`（L33）、`checkVideo`（L255）；`svc_transcode/source/svc_mq.cc` L22/L93；追问：失败怎么补偿？答：当前只有 ERR 日志，没有重试/补偿；可加状态 6、重试队列、人工重跑和源文件保留。

**Q15. remux 和 transcode 的区别？为什么点播用 HLS？**
- 30 秒骨架：remux 只换容器、复制编码参数，快且画质不变；transcode 要解码/重编码，能改分辨率码率但 CPU 贵；HLS 基于 HTTP 切片，兼容/穿透好，适合点播，代价是秒级延迟。
- 代码锚点与追问：`hls.cc` `transcode`（L61）、`avcodec_parameters_copy`（L88）、`hls_time`（L94）；`svc_mq.cc` L40/L63/L82；追问：多码率怎么做？答：当前单码率 remux；要多码率需多路重编码 + master m3u8，不在当前实现内。

**Q16. FastDFS 的 file_id 怎么用？下载为什么可以不带 session？**
- 30 秒骨架：FastDFS 返回 `group/M00/...` path，项目另生成 20 位 file_id 存 `tbl_file_meta`；下载先用 file_id 查 path，再取 FastDFS；`downloadVideo` 不鉴权，因为 mpv 拉 ts 时不一定带 session，fileId 被当作不可预测的 bearer token。
- 代码锚点与追问：`fdfs.cc` L33/L58；`svc_file/source/svc_rpc.cc` L97/L161；`svc_gateway/source/svc_rpc.cc` `downloadVideo`（L855，注释说明不鉴权）；`deploy/sql/data.sql` L339 角色 0；追问：安全风险？答：URL 泄露即泄露视频；应改签名 URL/过期时间/防盗链，或网关校验短时 token。

### 排障

**Q17. 线上偶发 `SERVICE_NOT_FOUND`，你怎么定位和修复？**
- 30 秒骨架：先看网关日志和 `run-dev.sh status`，再查 etcd 前缀；常见原因是服务没注册、lease 过期、watch/list 竞态、同地址新旧实例删除误删；项目修复是 lease 10s、watch 先于 list、按实例键跟踪、5s resync。
- 代码锚点与追问：`etcd.cc` L32–55、L95–135；`rpc.cc` `addNode`/`delNode`（L73/L96）；`svc_gateway/source/svc_rpc.cc` `tempLogin` 的 `SERVICE_NOT_FOUND`（L33）；追问：怎么复现？答：停 user，2 秒后 curl tempLogin；重启 user 等注册恢复；用 etcd range 确认 key 回来。

**Q18. ES 挂了、MQ 声明超时，服务为什么还活着？**
- 30 秒骨架：ES `init` 捕获异常置 `_enabled=false`，搜索返回空，其余业务继续；MQ 声明 `wait_for` 30 秒超时后只 `ERR` 并降级，不再 `abort`，但异步链路暂不可用；代价是没有 metrics/告警，故障可能静默。
- 代码锚点与追问：`server/data/video_search.cc` `init`（L13）、`_enabled` 判断（L42/L83）；`mq.cc` `_declared` 超时（L107）；追问：应该 abort 吗？答：看可用性目标；读多写少可降级，但要加健康检查和告警；启动阶段也可 fail-fast 配置开关。

**Q19. 0 时长视频为什么会让客户端崩溃？怎么修的？**
- 30 秒骨架：`duration=0` 时 `playTime/duration` 得到 inf/nan，double 转 int 是未定义行为，`setGeometry` 收到极端宽度，Windows 下崩溃；修复是 duration>0 才做除法、`std::isfinite` 保护、宽度夹取、播放结束不自动重播。
- 代码锚点与追问：`playerpage.cpp` `onPlayPositionChanged`（L214）、`onEndOfPlaylist`（L232）；`playslider.cpp` `setPlayStep`（L20）/`moveSlider`（L86）；提交 `92142e5`；追问：还有哪些除零/几何风险？答：进度拖动 `playRatio*duration`、弹幕动画宽度、窗口 resize；要统一夹取和断言。

### 性能

**Q20. 首页列表缓存带来了多少提升？下一步优化什么？**
- 30 秒骨架：文档记录 2 vCPU/Debug 基线约 276–290 QPS、p99 346ms，3 秒列表缓存后约 765–804 QPS、p99 94ms，约 2.8 倍；Release 无显著提升，说明瓶颈在 Redis/网络往返和 JSON 序列化，不是编译器。以上来自 `docs/05-performance.md`，本次未复测。
- 代码锚点与追问：`svc_data.cc` `getMainPageList`（L504）/`setex`（L539）；`scripts/bench.py`；`docs/05-performance.md` §2；追问：下一步先做什么？答：权限校验本地缓存、批量查用户消除 N+1、列表结果更长 TTL/分页缓存、网关多实例；先复测再下结论。

## 6. 进度、周记与检查点

### 6.1 6 周勾选表

每格填日期或「—」；「证据」填图/diff/录音路径。

| 周 | 读 | 跑 | 改 | 讲 | 证据 | CP |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | ☐ | ☐ | ☐ | ☐ | 3 JSON + 链路图 + 录音 | 为 CP1 |
| 2 | ☐ | ☐ | ☐ | ☐ | etcd 记录 + TTL 表 + 录音 | CP1 ☐ |
| 3 | ☐ | ☐ | ☐ | ☐ | 压测表 + hit/miss 日志 | 为 CP2 |
| 4 | ☐ | ☐ | ☐ | ☐ | 4 队列表 + 失败 diff + 录音 | CP2 ☐ |
| 5 | ☐ | ☐ | ☐ | ☐ | 接口表 + 客户端 diff | 为 CP3 |
| 6 | ☐ | ☐ | ☐ | ☐ | 测试输出 + 优化报告 + 录音 | CP3 ☐ |

### 6.2 每周周记模板（5 行以内）

```text
第N周｜日期：____
跑通：____（命令/接口/日志关键词）
画出：____（链路图/时序图）
改动：____（文件/函数/diff 路径/验收结果）
卡点与下周：____（一个没讲清的点 + 一个动作）
```

### 6.3 CP1–CP3 评分表

每个 CP 按 6 个维度打分，每项 0/1/2，满分 12；**≥9 分且「命令独立」「脱稿讲解」不为 0** 才算过。

| 维度 | 0 分 | 1 分 | 2 分 |
| --- | --- | --- | --- |
| 命令独立 | 每一步都要人给命令 | 偶尔提示或查笔记 | 不看笔记独立跑完，能解释输出 |
| diff 可运行 | 编译/运行失败 | 能跑但讲不清动机 | 可编译可运行，能讲动机、影响、回滚 |
| 脱稿讲解 | 全程照读 | 偶尔看稿 | 脱稿，能边讲边画链路图 |
| 接住追问 | 答不上或绕开 | 能答一部分 | 有取舍、有数据、有失败案例 |
| 证据齐全 | 没有输出/截图 | 只有零散截图 | 命令输出 + diff + 录音/图齐全 |
| 面试题覆盖 | 少于 5 题 | 5–7 题 | 至少 8 题 |

| CP | 跑 | 改 | 讲 |
| --- | --- | --- | --- |
| CP1（第 2 周末） | `run-dev.sh start/status/stop`；curl 登录/列表/下载；etcd range 查节点 | lease 10s→5s（或给 `tempLogin` 加耗时日志），diff 可编译可运行 | 5 分钟白板 `client → gateway → etcd → user → MySQL`，覆盖注册/发现/失效 |
| CP2（第 4 周末） | 触发一次上传/投递，`tail logs/transcode.log`，curl 到 m3u8；能指出日志行 | 给「源文件不存在」加失败分支并验证：有明确日志、状态不变、正常链路仍 18/18 | 10 分钟：上传 → 转码 → HLS → 审核 + 4 个队列 + 状态机，能讲消息丢失/重复消费 |
| CP3（第 6 周末） | 全新 build + `ctest` 8/8 + smoke 4/4 + e2e 18/18 + `check-client-api.py` 38/38 + bench | 一个已合并改进（diff + 测试/验证），可演示、可回滚 | 15 分钟模拟面试 10 题（架构/缓存/MQ/排障/优化），至少 8 题答到点 |

**检查点记录模板**（每个 CP 复制一份）：

```text
日期：____
跑：命令与输出摘要……
改：分支/commit/diff 摘要……
讲：录音时长、卡住的点……
得分：__/12
下一步：……
```

### 6.4 「学不动时」的 15 分钟最小行动（任选 1–2 个）

1. `ctest --test-dir build`，把 8 个 case 名字抄一遍，选 1 个读实现。
2. 不看文档画登录链路 5 个框，再对照 §2.4 补漏。
3. `grep -n "tempLogin" server/svc_gateway/source/svc_rpc.cc server/svc_user/source/svc_rpc.cc`，读完两个 handler。
4. 用 etcd range 命令查一次节点；没有节点就解释「为什么是 0」。
5. 用 Redis RESP `KEYS *`，挑一个 key 猜它属于哪条链路。
6. 录 1 分钟回答「网关怎么找到 user 服务」，回听并写 3 行改进。

### 6.5 「讲给面试官」的 3 分钟项目介绍模板

```text
1. 一句话：笺流是一个 C++17 的微服务视频点播系统，Qt6 客户端 + gateway + user/video/file/transcode，覆盖 RPC、服务发现、缓存、MQ、ORM、FastDFS、HLS。
2. 架构线：客户端只连 gateway（HTTP+JSON）；gateway 按 URL 鉴权并路由；子服务通过 etcd 注册/发现、brpc 调用；文件走 FastDFS，转码走 RabbitMQ 异步。
3. 三个取舍：①上传与转码解耦，用户不等转码；②列表 3 秒缓存换吞吐，代价是审核后最多 3 秒可见；③计数先写 Redis、30 秒回写 MySQL，换取写合并，代价是最终一致。
4. 验证：单测 8 项、冒烟 4 项、E2E 18 项、接口一致性 38 项；我实际跑过/没跑过的部分要如实说明。
5. 反思：消息没有 confirm/幂等键、缓存穿透未做、转码失败没有重试；如果重做，先补可靠性和可观测性。
```

## 7. 附录

### 7.1 命令速查

| 目的 | 命令 | 说明 |
| --- | --- | --- |
| 环境自检 | `./scripts/learn-check.sh` | 只读；中间件、服务、冒烟、单测、接口一致性、文档链接一次查完 |
| 构建 | `./scripts/build.sh` | 默认 Debug；Release 用 `BUILD_TYPE=Release ./scripts/build.sh` |
| 启停/状态 | `./scripts/run-dev.sh start` / `status` / `stop` | `start` 会先停旧进程；单服务 `start user`（file/user/video/gateway/transcode） |
| 冒烟 | `./scripts/smoke-test.sh` | 需网关在线，期望 通过 4 / 失败 0 |
| 单元测试 | `ctest --test-dir build` | 共 8 个 gtest case；`build/` 不存在先构建 |
| 客户端接口一致性 | `python3 scripts/check-client-api.py` | 服务端 rpc 与客户端调用各 38 项 |
| 文档链接 | `python3 scripts/check-doc-links.py` | 检查文档相对链接，期望 0 broken |
| 取会话 SID | `SID=$(curl -s -X POST http://127.0.0.1:10000/HttpService/tempLogin -H 'Content-Type: application/json' -d '{"requestId":"R1"}' \| python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["sessionId"])')` | 需网关在线；SID 供 passwdLogin / e2e 使用 |
| 登录测试用户 | `curl -s -X POST http://127.0.0.1:10000/HttpService/passwdLogin -H 'Content-Type: application/json' -d "{\"requestId\":\"R2\",\"sessionId\":\"$SID\",\"username\":\"tester@example.com\",\"password\":\"TestPass123\"}"` | `tester@example.com` / `TestPass123`（普通用户 + 管理员） |
| 端到端 | `python3 scripts/e2e-test.py --session "$SID" --admin` | 需普通用户会话；`--admin` 尝试管理员审核上架 |
| 压测 | `python3 scripts/bench.py --concurrency 20 --total 500` | 默认压只读接口 `/HttpService/allVideoList` |
| etcd 查注册节点 | `curl -s -X POST http://dev-etcd:2379/v3/kv/range -d '{"key":"Lw==","range_end":"MA=="}'` | v3 HTTP 的 key 是 base64；完整输出见下方代码块 |
| Redis 查询 | 用 Python 发 RESP（见下方代码块） | 容器内没有 `redis-cli` |
| 看日志 | `tail -f logs/gateway.log logs/video.log logs/transcode.log` | 日志在 `logs/<service>.log`，PID 在 `logs/<service>.pid` |
| MySQL（宿主机） | `docker exec -it dev-mysql mysql -uroot -p123456 vptest -e "select video_id,title,status from tbl_video_meta;"` | 当前开发容器无 docker，需在宿主机执行；只读查询 |
| git 回滚 | `git diff > /tmp/wN.patch && git apply -R /tmp/wN.patch` | 只回滚自己的改动；不要 `git checkout -- .` |

**etcd v3 range 查询**：

```bash
# / 的 base64 是 Lw==；枚举以 / 为前缀的 key 时 range_end 取 MA==
curl -s -X POST http://dev-etcd:2379/v3/kv/range \
  -d '{"key":"Lw==","range_end":"MA=="}' | python3 -m json.tool
```

正常能看到 `/file/xxx`、`/user/xxx`、`/video/xxx` 三个节点的地址；注册租约 10s，停服后节点最多约 10s 消失。

**Redis RESP 查询**（容器内没有 `redis-cli`）：

```bash
python3 - <<'PY'
import socket

s = socket.create_connection(("dev-redis", 6379), 2)

def cmd(*args):
    req = ("*%d\r\n" % len(args)).encode()
    for a in args:
        req += b"$%d\r\n%s\r\n" % (len(a.encode()), a.encode())
    s.sendall(req)
    return s.recv(8192)

print(cmd("AUTH", "123456"))
print(cmd("KEYS", "cache_main_page_*"))
print(cmd("GET", "cache_main_page_0_10"))
PY
```

**性能基线（文档记录，来自 `docs/05-performance.md`）**：

| 场景 | QPS | p99 | 说明 |
| --- | --- | --- | --- |
| Debug 基线（并发 20 / 50） | 276–290 | 346ms（50 并发） | 2 vCPU、列表每次回源 DB |
| 首页列表 3s 缓存 | 765–804 | 94ms（50 并发） | 命中直接返回，约 2.8 倍；代价是上下架最多延迟 3s 可见 |
| Release 构建（-O3） | 768 / 797 | 96ms（50 并发） | 与 Debug+缓存几乎无差异，瓶颈在 I/O/序列化 |

学习时以自己实测为准：`python3 scripts/bench.py --concurrency 20 --total 500`。

### 7.2 延伸阅读索引

> 本手册只做入口，不复制全文：这里只回答「读哪篇、什么时候读」。

| 文档 | 讲什么 | 什么时候读 |
| --- | --- | --- |
| [01-architecture.md](../01-architecture.md) | 进程视图、代码分层、数据模型、关键流程、容错降级 | 先建立全局图；面试前画架构图时 |
| [02-api.md](../02-api.md) | HTTP 接口约定、请求/响应示例、错误码 | 调接口、写客户端、被问接口设计时 |
| [03-deploy.md](../03-deploy.md) | 拓扑与端口、中间件连接参数、本地与容器部署 | 要复现环境或讲部署方式时 |
| [04-interview.md](../04-interview.md) | 3 分钟故事、架构/MQ/缓存追问、真实排错、STAR 写法 | 面试前 1 天，重点第 8 节工程与排错 |
| [05-performance.md](../05-performance.md) | 测试环境、QPS/p99、读路径瓶颈、优化清单 | 被问性能数据或「怎么优化」时 |
| [06-code-walkthrough.md](../06-code-walkthrough.md) | 代码地图、三条链路、常见改动、调试技巧 | 刚上手代码、要改功能时 |
| [07-client-integration.md](../07-client-integration.md) | 38 个接口一致性、页面↔接口映射、Windows 构建 | 联调客户端、讲 Qt 侧实现时 |
| [08-project-summary.md](../08-project-summary.md) | 一页纸定位、技术栈、质量证据、已知限制 | 写简历、开场介绍时 |
| [notes/01-redis-cache.md](../notes/01-redis-cache.md) | cache-aside、延迟双删、计数异步回写 | 被追问缓存一致性时 |
| [notes/02-rabbitmq-delay.md](../notes/02-rabbitmq-delay.md) | DLX + TTL 延迟队列、可靠性与幂等 | 讲转码解耦、延迟回写时 |
| [notes/03-brpc-etcd.md](../notes/03-brpc-etcd.md) | 租约/watch、轮询负载均衡、异步 Closure | 讲服务发现与 RPC 时 |
| [notes/04-odb.md](../notes/04-odb.md) | 实体/视图/事务/连接池/代码生成 | 讲数据层选型与 ORM 取舍时 |
| [notes/05-hls-ffmpeg.md](../notes/05-hls-ffmpeg.md) | remux、m3u8/ts、播放链路 | 讲转码与播放时 |
| [diagrams/jianliu-vod-e2e.html](../diagrams/jianliu-vod-e2e.html) | 可交互的端到端业务流程图 | 面试前过全链路、讲图时 |

**三条故障复盘（面试用）**：

- **etcd 旧 key 误删新实例**：网关偶发 `SERVICE_NOT_FOUND`；重启后同地址新旧实例并存，旧 key 租约到期删除时把新实例地址一起摘掉 → 修法：节点表按 etcd 实例键跟踪（同地址还有实例就保留信道）、租约 3s→10s、watch 先于全量、5s 周期全量同步兜底。
- **ES 不可用导致服务 abort**：ES 未启动时 video 服务在查询路径抛异常直接 `abort()`，整个服务不可用 → 修法：捕获异常并置 `_enabled=false`，搜索返回空列表，其余功能继续服务。
- **MQ 声明超时拖慢启动**：RabbitMQ 异常时，声明队列的 `condition_variable` 没有超时，启动线程永久等待，服务起不来 → 修法：声明加 30s 超时并降级（不 abort），MQ 能力暂不可用但进程照常启动。

**已知限制（讲项目时要诚实说）**：

1. **Windows 客户端未在本环境验证**：只在 Linux + Xvfb 完成编译与全链路 GUI 验证，Windows 需实机再证，并注意 `ffmpeg.exe` 的相对路径。
2. **Docker Compose 未实机验证**：当前环境没有 Docker；实测路径是 `scripts/run-dev.sh` + 本机中间件。
3. **ES 不可用/降级**：搜索当前返回空列表，只能说「降级不拖垮服务」，不能宣称「搜索可用」。
4. **MQ 无 publisher confirm、无幂等键**：极端情况可能丢消息；重复消费靠覆盖写、按 `video_id` 重跑兜底，不是严格幂等。
5. **大文件整块进内存**：上传把整个 HTTP body `to_string()` 放进 protobuf `filedata`，再整块交 FastDFS，大文件会放大内存占用；生产应改流式/分片。

### 7.3 文档与代码不一致（以代码为准）

| # | 原文档说法 | 代码事实 | 处理 |
| --- | --- | --- | --- |
| 1 | `02-api.md` §5 说 `newVideo` 后状态为「待审核(1)」 | `Video` 构造函数默认 `TRANSCODING(5)`（`server/data/data.cc` L160–165）；转码成功后才置 `AUDITING(1)`（`svc_mq.cc` L93） | 以 5 为准；审核通过才是 2 |
| 2 | `01-architecture.md` §6 说转码失败置「转码失败(6)」 | `TranscodeMQ::transcode` 的 `catch` 只打 `ERR`，没有 `set_status`（`svc_mq.cc` L99）；状态停在 5 | 失败分支验收按「保持 5」写 |
| 3 | `02-api.md` §1.3 的错误码表与枚举值整体错位 | 以 `server/common/error.h` 为准：0 成功、1 `INVALID_SESSION`、16 `PERMISSION_DENIED`、26 `VIDEO_NOT_EXISTS`、27 `TRANSCODE_FAILED`、28 `SERVICE_NOT_FOUND`、29 `RPC_REQUEST_FAILED` | 面试说「错误码枚举」，不要背文档数字 |
| 4 | 旧学习稿说 `getMainPageList` 在 L468 | 实际函数起点 `svc_data.cc` L504；`setex(cache_key, 3, ...)` 在 L539 | 以 L504/L539 为准 |
| 5 | `02-api.md` §1.1 说文件下载都带 `sessionId` | `downloadVideo` 明确「不做会话鉴权」，fileId 即访问凭证（`svc_rpc.cc` L855 起注释）；`downloadPhoto` 要求 query 带 sessionId，但权限表角色为 0，`judgePermission` 直接放行 | 按接口区分，不要笼统说「下载都鉴权」 |

补充口径：`main.cc` 里 gateway/file/user/video 的 gflags 默认端口是 9000/9001/9002/9003；`run-dev.sh` 显式传 `--listen_port=10000..10003`，所以本地联调端口是 10000–10003，transcode 不监听端口。不要把「默认值」和「联调值」混用。

### 7.4 手册自检记录（2026-10-24）

- ✅ 路径与命令：本手册引用的仓库路径均存在；`ctest` 8/8、`check-client-api` 38/38、`check-doc-links` 0 broken、`learn-check` 9/6/1（服务未启动）。
- ✅ 与代码对齐：修正了 2 处旧文档事实（`notes/03` 的租约 3s→10s、`04-interview` 的 7 个单测→8 个），以 `etcd.cc` 与 `ctest` 为准。
- ✅ 去重：6 周表只保留 §0 一张；命令只在 §7.1；术语只在 §3.0；周任务只在 §4；CP 评分只在 §6.3。
- ⚠️ 未验证：启动/停止服务、smoke 4/4、E2E 18/18、transcode/HLS、bench、Qt/Windows 客户端、Docker Compose。
- 面试时把「文档记录」与「我实测」分开说；未验证的项直接说未验证。
