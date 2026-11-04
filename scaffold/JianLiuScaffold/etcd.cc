#include "etcd.h"
#include "log.h"
#include "util.h"
#include <thread>
#include <sstream>
#include <chrono>
#include <vector>

namespace JianLiuSvc {

// 阻塞等待 etcd 客户端连接成功；带超时，避免 etcd 不可用时启动永久挂起
bool wait_for_connection(etcd::Client &client, int timeout_sec) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    int attempts = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            if (client.head().get().is_ok()) {
                return true;
            }
        } catch (const std::exception &e) {
            // 连接尚未就绪：继续重试
        }
        ++attempts;
        if (attempts % 5 == 0) {
            WRN("连接etcd服务器失败，重试中(第{}次)...", attempts);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    ERR("连接etcd服务器超时({}s)，服务将以未注册状态继续运行", timeout_sec);
    return false;
}

// 保存注册信息并生成随机实例 ID
SvcProvider::SvcProvider(const std::string &reg_center_addr,
    const std::string &svc_name, const std::string &svc_addr)
    : _reg_center_addr(reg_center_addr)
    , _instance_id(JianLiuUtil::Random::code())
    , _svc_name(svc_name)
    , _svc_addr(svc_addr) {}

SvcProvider::~SvcProvider() {
    _stopped = true;
    std::lock_guard<std::mutex> lock(_keepalive_mtx);
    _keepalive.reset();
}

// 生成 etcd 实例键：/服务名/实例ID
std::string SvcProvider::make_key() {
    std::stringstream ss;
    ss << "/" << _svc_name << "/" << _instance_id;
    return ss.str();
}

// bool ret = registry("user", "192.168.65.130:9000");
// -> key = /user/instance_id;  val = 192.168.65.130:9000
// 创建租约、写入服务地址并启动保活
bool SvcProvider::registry() {
    if (_stopped) return false;
    //1. 实例化etcd客户端对象（短连接：注册完即可释放）
    etcd::Client client(_reg_center_addr);
    if (!wait_for_connection(client, 10)) {
        return false;
    }
    //2. 创建10s租约（3s 在负载高时容易过期，导致服务在注册中心反复上下线）
    auto lease_resp = client.leasegrant(10).get();
    if (lease_resp.is_ok() == false) {
        ERR("创建租约失败: {}", lease_resp.error_message());
        return false;
    }
    auto lease_id = lease_resp.value().lease();
    //3. 重组key值, 根据租约向服务器添加数据
    auto resp = client.put(make_key(), _svc_addr, lease_id).get();
    if (resp.is_ok() == false) {
        ERR("添加数据失败: {}", resp.error_message());
        return false;
    }
    DBG("服务注册成功: {} = {}", make_key(), _svc_addr);
    //4. 保活失败时「延迟重注册」：不能在 KeepAlive 自己的回调线程里 reset 自己，
    //   否则析构会 join 当前线程 -> std::system_error("Resource deadlock avoided") -> terminate。
    auto handler = [this](const std::exception_ptr &eptr) {
        WRN("etcd 租约保活失败，准备重新注册服务: {}", _svc_name);
        if (_stopped || _reregistering.exchange(true)) {
            return;
        }
        std::thread([this]() {
            for (int i = 0; i < 5 && !_stopped; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (this->registry()) {
                    INF("etcd 重新注册成功: {}", _svc_name);
                    break;
                }
            }
            _reregistering = false;
        }).detach();
    };
    std::lock_guard<std::mutex> lock(_keepalive_mtx);
    if (_stopped) return false;
    _keepalive.reset(new etcd::KeepAlive(_reg_center_addr, handler, 10, lease_id));
    return true;
}

// 保存注册中心地址与上下线回调
SvcWatcher::SvcWatcher(const std::string &reg_center_addr,
    SvcWatcher::ModCallbck &&online_callback,
    SvcWatcher::ModCallbck &&offline_callback)
    : _reg_center_addr(reg_center_addr)
    , _online_callback(std::move(online_callback))
    , _offline_callback(std::move(offline_callback)){ }

// 从 etcd 实例键中解析服务名
std::string SvcWatcher::parse_key(const std::string &key) {
    // /user/instance_id
    std::vector<std::string> arry;
    JianLiuUtil::STR::split(key, "/", arry);
    return arry.empty() ? std::string() : arry[0];
}

void SvcWatcher::addKnownKey(const std::string &key) {
    std::lock_guard<std::mutex> lock(_keys_mtx);
    _known_keys.insert(key);
}

void SvcWatcher::removeKnownKey(const std::string &key) {
    std::lock_guard<std::mutex> lock(_keys_mtx);
    _known_keys.erase(key);
}

