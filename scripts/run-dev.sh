#!/usr/bin/env bash
# ============================================================
# 本地联调：一键启动 / 停止 5 个服务
#
#   ./scripts/run-dev.sh start      # 启动全部（会先停掉旧的）
#   ./scripts/run-dev.sh start user # 只启动某个
#   ./scripts/run-dev.sh stop
#   ./scripts/run-dev.sh status
#
# 依赖的中间件默认使用 Docker 网络内的容器名：
#   dev-etcd / dev-mysql / dev-redis / dev-rabbitmq / dev-tracker
# 可用环境变量覆盖，例如：
#   MYSQL_HOST=127.0.0.1 REDIS_HOST=127.0.0.1 ./scripts/run-dev.sh start
#
# 资源相关（可用环境变量覆盖）：
#   LOG_LEVEL=1      调试日志（默认 2=info）
#   LOG_ASYNC=true   异步日志（默认 true，避免同步落盘拖慢请求）
#   DB_POOL_SIZE=3   MySQL/Redis 连接池大小
#   BTHREAD_CONCURRENCY=4  brpc 工作线程数（brpc 校验要求 >=4；2 vCPU 默认 4）
#   ES_URL=...       配置后才启用 Elasticsearch；留空表示搜索降级
# ============================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="${BIN_DIR:-$ROOT/build/bin}"
LOG_DIR="${LOG_DIR:-$ROOT/logs}"
mkdir -p "$LOG_DIR" "$ROOT/temp/transcode"

# FFmpeg 私有前缀（无 root 场景，见 scripts/fetch-ffmpeg.sh）
FFMPEG_LIB="${FFMPEG_LIB:-$HOME/local/ffmpeg/usr/lib/x86_64-linux-gnu}"
[[ -d "$FFMPEG_LIB" ]] && export LD_LIBRARY_PATH="$FFMPEG_LIB:${LD_LIBRARY_PATH:-}"

ETCD_ADDR="${ETCD_ADDR:-http://dev-etcd:2379}"
MYSQL_HOST="${MYSQL_HOST:-dev-mysql}"
REDIS_HOST="${REDIS_HOST:-dev-redis}"
MQ_URL="${MQ_URL:-amqp://admin:123456@dev-rabbitmq:5672/}"
ES_URL="${ES_URL:-}"
FDFS_TRACKER="${FDFS_TRACKER:-dev-tracker:22122}"
HOST_ADDR="${HOST_ADDR:-127.0.0.1}"
DB_POOL_SIZE="${DB_POOL_SIZE:-3}"
BTHREAD_CONCURRENCY="${BTHREAD_CONCURRENCY:-4}"

LOG_FLAGS=(
    "--log_level=${LOG_LEVEL:-2}"
    "--log_async=${LOG_ASYNC:-true}"
    "--log_format=[%H:%M:%S][%-7l]: %v"
    "--log_path=stdout"
)
BRPC_FLAGS=(
    "--bthread_concurrency=${BTHREAD_CONCURRENCY}"
)
MYSQL_FLAGS=(
    "--mysql_host=$MYSQL_HOST"
    "--mysql_port=3306"
    "--mysql_user=root"
    "--mysql_passwd=123456"
    "--mysql_db=vptest"
    "--mysql_connection_pool_size=$DB_POOL_SIZE"
)
REDIS_FLAGS=(
    "--redis_host=$REDIS_HOST"
    "--redis_port=6379"
    "--redis_user=default"
    "--redis_passwd=123456"
    "--redis_connection_pool_size=$DB_POOL_SIZE"
)
ETCD_FLAGS=("--registry_center_addr=$ETCD_ADDR")

DEL_FILE_Q='{"exchange": "delete_file_exchange","type": "direct","queue": "delete_file_queue","binding_key": "delete_file_queue"}'
DEL_CACHE_Q='{"exchange": "delete_cache_exchange","type": "delayed","queue": "delete_cache_queue","binding_key": "delete_cache_queue","delayed_ttl":3}'
SYNC_CACHE_Q='{"exchange": "cache_sync_to_db_exchange","type": "delayed","queue": "cache_sync_to_db_queue","binding_key": "cache_sync_to_db_queue","delayed_ttl":30}'
TRANSCODE_Q='{"exchange": "hls_transcode_exchange","type": "direct","queue": "hls_transcode_queue","binding_key": "hls_transcode_queue"}'

SERVICES=(file user video gateway transcode)
declare -A PORTS=([file]=10001 [user]=10002 [video]=10003 [gateway]=10000 [transcode]=0)

port_open() {
    timeout 1 bash -c "echo > /dev/tcp/127.0.0.1/$1" 2>/dev/null
}

stop_one() {
    local svc="$1"
    local pidfile="$LOG_DIR/$svc.pid"
    [[ -f "$pidfile" ]] || return 0
    local pid; pid="$(cat "$pidfile" 2>/dev/null || true)"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        echo "[run] 停止 $svc (pid=$pid)"
        kill "$pid" 2>/dev/null || true
        # 等进程真正退出，避免旧进程占端口导致新进程 Start 失败
        for _ in $(seq 1 50); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$pid" 2>/dev/null; then
            echo "[run] $svc 10s 未退出，强制结束"
            kill -9 "$pid" 2>/dev/null || true
        fi
    fi
    rm -f "$pidfile"
}

stop_all() {
    for svc in "${SERVICES[@]}"; do
        stop_one "$svc"
    done
}

