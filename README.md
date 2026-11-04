# 笺流（JianLiu）· 视频点播系统

> C++17 微服务化视频点播系统：**网关 + 用户 / 视频 / 文件 / 转码 5 个服务 + Qt6 桌面客户端**，
> 覆盖日志、RPC、服务注册发现、消息队列、缓存、ORM、搜索引擎、分布式文件存储、HLS 转码等中间件实践。
>
> 基础库命名统一为 `JianLiu*`（如 `JianLiuLog`、`JianLiuRpc`、`JianLiuScaffold`）。

<p align="center">
  <em>一个可以从头讲清楚、跑起来、写进简历的 C++ 微服务项目</em>
</p>

---

## 界面预览


| 首页（服务端真实数据）        | 播放页（HLS + 弹幕）              |
| ----------------------------- | --------------------------------- |
| ![首页](docs/images/home.png) | ![播放页](docs/images/player.png) |


| 我的（登录后）                   | 系统（管理员审核）             |
| -------------------------------- | ------------------------------ |
| ![我的](docs/images/profile.png) | ![审核](docs/images/admin.png) |

> 截图来自 Linux + Xvfb 下的实际运行：客户端拉取视频列表与封面，支持密码登录，
> 点击视频通过 m3u8 + ts 播放 HLS；管理员可在「系统 → 审核管理」里通过/驳回视频（截图即为通过操作后的状态）。

> 交互式流程图：[端到端业务流程](docs/diagrams/jianliu-vod-e2e.html)（Archify 生成，单文件离线可打开，支持深浅色与导出）。

---

## 架构总览

```text
 Qt6 客户端 (JianLiuPlayer)
        │  HTTP + protobuf
        ▼
 ┌──────────────┐
 │   gateway    │ :10000   统一入口，参数校验、会话鉴权、服务路由
 └──┬────┬───┬──┘
    │    │   │   brpc（服务发现：etcd）
    ▼    ▼   ▼
 ┌──────┐ ┌──────┐ ┌──────┐        ┌───────────┐
 │ user │ │video │ │ file │        │ transcode │
 │:10002│ │:10003│ │:10001│        │  (MQ 消费) │
 └──┬───┘ └──┬───┘ └──┬───┘        └─────┬─────┘
    │        │        │                  │
    ▼        ▼        ▼                  ▼
  MySQL   MySQL    FastDFS  ◄──────── FFmpeg HLS
  Redis   Redis    MySQL
  邮件     ES 搜索  Redis
           │
           └── RabbitMQ（延迟队列：缓存同步 / 删除文件 / 转码任务）
```

**核心链路**：上传视频 → 文件服务落 FastDFS → 视频服务落库并投递转码消息 → 转码服务拉取、切片、回传 →
客户端拉取 m3u8 播放。缓存采用「先写 Redis，延迟消息异步回写 MySQL」的策略。

---

## 技术栈


| 层次      | 技术                          | 用途                         |
| --------- | ----------------------------- | ---------------------------- |
| 语言/构建 | C++17、CMake 3.16+            | 服务端                       |
| RPC       | brpc + protobuf               | 服务间通信、HTTP 网关        |
| 注册中心  | etcd（租约 + watch）          | 服务注册与发现               |
| 消息队列  | RabbitMQ（DLX 延迟队列）      | 转码任务、缓存同步、文件删除 |
| 缓存      | Redis（redis++）              | 会话、视频信息、计数         |
| 数据库    | MySQL 8 + ODB 2.5             | 持久化、ORM                  |
| 搜索      | Elasticsearch（elasticlient） | 视频标题/简介检索            |
| 文件存储  | FastDFS                       | 视频、封面、切片存储         |
| 转码      | FFmpeg（libavformat）         | MP4 → HLS（m3u8 + ts）      |
| 日志      | spdlog                        | 同步/异步、轮转              |
| 客户端    | Qt 6.7 + libmpv               | 桌面播放器（Windows 构建）   |

---

## 目录结构