// 处理 watch 事件并分发上线/下线回调
void SvcWatcher::callback(const etcd::Response &resp) {
    if (resp.is_ok() == false) {
        ERR("监控出错: {}", resp.error_message());
        return;
    }
    const auto &events = resp.events();
    for (auto &e : events) {
        if (e.event_type() == etcd::Event::EventType::PUT) {
            std::string svc_name = parse_key(e.kv().key());
            std::string svc_addr = e.kv().as_string();
            DBG(" {} 服务上线节点: {}", svc_name, svc_addr);
            addKnownKey(e.kv().key());
            if (_online_callback) _online_callback(svc_name, svc_addr, e.kv().key());
        }else if (e.event_type() == etcd::Event::EventType::DELETE_) {
            std::string svc_name = parse_key(e.prev_kv().key());
            std::string svc_addr = e.prev_kv().as_string();
            DBG(" {} 服务下线节点: {}", svc_name, svc_addr);
            removeKnownKey(e.prev_kv().key());
            if (_offline_callback) _offline_callback(svc_name, svc_addr, e.prev_kv().key());
        }else {
            WRN("无效通知类型");
        }
    }
}

// watch 断开后延迟重连：不在 Watcher 回调线程内重建 watcher（库不允许）
void SvcWatcher::scheduleReconnect() {
    if (_destroying || _reconnecting.exchange(true)) {
        return;
    }
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        _reconnecting = false;
        if (!_destroying) {
            this->watch();
        }
    }).detach();
}

// 建立递归监听并拉取当前全量服务节点
bool SvcWatcher::watch() {
    std::lock_guard<std::mutex> watch_lock(_watch_mtx);
    if (_destroying) return false;
    //1. 复用 etcd 客户端，并等待连接成功
    if (!_client) {
        _client = std::make_shared<etcd::Client>(_reg_center_addr);
    }
    if (!wait_for_connection(*_client, 10)) {
        WRN("etcd 不可用，watch 稍后重试");
        scheduleReconnect();
        return false;
    }
    //2. 先建立 watch，再拉取全量：避免「ls 之后、watch 建立之前」注册的服务丢失
    auto cb = std::bind(&SvcWatcher::callback, this, std::placeholders::_1);
    _watcher.reset(new etcd::Watcher(_reg_center_addr, "/", cb, true));
    _watcher->Wait([this](bool cond) {
        if (cond == true || _destroying.load()) { return; }
        WRN("etcd watch 断开，1s 后重连");
        scheduleReconnect();
    });
    //3. 浏览根目录，获取当前所有已经提供服务的节点信息（与 watch 事件重复的部分由 addNode 去重）
    auto resp = _client->ls("/").get();
    if (resp.is_ok() == true) {
        auto values = resp.values();
        for (auto &v : values) {
            std::string svc_name = parse_key(v.key());
            std::string svc_addr = v.as_string();
            addKnownKey(v.key());
            DBG("已有 {} 节点: {}", svc_name, svc_addr);
            if (_online_callback) _online_callback(svc_name, svc_addr, v.key());
        }
    } else {
        WRN("拉取服务列表失败: {}", resp.error_message());
    }
    //4. 周期性全量同步：修复链路抖动漏掉的事件，以及「同地址的新旧实例」下线误删
    if (!_resync_thread.joinable()) {
        _resync_thread = std::thread([this]() {
            while (true) {
                {
                    std::unique_lock<std::mutex> lock(_resync_mtx);
                    if (_resync_cv.wait_for(lock, std::chrono::seconds(5),
                                            [this]() { return _resync_stop.load(); })) {
                        break;
                    }
                }
                this->resync();
            }
        });
    }
    return true;
}

// 周期性拉取全量服务节点：上线补齐 + 下线删除（只加不删会残留死节点）
void SvcWatcher::resync() {
    try {
        std::shared_ptr<etcd::Client> client;
        {
            std::lock_guard<std::mutex> lock(_watch_mtx);
            if (_destroying) return;
            if (!_client) {
                _client = std::make_shared<etcd::Client>(_reg_center_addr);
            }
            client = _client;
        }
        auto resp = client->ls("/").get();
        if (resp.is_ok() == false) {
            WRN("周期性同步服务列表失败: {}", resp.error_message());
            return;
        }
        std::set<std::string> current;
        for (auto &v : resp.values()) {
            current.insert(v.key());
            addKnownKey(v.key());
            if (_online_callback) {
                _online_callback(parse_key(v.key()), v.as_string(), v.key());
            }
        }
        std::vector<std::string> removed;
        {
            std::lock_guard<std::mutex> lock(_keys_mtx);
            for (const auto &key : _known_keys) {
                if (current.find(key) == current.end()) {
                    removed.push_back(key);
                }
            }
            _known_keys = current;
        }
        for (const auto &key : removed) {
            WRN("周期性同步清理失效节点: {}", key);
            if (_offline_callback) {
                _offline_callback(parse_key(key), std::string(), key);
            }
        }
    } catch (const std::exception &e) {
        WRN("周期性同步服务列表异常: {}", e.what());
    }
}

// 停止 watch 和同步线程，安全释放资源
SvcWatcher::~SvcWatcher() {
    _destroying = true;
    if (_watcher) {
        _watcher->Cancel();
    }
    {
        std::lock_guard<std::mutex> lock(_resync_mtx);
        _resync_stop = true;
    }
    _resync_cv.notify_all();
    if (_resync_thread.joinable()) {
        _resync_thread.join();
    }
    _watcher.reset();
    _client.reset();
}

}