wait_ready() {
    local svc="$1"
    local port="${PORTS[$svc]:-0}"
    local i
    for i in $(seq 1 100); do
        if [[ "$port" -gt 0 ]]; then
            port_open "$port" && return 0
        else
            # transcode 没有 HTTP 端口：以 MQ 订阅成功/降级日志作为就绪信号
            grep -qE "订阅队列消息成功|MQ 已降级|声明交换机/队列超时" "$LOG_DIR/$svc.log" 2>/dev/null && return 0
            kill -0 "$(cat "$LOG_DIR/$svc.pid" 2>/dev/null)" 2>/dev/null || return 1
        fi
        sleep 0.2
    done
    return 1
}

start_one() {
    local svc="$1"
    local bin="$BIN_DIR/${svc}_server"
    [[ -x "$bin" ]] || { echo "[run] 缺少可执行文件: $bin（先执行 scripts/build.sh）"; return 1; }
    stop_one "$svc"

    local -a args=("${LOG_FLAGS[@]}")
    case "$svc" in
        gateway)
            args+=("${BRPC_FLAGS[@]}" "${ETCD_FLAGS[@]}" "${MYSQL_FLAGS[@]}" "${REDIS_FLAGS[@]}"
                   "--listen_port=10000"
                   "--user_svc_name=user"
                   "--file_svc_name=file"
                   "--video_svc_name=video") ;;
        file)
            args+=("${BRPC_FLAGS[@]}" "${ETCD_FLAGS[@]}" "${MYSQL_FLAGS[@]}" "${REDIS_FLAGS[@]}"
                   "--listen_port=10001"
                   "--service_name=file" "--service_addr=$HOST_ADDR:10001"
                   "--mq_url=$MQ_URL"
                   "--delete_file_queue_info=$DEL_FILE_Q"
                   "--cache_del_queue_info=$DEL_CACHE_Q"
                   "--fdfs_tracker_host=$FDFS_TRACKER") ;;
        user)
            args+=("${BRPC_FLAGS[@]}" "${ETCD_FLAGS[@]}" "${MYSQL_FLAGS[@]}" "${REDIS_FLAGS[@]}"
                   "--listen_port=10002"
                   "--service_name=user" "--service_addr=$HOST_ADDR:10002"
                   "--mq_url=$MQ_URL"
                   "--delete_file_queue_info=$DEL_FILE_Q"
                   "--cache_del_queue_info=$DEL_CACHE_Q") ;;
        video)
            args+=("${BRPC_FLAGS[@]}" "${ETCD_FLAGS[@]}" "${MYSQL_FLAGS[@]}" "${REDIS_FLAGS[@]}"
                   "--listen_port=10003"
                   "--service_name=video" "--service_addr=$HOST_ADDR:10003"
                   "--mq_url=$MQ_URL"
                   "--delete_file_queue_info=$DEL_FILE_Q"
                   "--transcode_queue_info=$TRANSCODE_Q"
                   "--cache_sync_queue_info=$SYNC_CACHE_Q"
                   "--cache_del_queue_info=$DEL_CACHE_Q")
            if [[ -n "$ES_URL" ]]; then
                args+=("--es_url=$ES_URL")
            fi ;;
        transcode)
            # 转码服务只依赖 MySQL + MQ + FastDFS
            args+=("${MYSQL_FLAGS[@]}"
                   "--mq_url=$MQ_URL"
                   "--transcode_queue_info=$TRANSCODE_Q"
                   "--fdfs_tracker_host=$FDFS_TRACKER"
                   "--temp_dir=$ROOT/temp/transcode"
                   "--hls_time=10" "--hls_play_type=vod"
                   "--hls_base_url=/HttpService/downloadVideo/?fileId=") ;;
        *) echo "[run] 未知服务: $svc"; return 1 ;;
    esac

    echo "[run] 启动 $svc ..."
    nohup "$bin" "${args[@]}" > "$LOG_DIR/$svc.log" 2>&1 &
    echo $! > "$LOG_DIR/$svc.pid"

    if wait_ready "$svc"; then
        echo "[run] $svc 就绪"
    else
        echo "[run] 警告：$svc 未在 20s 内就绪，查看 $LOG_DIR/$svc.log"
        return 1
    fi
}

status_all() {
    for svc in "${SERVICES[@]}"; do
        local pidfile="$LOG_DIR/$svc.pid"
        local port="${PORTS[$svc]:-0}"
        if [[ -f "$pidfile" ]] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
            if [[ "$port" -gt 0 ]]; then
                if port_open "$port"; then
                    echo "  $svc: 运行中 (pid=$(cat "$pidfile"), port=$port)"
                else
                    echo "  $svc: 进程在但端口 $port 未监听"
                fi
            else
                echo "  $svc: 运行中 (pid=$(cat "$pidfile"))"
            fi
        else
            echo "  $svc: 已停止"
        fi
    done
}

case "${1:-start}" in
    start)
        if [[ -n "${2:-}" ]]; then
            start_one "$2"
        else
            stop_all
            sleep 0.5
            for s in "${SERVICES[@]}"; do
                start_one "$s" || true
            done
        fi
        status_all
        echo "[run] 日志目录: $LOG_DIR"
        ;;
    stop)   stop_all ;;
    status) status_all ;;
    *) echo "用法: $0 {start [服务]|stop|status}"; exit 1 ;;
esac
