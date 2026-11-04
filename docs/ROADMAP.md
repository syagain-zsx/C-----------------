# 笺流（JianLiu）· 重构路线图

> 版本：2026-10-18 ｜ 当前分支：`feature/vod-system` ｜ 当前阶段：**M3 接口联调**
>
> 这份文档是整个项目的"总纲"：先定义长远目标，再拆成可验收的里程碑，最后落到每周任务。
> 所有阶段性进展都记录在文末的「进度日志」里。

---

## 一、长远目标（North Star）

**把这个课程版视频点播系统，重构成一个「跑得起来、讲得清楚、写进简历」的个人作品。**

拆成三个层次：

| 层次 | 目标 | 判断标准 |
| --- | --- | --- |
| 工程 | 单一仓库、分层清晰、一条命令构建 | 新人拿到仓库，看 README 能在 30 分钟内跑起来 |
| 能力 | 吃透 11 个中间件封装 + 5 个微服务 + 1 个 Qt 客户端 | 每个模块都能白板画出调用链，说清"为什么这么选" |
| 求职 | 形成可复述的项目故事 | 简历 5 行描述 + 8 个能展开讲 5 分钟的技术点 |

**最终验收（Definition of Done）**

1. `cmake -S . -B build && cmake --build build` 一次通过，产出 5 个服务端可执行文件；
2. `deploy/` 下的脚本能一键拉起依赖中间件并启动全部服务；
3. 客户端能完成：登录 → 首页推荐 → 搜索 → 播放（HLS）→ 上传 → 转码 → 弹幕/点赞/关注；
4. `docs/` 里有架构图、接口说明、部署手册、学习笔记、面试问答；
5. 至少 1 组数据证明做过优化（如缓存命中率、RPC 压测 QPS、转码耗时对比）。

---

## 二、现状盘点（2026-10-18）

### 2.1 已有资产

| 资产 | 位置 | 状态 |
| --- | --- | --- |
| 脚手架基础库（11 个模块） | `scaffold/JianLiuScaffold/` | ✅ 已编译通过（静态库） |
| 服务端 5 个微服务 | `server/svc_*/` | 🟡 已导入，构建中 |
| 数据层（ODB 实体 + DAO，16 个类） | `server/data/` | 🟡 参与构建 |
| protobuf 接口定义（6 个 .proto） | `server/proto/` | ✅ protoc 生成通过 |
| Qt6 客户端 JianLiuPlayer | `client/JianLiuPlayer/` | ✅ Linux（Xvfb）完整验证；Windows 实机待验证 |
| 部署配置 | `deploy/`（compose/Dockerfile/global.conf/SQL） | ✅ 已适配新目录（Docker 未实机验证） |
| 中间件环境 | Docker 网络 `dev-environment_dev-network` | ✅ mysql/redis/etcd/rabbitmq/fastdfs 在跑；❌ ES 未启动 |

### 2.2 与课程原版的差异（本次重构做的事）

- **合并为一个仓库**：原课程 `client/` + `server/` + 脚手架库三方分离，现在 `scaffold + server + client + deploy + docs` 同仓；
- **统一构建**：删掉 5 个服务各自的 `CMakeLists.txt`（内容几乎复制粘贴），改成顶层一个函数批量生成；
- **重新组织目录**：`sql.d → deploy/sql`、`dockerfile → deploy/Dockerfile`、`global.conf → deploy/conf/`、`svc_file/client → tools/file-client`；
- **补上脚手架工程**：原课程的 `JianLiuScaffold` 是预装在镜像里的，仓库里只有源码没有构建；现在补了 `scaffold/CMakeLists.txt`（导出 `JianLiuScaffold::JianLiuScaffold`）。

### 2.3 已知缺口 / 风险

