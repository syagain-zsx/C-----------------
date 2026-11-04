# 笺流 · 本地运行手册（Linux VM + Windows Qt）

> 适用环境：VMware 里的 Ubuntu 22.04 虚拟机（`192.168.131.128`），项目跑在 VM 内的 Docker 容器（SSH 端口 `2222`），Windows 本机用 Qt Creator 运行客户端。
> 结论先说：**10000–10003 没有映射到 Windows**，所以 Windows 客户端不能直连 VM IP，必须先做一条 SSH 端口转发（本文 §2.5）。

## 0. 拓扑与端口现状

| 地址 | 含义 | Windows 能否直连 |
| --- | --- | --- |
| `192.168.131.128:2222` | SSH 进入容器（`/home/dev/workspace`） | ✅ |
| `192.168.131.128:22` | SSH 进入 VM 宿主机（可执行 docker 命令） | ✅ |
| `192.168.131.128:3306/6379/2379/5672` | MySQL / Redis / etcd / RabbitMQ | ✅ |
| `192.168.131.128:10000–10003` | gateway / file / user / video | ❌ **未映射** |

客户端只需要 gateway（10000）。两条路：

1. **SSH 端口转发（推荐，零改动）**：把本机 10000 转到容器 10000；
2. 在 VM 宿主机把容器端口发布出来（需要重建容器或加 `socat`，见 §2.5 备选）。

---

# 一、Linux VM（容器内）运行

## 1.1 登录

```bash
# Windows PowerShell / CMD
ssh -p 2222 dev@192.168.131.128
# 密码按你设置的那一个；登录后：
cd /home/dev/workspace
git log --oneline -1      # 期望 c32e6fa 或更新的提交
```

## 1.2 先查中间件（每次开工第一步）

```bash
cd /home/dev/workspace
for hp in dev-mysql:3306 dev-redis:6379 dev-etcd:2379 \
          dev-rabbitmq:5672 dev-tracker:22122 dev-storage:23000; do
  h=${hp%:*}; p=${hp#*:}
  timeout 2 bash -c "echo > /dev/tcp/$h/$p" 2>/dev/null \
    && echo "$hp open" || echo "$hp CLOSED"
done
curl -s http://dev-etcd:2379/health; echo
```

- `dev-storage:23000` 必须 open，否则上传/转码会失败（见 §1.9）。
- 如果 storage 关闭，去 **VM 宿主机**（不是容器）执行：
  ```bash
  ssh dev@192.168.131.128        # 22 端口
  docker ps -a | grep -E 'storage|tracker'
  docker start dev-storage
  ```
- ES（`dev-icsearch:9200`）可以不开：服务会降级为“搜索空结果”，不影响登录/列表/上传/播放。

## 1.3 构建

```bash
cd /home/dev/workspace

./scripts/build.sh                 # Debug，输出 build/bin/*_server
BUILD_TYPE=Release ./scripts/build.sh   # Release（性能对比时用）

# 只要改了一个服务也可以全量构建，脚本是增量的，通常 20–60s
```

产物：

```text
build/bin/gateway_server
build/bin/file_server
build/bin/user_server
build/bin/video_server
build/bin/transcode_server
build/bin/file_client
```

## 1.4 启动 / 停止 / 状态

```bash
./scripts/run-dev.sh start         # 停旧进程 → 依次启动 5 个服务 → 每个探活就绪
./scripts/run-dev.sh status        # 进程 + 端口
./scripts/run-dev.sh stop          # SIGTERM，等退出（10s 不退才 kill -9）

./scripts/run-dev.sh start video   # 只重启/启动某一个
```

启动成功的标志（5 行都带 port，transcode 无 port）：

```text
file: 运行中 (pid=..., port=10001)
user: 运行中 (pid=..., port=10002)
video: 运行中 (pid=..., port=10003)
gateway: 运行中 (pid=..., port=10000)
transcode: 运行中 (pid=...)
```

默认参数（见脚本头部）：
- 日志：`--log_async=true --log_level=2`（info）；
- 连接池：MySQL/Redis 各 3；
- brpc 工作线程：4；
- `ES_URL` 为空 → 搜索降级；
- 端口：gateway 10000、file 10001、user 10002、video 10003。

## 1.5 验证（从快到全）