```text
.
├── CMakeLists.txt          # 顶层构建入口
├── scaffold/               # JianLiuScaffold 基础库（中间件客户端封装）
│   └── JianLiuScaffold/      # log util rpc etcd mq redis odb elastic fdfs hls mail
├── server/
│   ├── common/             # 错误码、缓存同步抽象
│   ├── data/               # ODB 实体 + DAO
│   ├── proto/              # 6 个 proto 定义
│   ├── svc_gateway/        # 网关
│   ├── svc_user/           # 用户服务
│   ├── svc_video/          # 视频服务
│   ├── svc_file/           # 文件服务
│   └── svc_transcode/      # 转码服务
├── client/
│   ├── JianLiuPlayer/          # Qt6 客户端
│   └── JianLiuMockServer/# 客户端联调用的 Mock 服务
├── deploy/                 # docker-compose / Dockerfile / 配置 / SQL
├── docs/                   # 架构、接口、部署、学习笔记、面试题
├── scripts/                # build.sh / fetch-ffmpeg.sh / 运行脚本
└── tools/                  # file_client 等联调工具
```

---

## 快速开始（服务端）

### 1. 依赖

Ubuntu 22.04 下需要：`g++ cmake make protobuf-compiler odb libodb-*-dev libbrpc-dev brpc libetcd-cpp-api-dev libamqpcpp-dev libev-dev libredis++-dev libhiredis-dev libspdlog-dev libfmt-dev libjsoncpp-dev libcurl4-openssl-dev libssl-dev libgflags-dev libleveldb-dev libmysqlclient-dev libelasticlient-dev libcpr-dev libwebsocketpp-dev libfastcommon-dev`

> 没有 root 权限时，FFmpeg 用 `scripts/fetch-ffmpeg.sh` 解到 `~/local/ffmpeg`，见脚本注释。

### 2. 构建

```bash
./scripts/build.sh                # 默认 Debug，产物在 build/ 与 build/bin
# 或手动：
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DJIANLIU_FFMPEG_ROOT=$HOME/local/ffmpeg/usr
cmake --build build -j$(nproc)
```

### 3. 配置与运行

配置通过 gflags 传入，`deploy/conf/global.conf` 是课程原版的 docker-compose 配置（`${XXX}` 由 `envsubst` 注入）。
本地联调直接用脚本：

```bash
./scripts/run-dev.sh start     # 启动 5 个服务（日志在 logs/）
./scripts/run-dev.sh status
./scripts/run-dev.sh stop
```

数据库初始化脚本见 [`deploy/sql/`](deploy/sql/)。

服务端口：网关 `10000`、文件 `10001`、用户 `10002`、视频 `10003`。

### 4. 验证

```bash
./scripts/smoke-test.sh      # 网关 + 会话 + 列表（4 项）
ctest --test-dir build       # 8 个单元测试
curl -s http://127.0.0.1:10000/status | head   # brpc 状态页

# 端到端（需要已登录会话；--admin 额外验证审核上架）
python3 scripts/e2e-test.py --session <sessionId> --admin

# 客户端接口一致性（静态检查，不依赖 Qt）
python3 scripts/check-client-api.py

# 文档相对链接检查
python3 scripts/check-doc-links.py

# 学习环境自检（中间件/服务/测试/文档，只读）
./scripts/learn-check.sh

# 文件服务 CLI（上传/下载）
./build/bin/file_client --session_id=<sid> --file=tools/file-client/jj.jpg --mime=image/jpeg
./build/bin/file_client --session_id=<sid> --download_id=<fileId> --output=/tmp/out.jpg
```

---

## 客户端

`client/JianLiuPlayer` 是 Qt 6.7 工程（Windows + MinGW 构建），依赖 libmpv 的 DLL：

```bash
# 在 Windows / Qt Creator 中打开 client/JianLiuPlayer/CMakeLists.txt
# 运行前把 config.ini 放到「当前工作目录」（模板见 client/JianLiuPlayer/config.ini.example）：
# [server]
# serverIp=192.168.131.128
# serverPort=10000
```