| 问题 | 影响 | 处理方案 |
| --- | --- | --- |
| 无 root 权限（sudo 需密码） | 不能 `apt install ffmpeg` | ✅ 已解决：`apt-get download` + `dpkg -x` 到 `~/local/ffmpeg`，CMake 通过 `JIANLIU_FFMPEG_ROOT` 引用 |
| `dev-icsearch` 不解析/9200 不通 | 搜索功能不可用 | ✅ 已降级：video 服务捕获连接异常，`_enabled=false`，搜索返回空，其余业务正常；待 ES 恢复后自动启用 |
| 客户端依赖 mpv DLL（108 MB，未入库） | 客户端无法直接运行 | 仓库提供 `client/README.md` 说明下载方式；`.gitignore` 排除 |
| 课程遗留 `data_test`（16 个独立小工程） | 目录噪音、无法统一构建 | ✅ 已删除，改为 `server/tests/` 的 7 个 gtest 用例（JSON/工具/M3U8） |
| 课程代码里注释掉的死代码、重复 `INSTALL()` | 可读性差 | 逐服务清理（M6） |

---

## 三、目标架构

### 3.1 全景图

```text
                        ┌──────────────────────────────┐
                        │   JianLiuPlayer (Qt6 + mpv)      │
                        │  登录/首页/搜索/播放/上传/后台 │
                        └──────────────┬───────────────┘
                                       │ HTTP + protobuf
                                       ▼
                        ┌──────────────────────────────┐
                        │  gateway_server  :10000      │
                        │  HttpService（brpc HTTP）     │
                        └───┬──────────┬───────────┬───┘
              brpc RPC      │          │           │
        ┌───────────────────┘          │           └──────────────────┐
        ▼                              ▼                              ▼
┌───────────────┐            ┌───────────────┐              ┌───────────────┐
│ user_server   │            │ video_server  │              │ file_server   │
│ :10002        │            │ :10003        │              │ :10001        │
└──┬────┬───┬───┘            └──┬───┬───┬───┘              └──┬───┬───┬────┘
   │    │   │                   │   │   │                     │   │   │
   ▼    ▼   ▼                   ▼   ▼   ▼                     ▼   ▼   ▼
 MySQL Redis 邮件           MySQL Redis ES(搜索)          FastDFS  MySQL Redis
                              │    │    │                    │
                              │    │    └── MQ: hls_transcode_queue
                              │    │                  │
                              │    └── MQ: cache_sync_to_db_queue
                              │                       ▼
                              │            ┌──────────────────────┐
                              │            │ transcode_server     │
                              │            │ 拉取 FDFS 视频 → FFmpeg│
                              │            │ → 切片上传 → 更新状态 │
                              │            └──────────────────────┘
                              └── etcd（服务注册/发现，所有服务共用）
```

### 3.2 请求链路（以"上传视频"为例）

1. 客户端把视频分片 + 元信息 POST 给网关 `uploadVideo`；
2. 网关转发 `FileService.uploadVideo` 到 file 服务（brpc，服务地址从 etcd 拿）；
3. file 服务把文件写进 FastDFS，返回 `file_id`，元信息落 MySQL/Redis；
4. 客户端再调 `newVideo`，网关转 `VideoService.newVideo`；
5. video 服务写视频元信息，向 MQ 发一条 `hls_transcode_queue` 消息；
6. transcode 服务消费消息：FDFS 下载 → HLS 切片 → 上传 ts/m3u8 → 回调更新视频状态；
7. 视频状态变为"上架"，客户端首页/列表即可见，播放时拉 m3u8 播放。

### 3.3 目标目录树