```bash
./scripts/smoke-test.sh                 # 4 项：/status、tempLogin、sessionLogin、allVideoList
ctest --test-dir build                  # 8 个单测
python3 scripts/check-client-api.py     # 38/38 接口一致性
python3 scripts/check-doc-links.py      # 文档链接
```

端到端（需要 storage 在线）：

```bash
SID=$(curl -s -X POST http://127.0.0.1:10000/HttpService/tempLogin \
  -H 'Content-Type: application/json' -d '{"requestId":"R1"}' \
  | python3 -c 'import sys,json;print(json.load(sys.stdin)["result"]["sessionId"])')

curl -s -X POST http://127.0.0.1:10000/HttpService/passwdLogin \
  -H 'Content-Type: application/json' \
  -d "{\"requestId\":\"R2\",\"sessionId\":\"$SID\",\"username\":\"tester@example.com\",\"password\":\"TestPass123\"}"

python3 scripts/e2e-test.py --session "$SID" --admin    # 期望 18/18
```

压测：

```bash
python3 scripts/bench.py --concurrency 20 --total 500
```

## 1.6 日志与资源

```bash
tail -f logs/gateway.log logs/file.log logs/video.log logs/transcode.log
ls logs/*.pid
grep -c 'Resource deadlock avoided' logs/*.log   # 期望全 0

# 线程/RSS（2 vCPU 下正常值：每个 brpc 服务 18–19 线程，transcode 9）
for s in gateway file user video transcode; do
  p=$(cat logs/$s.pid)
  echo "$s threads=$(ls /proc/$p/task|wc -l) rss=$(awk '/VmRSS/{print $2}' /proc/$p/status)KB"
done
```

## 1.7 环境变量（临时改参数）

```bash
LOG_LEVEL=1 ./scripts/run-dev.sh start video          # 打开 debug 日志（排查用）
LOG_ASYNC=false ./scripts/run-dev.sh start gateway    # 临时同步日志
DB_POOL_SIZE=5 ./scripts/run-dev.sh start             # 连接池调大
BTHREAD_CONCURRENCY=8 ./scripts/run-dev.sh start      # brpc 线程调大（>=4）
ES_URL=http://elastic:123456@dev-icsearch:9200/ ./scripts/run-dev.sh start video
MYSQL_HOST=127.0.0.1 ./scripts/run-dev.sh start       # 中间件不在默认主机名时
```

## 1.8 停服与重启的注意点

- `stop` 会等进程真正退出并删 pidfile；**不要**直接 `pkill -f _server`，容易把别的服务一起带走。
- 只重启一个服务时，等 2–5s 让 etcd 重新注册；网关有 5s 全量同步兜底。
- 5 个服务全部重启后，第一次 `tempLogin` 可能遇到瞬时 `SERVICE_NOT_FOUND`，重试一次即可。

## 1.9 常见故障

| 现象 | 原因 / 处理 |
| --- | --- |
| 上传报 `fdfs_recv_response fail, result: 2` | `dev-storage:23000` 未启动。VM 宿主机 `docker start dev-storage`，确认端口 open。 |
| 日志 `Elasticsearch 连接失败...搜索降级` | ES 未启动，属预期；搜索返回空，其余正常。要启用就设 `ES_URL`。 |
| `SERVICE_NOT_FOUND`（28） | 目标服务没注册/刚重启。`run-dev.sh status` + etcd range + `logs/<svc>.log`。 |
| 启动报 `Fail to listen 0.0.0.0:1000x` | 端口被残留进程占用：`./scripts/run-dev.sh stop` 后重试。 |
| 日志 `MQ 降级运行` | RabbitMQ 不可用；服务仍启动但异步链路不可用。检查 `dev-rabbitmq:5672`。 |
| `ctest` 失败 | 先 `./scripts/build.sh`；测试依赖构建产物。 |
| 脚本 `Permission denied` | `chmod +x scripts/*.sh`。 |

---

# 二、Windows 本机 Qt 客户端

## 2.1 本机已经有的东西（已核对）