mpv 运行库（约 108 MB）不入库，下载后放到 `client/JianLiuPlayer/mpv/dll/`。

---

## 文档


| 文档                                                                     | 内容                                              |
| ------------------------------------------------------------------------ | ------------------------------------------------- |
| [docs/08-project-summary.md](docs/08-project-summary.md)                 | **项目一页纸总览（简历/面试用）**                 |
| [docs/learning/ai-study-plan.md](docs/learning/ai-study-plan.md)         | **6 周学习手册（环境/链路/难点/计划/面试/速查）** |
| [docs/09-runbook.md](docs/09-runbook.md)                                 | **本地运行手册（Linux VM 命令 + Windows Qt 操作）** |
| [docs/career/intern-plan.md](docs/career/intern-plan.md)                 | **双非实习冲刺方案（方向/24 周/面试/投递）**     |
| [docs/diagrams/jianliu-vod-e2e.html](docs/diagrams/jianliu-vod-e2e.html) | 端到端业务流程交互图（Archify）                   |
| [docs/ROADMAP.md](docs/ROADMAP.md)                                       | 长远目标、里程碑、学习路线、简历模板              |
| [docs/02-api.md](docs/02-api.md)                                         | 接口清单、错误码、curl 联调示例                   |
| [docs/01-architecture.md](docs/01-architecture.md)                       | 服务划分、代码分层、数据模型、关键流程            |
| [docs/03-deploy.md](docs/03-deploy.md)                                   | 部署手册（本地实测 + Docker）、常见问题           |
| [docs/04-interview.md](docs/04-interview.md)                             | 面试讲述稿、高频问答、简历模板                    |
| [docs/05-performance.md](docs/05-performance.md)                         | 压测方法、基线数据、优化清单                      |
| [docs/06-code-walkthrough.md](docs/06-code-walkthrough.md)               | 代码导读：阅读顺序、改动场景、调试技巧            |
| [docs/07-client-integration.md](docs/07-client-integration.md)           | 客户端联调：接口映射、构建、联调顺序              |
| [docs/notes/](docs/notes/README.md)                                      | 中间件学习笔记（Redis/MQ/RPC/ODB/HLS）            |

---

## 设计要点（面试可讲）

1. **为什么 brpc + etcd**：brpc 单机多线程 + bthread，吞吐高；etcd 租约保活 + watch 实现服务上下线实时感知。
2. **延迟队列做缓存同步**：Redis 先写、MQ 延迟消息异步刷 MySQL，兼顾性能与最终一致性；用 DLX + TTL 实现延迟。
3. **转码异步化**：上传接口只落库 + 投递消息，转码服务独立消费，避免长耗时阻塞网关。
4. **ODB 而非裸 SQL**：实体与表结构同源，编译期检查字段，多数据库可迁移；配合连接池使用。
5. **FastDFS**：视频/切片文件量大，用 Tracker/Storage 架构水平扩展，业务侧只存 file_id。

---

## 项目状态


| 阶段                                      | 状态                                                                |
| ----------------------------------------- | ------------------------------------------------------------------- |
| M0 源码盘点、目录重构                     | ✅                                                                  |
| M1 统一构建系统（scaffold + 5 服务）      | ✅                                                                  |
| M2 服务可运行（etcd 注册 + 网关响应）     | ✅                                                                  |
| M3 接口联调（登录/上传/转码/HLS/互动）    | ✅                                                                  |
| M4 客户端联调                             | ✅ Linux 下完整验证（登录/列表/封面/播放/弹幕）；Windows 实机待验证 |
| M5 业务闭环（服务端）                     | ✅（转码/HLS/点赞/弹幕/关注/审核）                                  |
| M6 测试与部署（gtest 8 项 + Docker 编排） | 🔄 测试完成，Docker 编排待实机验证                                  |
| M7 文档、学习笔记、面试材料               | ✅                                                                  |
| M8 性能优化与压测                         | ✅（列表 3s 缓存：290 → ~800 QPS，2.8x）                           |

详见 [docs/ROADMAP.md](docs/ROADMAP.md)。