```text
vod-system/
├── CMakeLists.txt              # 顶层构建入口
├── README.md                   # 项目介绍 / 快速开始 / 架构速览
├── scaffold/                   # JianLiuScaffold 基础库
│   ├── CMakeLists.txt
│   └── JianLiuScaffold/          # log/util/rpc/etcd/mq/redis/odb/elastic/fdfs/hls/mail
├── server/
│   ├── CMakeLists.txt          # 公共库 + 5 个服务的统一构建
│   ├── common/                 # 错误码、缓存同步抽象
│   ├── data/                   # ODB 实体 + DAO（16 张表/类）
│   ├── proto/                  # 6 个 proto 文件
│   ├── svc_gateway/            # 网关（HTTP 入口）
│   ├── svc_user/               # 用户服务
│   ├── svc_video/              # 视频服务
│   ├── svc_file/               # 文件服务
│   └── svc_transcode/          # 转码服务（FFmpeg）
├── client/                     # Qt6 客户端（Windows 构建）
│   ├── JianLiuPlayer/
│   └── JianLiuMockServer/    # 客户端 Mock 服务（可选）
├── deploy/
│   ├── docker-compose.yml
│   ├── Dockerfile
│   ├── conf/global.conf
│   └── sql/                    # 建表 SQL
├── docs/                       # 架构 / 接口 / 部署 / 学习笔记 / 面试
├── scripts/                    # 构建、启动、初始化脚本
└── tools/                      # file_client 等联调工具
```

---

## 四、里程碑计划

> 时间按"业余每周 10–15 小时"估算，总计约 8 周。

| 阶段 | 目标 | 关键任务 | 验收标准（DoD） | 预计 |
| --- | --- | --- | --- | --- |
| **M0** 盘点与骨架 | 摸清家底，定目标 | 源码导入、目录重构、路线图 | 仓库结构落定，本文档完成 | ✅ 已完成 |
| **M1** 构建系统 | 一次构建出全部产物 | 脚手架 CMake、统一服务端构建、FFmpeg 本地前缀 | `cmake --build build` 产出 5 个可执行文件 | ✅ 已完成 |
| **M2** 服务端可运行 | 5 个服务能启动、注册到 etcd | 配置文件、启动脚本、端口、日志 | `etcdctl get --prefix /` 能看到 4 个注册节点；`curl :10000/` 有响应 | ✅ 已完成 |
| **M3** 核心接口联调 | 登录/视频/文件主链路通 | 用 `file_client`、curl 或脚本验证 RPC | 能完成"注册登录 → 上传 → 建视频 → 列表查询" | ✅ 已完成（含转码/HLS/点赞/弹幕） |
| **M4** 客户端打通 | JianLiuPlayer 能连真实后端 | 接口地址配置、编译、修 bug | 登录、首页、播放页可用 | ✅ Linux（Xvfb）下完整验证，Windows 实机待验证 |
| **M5** 业务闭环 | 转码 + HLS 播放 + 互动 | FFmpeg 转码链路、弹幕、点赞、关注、搜索 | 上传的 mp4 能自动转 HLS 并在客户端播放，互动数据正确 | 🔄 服务端已完成（转码/HLS/点赞/弹幕/关注/审核），客户端播放待 M4 |
| **M6** 质量与部署 | 可交付 | 清理死代码、补测试、docker-compose、一键脚本 | `docker compose up` 起全栈；gtest 通过 | 🔄 进行中（测试/脚本/编排已完成，Docker 未在本环境验证） |
| **M7** 文档与求职 | 能讲出来 | 架构文档、学习笔记、面试题、简历 | docs 完整；能脱稿讲 10 分钟 | 1 周 |
| **M8** 优化与压测 | 有数据支撑 | 缓存命中、连接池、RPC 压测、转码耗时 | 一张压测/优化对比表 | ✅ 列表缓存 290→804 QPS（2.8x） |

### M1 任务清单

- [x] `scaffold/CMakeLists.txt`：11 个模块 → `JianLiuScaffold::JianLiuScaffold`
- [x] 顶层 `CMakeLists.txt` + `server/CMakeLists.txt`：protoc/ODB 生成 + 公共库 + 5 服务
- [x] 移除 5 个复制的服务级 CMakeLists，改为 `add_vod_service()` 统一生成
- [x] FFmpeg 依赖落到 `~/local/ffmpeg`（无 root 方案），transcode 参与构建
- [x] 构建产物验证：`build/bin/{gateway,user,video,file,transcode}_server`
- [x] `scripts/build.sh` 固化构建命令