| 项 | 路径 |
| --- | --- |
| Qt Creator | `D:\Apps\Qt\Tools\QtCreator\bin\qtcreator.exe` |
| Qt 6.10.3 MinGW | `D:\Apps\Qt\6.10.3\mingw_64` |
| MinGW 13.1 | `D:\Apps\Qt\Tools\mingw1310_64` |
| 现有工程副本 | `C:\Users\Z1879\OneDrive\Desktop\preject_my\JianLiu-VOD\client\JianLiuPlayer` |
| libmpv | `...\JianLiuPlayer\mpv\dll\libmpv-2.dll`（已存在） |
| 已有构建目录 | `...\JianLiuPlayer\build\Desktop_Qt_6_10_3_MinGW_64_bit-Debug`（含 config.ini 与 ffmpeg） |

> `mpv/dll/` 被 `.gitignore` 忽略，所以从 GitHub 新克隆的代码**没有** `libmpv-2.dll`，需要从现有副本复制，或从 SourceForge 下载 libmpv-2.dll 放到 `mpv/dll/`。

## 2.2 代码从哪来

**方案 A（最快）**：直接用现有工程副本：
```text
C:\Users\Z1879\OneDrive\Desktop\preject_my\JianLiu-VOD\client\JianLiuPlayer
```

**方案 B（要最新代码）**：从 VM 拉一份（PowerShell，需输入 SSH 密码）：
```powershell
scp -P 2222 -r dev@192.168.131.128:/home/dev/workspace/client/JianLiuPlayer D:\JianLiuPlayer-latest
# 再把 libmpv-2.dll 从旧副本复制到新副本的 mpv\dll\
```

**方案 C**：`git clone https://github.com/syagain-zsx/cpp-microservice-scaffold-vod-system.git`，再补 `mpv/dll/libmpv-2.dll`。

## 2.3 Qt Creator 打开与构建

1. 启动 Qt Creator：
   ```powershell
   & 'D:\Apps\Qt\Tools\QtCreator\bin\qtcreator.exe'
   ```
2. 菜单 **File → Open File or Project…**，选择：
   ```text
   ...\client\JianLiuPlayer\CMakeLists.txt
   ```
3. 在 **Configure Project** 页选择 Kit：**Desktop Qt 6.10.3 MinGW 64-bit**，点 **Configure Project**。
   - 若没有这个 Kit：**Tools → Options → Kits**，Compiler 选 `D:\Apps\Qt\Tools\mingw1310_64\bin\g++.exe`，Qt Version 指向 `D:\Apps\Qt\6.10.3\mingw_64\bin\qmake.exe`。
4. 构建：**Build → Build Project**（或 `Ctrl+B`）。
   - CMake 会在配置阶段检查 `mpv/dll/libmpv-2.dll`，缺了会直接报 `FATAL_ERROR`；
   - 构建后会把 `libmpv-2.dll` 自动复制到 exe 同目录。
5. 产物：
   ```text
   ...\build\Desktop_Qt_6_10_3_MinGW_64_bit-Debug\JianLiuPlayer.exe
   ```

> 也可以用脚本构建（PowerShell）：
> ```powershell
> cd 'C:\Users\Z1879\OneDrive\Desktop\preject_my\JianLiu-VOD\client\JianLiuPlayer'
> .\build-windows.ps1 -QtPath 'D:/Apps/Qt/6.10.3/mingw_64' -MinGwPath 'D:/Apps/Qt/Tools/mingw1310_64/bin'
> ```

## 2.4 运行目录（很重要）

程序用 `QDir::currentPath()` 读 `config.ini`，并且封面截帧用 `当前目录/ffmpeg/ffmpeg.exe`。
所以 **Run 的工作目录必须是一个同时有 `config.ini` 和 `ffmpeg\ffmpeg.exe` 的目录**。

现成目录：
```text
...\JianLiuPlayer\build\Desktop_Qt_6_10_3_MinGW_64_bit-Debug
├─ JianLiuPlayer.exe
├─ config.ini
└─ ffmpeg\ffmpeg.exe
```

Qt Creator 设置：**Projects（左侧）→ Run → Working directory**，填上面的构建目录（或你自己的部署目录）。

`config.ini` 内容：
```ini
[server]
serverIp=127.0.0.1
serverPort=10000
```

## 2.5 连接 VM：SSH 端口转发（必须先做）

因为 10000 没有映射到 Windows，先在 PowerShell 里开一条隧道（**保持窗口不关**）：

```powershell
ssh -p 2222 -L 10000:127.0.0.1:10000 dev@192.168.131.128 -N
# 输入 SSH 密码；建议加心跳：
# ssh -p 2222 -o ServerAliveInterval=30 -L 10000:127.0.0.1:10000 dev@192.168.131.128 -N
```