### M2 任务清单

- [x] `scripts/run-dev.sh`：一键启动/停止 5 个服务（按服务裁剪 gflags）
- [x] etcd 注册验证：`/file`、`/user`、`/video` 三个节点 + 租约
- [x] 网关服务发现：watch 上线/下线事件正常
- [x] `curl :10000/status` 返回 HttpService 状态页
- [ ] `deploy/conf/local.conf`（改用脚本传参后暂不需要，保留课程配置做参考）

---

## 五、近期两周计划（M1 → M3）

**第 1 周**

1. 完成 M1 全部验收（5 个可执行文件 + transcode）；
2. 写 `scripts/build.sh`、`scripts/fetch-ffmpeg.sh`（记录无 root 依赖方案）；
3. 本地起服务：先 gateway + user，验证 etcd 注册与 HTTP 响应；
4. 补 `deploy/conf/local.conf`（把 `dev-mysql` 等容器名换成 127.0.0.1/容器名可配）。

**第 2 周**

5. 起全 5 个服务，跑通 `tempLogin → sessionLogin → getCode/regOrLogin`；
6. 用 `tools/file-client` 验证 FastDFS 上传/下载；
7. 用 SQL 初始化 `vptest` 库，验证 ODB 建表与读写；
8. 输出 `docs/02-api.md` 的第一版（登录 + 文件 + 视频接口）。

---

## 六、学习路线（与工程并行，求职导向）

> 配套的 6 周学习手册（环境、代码地图、术语、难点、每周任务、CP 评分、面试）见
> [`docs/learning/ai-study-plan.md`](learning/ai-study-plan.md)；本节的“四步法”是它的理论底座。

### 6.1 每个模块的"四步法"

> 不要只"看懂"，要能**默写调用链**。

1. **原理**：这个中间件解决什么问题？核心机制是什么？
2. **本项目**：`scaffold/JianLiuScaffold/xxx.h` 封装了哪些能力？服务端怎么用？
3. **动手**：改一处 `scaffold/JianLiuScaffold/xxx.cc` 的实现，或者加一个功能验证理解；
4. **输出**：写一页笔记 + 3 个面试问答，放进 `docs/notes/`。

### 6.2 模块学习顺序（由浅入深）

| # | 模块 | 学习重点 | 面试高频 |
| --- | --- | --- | --- |
| 1 | log（spdlog） | 同步/异步、日志级别、轮转、格式 | 为什么异步日志？丢了怎么办？ |
| 2 | util（jsoncpp） | 序列化/反序列化、optional 返回值 | 为什么用 optional 而不是异常？ |
| 3 | redis | 连接池、string/hash/zset 选型、缓存更新 | 缓存穿透/击穿/雪崩、双写一致性 |
| 4 | odb | ORM 映射、事务、连接池、视图 | ODB vs 手写 SQL 的取舍 |
| 5 | rpc（brpc） | Channel、超时、重试、负载均衡 | brpc 为什么快？bthread 是什么？ |
| 6 | etcd | 租约保活、watch、服务发现 | 为什么不用 ZooKeeper？ |
| 7 | mq（RabbitMQ） | 交换机类型、TTL、死信队列、延迟消息 | 如何用 DLX 实现延迟队列？消息幂等 |
| 8 | elastic | 倒排索引、分词、查询 DSL | ES 和 MySQL like 的区别 |
| 9 | fdfs | Tracker/Storage、文件 ID | 为什么要用分布式文件系统 |
| 10 | hls/FFmpeg | 封装格式、HLS 切片、m3u8 | HLS 与 RTMP 区别、为什么点播用 HLS |
| 11 | 微服务 | 网关、服务拆分、配置、部署 | 一次请求怎么串起来？如何排障？ |

### 6.3 面试作品讲述模板（5 分钟版）

1. **背景**：一个 C++ 视频点播系统，微服务架构，支持上传、转码、播放、弹幕；
2. **我的职责**：脚手架封装 + 服务端 + 客户端全链路（可强调你重构的部分）；
3. **架构**：客户端 → 网关 → 3 个业务服务 → 中间件（画图）；
4. **难点**：① 延迟队列做缓存同步；② 转码异步化；③ 服务发现与容错；
5. **数据**：QPS / 缓存命中率 / 转码耗时（M8 补）；
6. **反思**：哪里设计得不好，如果重做会怎么改。

---

## 七、风险与决策记录

| 日期 | 决策 | 原因 |
| --- | --- | --- |
| 2026-10-18 | 合并为单仓库 `vod-system` | 课程三方分离，构建/联调成本高 |
| 2026-10-18 | 服务端统一 CMake | 5 份几乎相同的 CMakeLists，改一处要改五处 |
| 2026-10-18 | FFmpeg 走本地前缀 | 容器内无 root，不能 apt 安装 |
| 2026-10-18 | `data_test` 暂不参与构建 | 16 个独立工程，先保证主链路，M6 再改 gtest |

---

## 八、进度日志

### 2026-10-18（M0 / M1 / M2 完成，M3 进行中）

**仓库与构建**

- 导入 `server`（150 文件）、`client`（源码，剔除 build / mpv DLL / 测试视频）；
- 目录重构：`cpp-microservice-scaffold/source → scaffold/JianLiuScaffold`，`example → examples`；
- 新增 `scaffold/CMakeLists.txt`（11 模块 → `JianLiuScaffold::JianLiuScaffold`，自动探测依赖）；
- 重写 `server/CMakeLists.txt`：protoc + ODB 自定义命令 → `libvideoplayer.a` → 5 个服务；
- 统一 LF 换行 + `.gitattributes`；`.gitignore` 重写；
- **`cmake --build build` 通过，产出 5 个可执行文件**（含 FFmpeg 转码服务）。

**依赖**

- 无 root 安装 FFmpeg 成功：`apt-get download`（含依赖解析）+ `dpkg -x` → `~/local/ffmpeg`，
  运行时 `LD_LIBRARY_PATH`；脚本 `scripts/fetch-ffmpeg.sh` 固化流程；
- 补上 `cpprest`、`odb-boost` 两个隐藏链接依赖（链接期才暴露）。

**运行（M2）**

- MySQL 初始化：`deploy/sql/data.sql` → 13 张表（库 `vptest`）；
- Redis（AUTH 123456）/ etcd / RabbitMQ / FastDFS 连通性验证；
- `scripts/run-dev.sh` 启动 5 服务；etcd 注册 `/file`、`/user`、`/video`；
- 网关 watch 到服务上线/下线；`curl :10000/status` 正常。

**接口（M3 完成）**

```bash
curl -X POST http://127.0.0.1:10000/HttpService/tempLogin \
     -H 'Content-Type: application/json' -d '{"requestId":"R1"}'
# -> {"errorCode":0,"result":{"sessionId":"..."}}

curl -X POST .../HttpService/sessionLogin   # -> isGuest=true（未登录）/ false（已登录）
curl -X POST .../HttpService/allVideoList   # -> 视频列表
```

**端到端联调结果（2026-10-18）**

- 造数：SQL 直插普通用户 + 绑定会话（绕过邮件验证码），清 Redis 会话缓存；
- 链路：`tempLogin → sessionLogin(isGuest=false) → uploadPhoto(6480B) → downloadPhoto(字节一致)`
  → `uploadVideo(318KB) → newVideo → MQ 转码 → HLS(1 个 m3u8 + 2 个 ts 上传 FastDFS)`
  → `allVideoList（返回视频，playCount=1）→ setLike/judgeLike → newBarrage/getBarrage`；
- 转码后视频状态为「待审核(1)」，通过审核接口置为「上架(2)」后出现在首页列表。

**审核 / 上下架（管理端链路）**