验证（另开一个 PowerShell）：

```powershell
curl.exe http://127.0.0.1:10000/status
```

能返回 JSON 就说明隧道通了。

> 如果本机 10000 被占用：改用 `-L 18000:127.0.0.1:10000`，同时把 `config.ini` 的 `serverPort=18000`。

**备选（不推荐）**：在 VM 宿主机把容器端口发布出来。容器 IP 是 `172.18.0.5`，宿主机执行：
```bash
socat TCP-LISTEN:10000,fork TCP:172.18.0.5:10000
```
（需要宿主机装 socat；容器 IP 用 `docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' <容器名>` 查；或重建容器时加 `-p 10000:10000`。）这样 Windows 可直接用 `serverIp=192.168.131.128`。

## 2.6 启动客户端

1. 隧道窗口保持运行；
2. Qt Creator 里点 **Run**（`Ctrl+R`）；
3. 启动页会先 `tempLogin`，成功后进入首页；
4. 测试账号：
   - 普通用户 + 管理员：`tester@example.com` / `TestPass123`
   - 超管：`superadmin` / `123456`

## 2.7 打包成“双击可运行”（可选）

```powershell
$b='...\build\Desktop_Qt_6_10_3_MinGW_64_bit-Debug'
& 'D:\Apps\Qt\6.10.3\mingw_64\bin\windeployqt.exe' "$b\JianLiuPlayer.exe"
Copy-Item '...\mpv\dll\libmpv-2.dll' $b -Force
Copy-Item '...\ffmpeg' $b -Recurse -Force
# config.ini 放 $b 下
```

之后双击 `JianLiuPlayer.exe` 即可（仍需先开 SSH 隧道）。

## 2.8 客户端常见错误

| 现象 | 原因 / 处理 |
| --- | --- |
| 启动即报缺少 `libmpv-2.dll` | `mpv/dll/libmpv-2.dll` 缺失；从旧副本复制或下载后重新构建。 |
| 一直停在启动页 | 隧道没开/网关不可达；先 `curl.exe http://127.0.0.1:10000/status`，再看 `logs/gateway.log`。 |
| 提示“程序已在运行” | 上次崩溃残留共享内存；注销/重启，或任务管理器结束残留 `JianLiuPlayer.exe`。 |
| 上传封面失败/无封面 | 运行目录下没有 `ffmpeg\ffmpeg.exe`；把 ffmpeg 目录放到工作目录。 |
| 登录失败 | 用户不存在或密码错；先在 VM 用 `passwdLogin` curl 验证账号。 |
| 能登录但列表为空 | 视频都是待审核（status=1）；用管理员审核上架，或等 3s 缓存过期。 |
| `windeployqt` 后仍缺 DLL | 用 `D:\Apps\Qt\6.10.3\mingw_64\bin\windeployqt.exe`，不要用其他版本。 |

---

# 三、一页速查

```bash
# ===== VM 容器内 =====
ssh -p 2222 dev@192.168.131.128
cd /home/dev/workspace
./scripts/run-dev.sh start
./scripts/smoke-test.sh
tail -f logs/gateway.log
./scripts/run-dev.sh stop

# ===== Windows =====
# 1) 隧道
ssh -p 2222 -L 10000:127.0.0.1:10000 dev@192.168.131.128 -N
# 2) 验证
curl.exe http://127.0.0.1:10000/status
# 3) Qt Creator
& 'D:\Apps\Qt\Tools\QtCreator\bin\qtcreator.exe'
# 打开 ...\client\JianLiuPlayer\CMakeLists.txt → Kit 6.10.3 MinGW → Build → Run
# 工作目录 = build\Desktop_Qt_6_10_3_MinGW_64_bit-Debug
```

## 验收清单

- [ ] VM 内 `run-dev.sh status` 5 个服务都在；
- [ ] `smoke-test.sh` 4/4；
- [ ] `curl.exe http://127.0.0.1:10000/status` 在 Windows 返回 JSON；
- [ ] 客户端能完成 `tempLogin → 首页列表 → 播放`；
- [ ] 上传视频后 `logs/transcode.log` 出现“视频转码成功”；
- [ ] 管理员审核后首页可见（最多 3s 缓存延迟）。