- 给测试用户追加 B 端身份（`identify_type=2`）与普通管理员角色（`role_type=2`），清 Redis 角色缓存；
- `checkVideo(checkResult=true)` → 状态 `ONLINE(2)` 且写入 `auditor_uid`；
- `haltVideo` → `OFFLINE(4)`；`saleVideo` → `ONLINE(2)`；`allVideoList` 始终可查到该视频。

**命名重构（品牌：笺流 / JianLiu）**

- `bite* → JianLiu*`：11 个命名空间（`JianLiuLog`、`JianLiuUtil`、`JianLiuRpc`、`JianLiuSvc`、
  `JianLiuMq`、`JianLiuRedis`、`JianLiuOdb`、`JianLiuIcs`、`JianLiuFdfs`、`JianLiuHls`、`JianLiuCode`）；
- 基础库 `bite_scaffold → JianLiuScaffold`（目录、target、头文件路径、CMake 选项 `JIANLIU_*`）；
- 客户端品牌：窗口标题与应用名改为「笺流」，资源图 `biteshipin.png → jianliushipin.png`。

**过程中修掉的问题**

1. `pplx::get_ambient_scheduler` 未定义 → 链接 `cpprest`；
2. `odb::boost::date_time` 未定义 → 链接 `odb-boost`；
3. `hls.h` 被 file 服务包含 → HLS 必须随 FFmpeg 一起启用（不再"可选关闭"）；
4. ES 不可用时 video 服务 `abort()` → 改为告警 + 搜索降级；
5. 转码服务不认识 redis/etcd 参数 → 启动脚本按服务裁剪 gflags。

### 2026-10-18（第二轮：质量与部署）

- 删除 `server/data_test`（16 个无法构建的旧工程），新增 `server/tests/`：
  `test_json.cc`（JSON/字符串/随机串/文件读写）+ `test_m3u8.cc`（m3u8 解析与写回），
  **7 个 gtest 用例全部通过**（`ctest --test-dir build`）；
- 修复 FFmpeg 私有前缀的运行时问题：链接加 `-Wl,--disable-new-dtags` 生成 `DT_RPATH`，
  让 `libavformat` 的间接依赖（libgme/libvpx 等）也能被找到，二进制不再依赖 `LD_LIBRARY_PATH`；
- 部署编排适配新目录：`deploy/Dockerfile`、`deploy/docker-compose.yml`、`scripts/package.sh`（收集 .so），
  镜像把依赖库放 `/usr/local/lib/jianliu` 避免覆盖系统库；
- 新增 `docs/03-deploy.md`：本地部署（实测）+ Docker（未验证）+ 常见问题；
- 验证 gflags 行为：`--flagfile` 中的未知参数会被忽略（Docker 共用一份配置可行），命令行传未知参数会报错。
- 从零构建验证：`BUILD_DIR=/tmp/build-check ./scripts/build.sh` 通过，产出 5 个可执行文件；
- 压测基线：`allVideoList` 在 20 并发下 276 QPS（p50 68ms），50 并发下 290 QPS（p99 346ms），
  2 核 Debug 环境，脚本 `scripts/bench.py`，分析见 `docs/05-performance.md`。
- 端到端回归脚本 `scripts/e2e-test.py`：会话→上传→建视频→转码→m3u8/ts→审核上架→点赞/播放/弹幕，
  **18 项全部通过**。

**业务闭环补验（服务端）**

- 关注/取关：user2 对 user1 `newAttention` → `followerCount 0→1`、`isFollowing=false→true`；`delAttention` 后回到 0；
- 搜索降级：ES 不可用时 `keyVideoList` 返回 `errorCode=0` + 空列表，服务不崩溃；
- 缓存回写：`setPlay` 后 video 日志出现「延迟将缓存播放量同步到数据库中」，队列链路正常。

### 2026-10-18（第三轮：联调、稳定性与性能）

- 端到端脚本 `scripts/e2e-test.py` 完成并跑通 **18/18**（会话→上传→建视频→转码→m3u8/ts→审核上架→点赞/播放/弹幕）；
- **修复服务发现抖动**（网关偶发 `SERVICE_NOT_FOUND`）：
  1) etcd 租约 3s → 10s，避免高负载下过期；2) 先建 watch 再全量拉取，消除启动窗口丢事件；
  3) 节点表按 etcd 实例键（`/svc/instance_id`）跟踪，旧实例 key 到期删除时，只有该地址没有其它实例才摘除信道；
  4) 网关每 5s 周期性全量同步，兜底漏掉的事件；
- 修复用户服务启动即崩溃：`DBG("...{}-{}", rid)` 占位符多于实参触发 spdlog 异常链，
  修正日志并给 spdlog 设置只打印不抛出的错误处理器；
- **性能优化**：首页列表加 3s 短缓存（protobuf 存 Redis），`allVideoList`
  20 并发 276 → **765 QPS**，50 并发 290 → **804 QPS**（p99 346 → 94ms），约 2.8 倍；
- 文档同步：`docs/05-performance.md` 增加优化对比，`docs/04-interview.md` 增加发现抖动/日志崩溃两个真实排错案例。

### 2026-10-18（第四轮：客户端联调准备）

- 新增 `scripts/check-client-api.py`：静态比对客户端 `netclient` 调用与服务端 `http.proto`，
  **38/38 接口一致、JSON 字段全部匹配**（`pageIndex` 等驼峰字段由 brpc 自动映射）；
- 新增 `docs/07-client-integration.md`：页面↔接口映射、`config.ini`/libmpv 说明、Windows 构建步骤、
  8 步联调顺序与已知差异（分页从 1 开始、上传是裸字节流、列表 3s 缓存、ES 降级）；
- 新增 `client/JianLiuPlayer/build-windows.ps1`：一行命令完成 MinGW 配置 + 构建，并检查 libmpv；
- 密码改为加盐 SHA-256（64 位十六进制 = 32 位盐 + 32 位摘要），`setPassword` 存哈希、
  `passwdLogin` 校验，兼容历史明文密码；新增 1 个单测（哈希/校验/盐随机/明文兼容）。

### 2026-10-18（第五轮：联调工具与收尾）

- 重写文件服务工具 `tools/file-client/file_client.cc`：支持上传（图片 RPC / 视频 HTTP attachment）、
  下载（`--download_id`）、`--session_id/--file/--mime/--output`，移除无用 MQ 依赖与注释代码；
  实测图片上传/下载字节一致、视频上传成功；已加入 install（`build/bin/file_client`）；
- `SvcWatcher` 退出优化：resync 用 condition_variable 等待、析构立即唤醒、置 `_destroying` 防重入、
  调用 `Watcher::Cancel()`；CLI 退出更干脆（库仍会打印一行无害的 watcher 警告）；
- 文档：README/部署手册补充 CLI 用法，测试数量更新为 8 项。

### 2026-10-18（第六轮：客户端编译验证）

- 在容器内用「apt 下载 + dpkg 解包」方式准备 Qt 6.2.4 Base 与 libmpv 0.34（私有前缀，不需要 root），
  成功编译 `client/JianLiuPlayer`（17MB 可执行文件）；
- 修复 3 个真实问题：
  1) Qt 组件名 `network` → `Network`（Linux 大小写敏感，Windows 下同样建议大写）；
  2) CMake 源文件列表引用了已剔除的 `mpv/mpv/*.h`（导入时排除了该目录）；
  3) `QJsonValue = int64_t` 在 Linux 上二义（`int64_t` 是 `long`，`qint64` 是 `long long`），显式转 `qint64`；
- CMake 改为跨平台链接 mpv：Windows 用 `mpv/dll/libmpv-2.dll`，Linux 用系统 `libmpv`；
- **验证结果**：`QT_QPA_PLATFORM=offscreen` 启动客户端，成功 `tempLogin` 并拉取 7 个视频；
  播放画面/Windows 实机仍待用户验证。

### 2026-10-18（第七轮：客户端清理与 Release 对比）

- 客户端清理：删除未使用的 `hello/ping` 接口、`DataCenter` 对应信号与 `client/JianLiuPlayer/test/`
  课程草稿；重新编译通过，服务端日志确认仍能 tempLogin + 拉列表；
- Release 构建（-O2/-O3）验证通过（`/tmp/build-release`，6 个可执行文件）；
- 压测对比：Debug+缓存 765/804 QPS，Release+缓存 768/797 QPS —— **无显著差异**，
  说明该读路径瓶颈在 Redis/网络往返与 JSON 序列化，优化应针对瓶颈而非编译器；
- 服务已切回 Debug 构建运行，冒烟 4/4。

### 2026-10-18（第八轮：客户端 GUI 完整验证 + 截图）

- 在容器内搭 Xvfb 虚拟显示（私有前缀 + 修补 xkbcomp 路径 + 中文字体），跑起客户端 GUI；
- **完整链路验证**：启动 → tempLogin → 首页 7 个视频 + 封面（FastDFS 下载）→ 点击播放
  HLS（m3u8 + ts，libmpv 软件渲染）→ 弹幕输入框、点赞/播放计数展示；
- 截图入库：[docs/images/home.png](images/home.png)、[docs/images/player.png](images/player.png)；
- 修复 file 服务被 MQ 拖死的问题：声明/订阅超时与错误回调不再 `abort()`，改为记录日志并降级运行；
- 测试数据整理：把 E2E 生成的视频标题改成演示名称（海边日落/城市夜景 4K/…）。

### 2026-10-18（第九轮：管理端 GUI 验证）

- 用 xdotool 在 Xvfb 里完成管理员操作：登录 tester → 系统 → 审核管理 → 查询 →
  点击「通过」→ 弹窗确认 → 状态由「待审核」变为「已审核」（数据库 status 1 → 2）；
- 截图入库 `docs/images/admin.png`，README 界面预览扩展为 4 张（首页/播放/我的/审核）；
- CMake：Windows 下缺少 `libmpv-2.dll` 时给出明确报错和下载地址，避免链接期才暴露；
- 测试数据标题整理（第二轮转码验证 → 山间溪流，E2E-* → 街头随拍）；
- **上传流程 GUI 验证**：上传页选文件 → `uploadVideo` 成功 → 调 `ffmpeg/ffmpeg.exe` 截首帧并
  `uploadPhoto` 成功 → 填标题/简介/分类/标签 → 提交 `newVideo` → MQ 转码成功（数据库出现 status=1 的新视频）。

### 2026-10-18（第十轮：收尾与总览）

- 新增 `docs/08-project-summary.md`：一页纸项目总览（技术栈/架构/功能验证/质量证据/性能/亮点/限制/简历描述）；
- 新增 `scripts/check-doc-links.py`：文档相对链接检查（16 个 md，0 失效）；
- 用 Release 配置做一次**从零构建 + 安装 + ctest** 的最终验证，确认仓库可复现。

---

## 九、下一步（交接）

项目主体已完成（服务端 + 客户端 + 文档 + 测试 + 性能），剩余事项：

1. **Windows 实机验证**：用 Qt 6.7 构建 JianLiuPlayer（需 `mpv/dll/libmpv-2.dll` 与 `ffmpeg/ffmpeg.exe`），
   按 [07-client-integration.md](07-client-integration.md) 的 8 步走一遍；
2. **Docker 编排实机验证**：`scripts/package.sh` 收集依赖 → `docker compose -f deploy/docker-compose.yml up -d --build`；
3. 可选优化：
   - 列表接口批量查询用户信息、权限结果本地缓存（减少 Redis 往返）
   - RabbitMQ publisher confirm + 消费失败重试/死信
   - Prometheus 指标（brpc 自带 bvar）与链路追踪
   - 接入 Elasticsearch 恢复搜索（当前为降级状态）
4. 求职材料：按 [04-interview.md](04-interview.md) 准备讲述稿与问答，README 中已有 4 张运行截图。
