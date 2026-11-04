#pragma once
#include <etcd/Client.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/Watcher.hpp>
#include <etcd/Response.hpp>
#include <etcd/Value.hpp>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <set>
#include <memory>

namespace JianLiuSvc {
    
    // 阻塞等待 etcd 客户端连接成功；带超时，避免 etcd 不可用时启动永久挂起
    extern bool wait_for_connection(etcd::Client &client, int timeout_sec = 10);
    // 服务注册器：向 etcd 注册服务地址并通过租约保活
    class SvcProvider {
        public:
            using ptr = std::shared_ptr<SvcProvider>;
            // 保存注册中心地址、服务名与服务地址，并生成实例 ID
            SvcProvider(const std::string &reg_center_addr, const std::string &svc_name, const std::string &svc_addr);
            // bool ret = registry("user", "192.168.65.130:9000");
            // -> key = /user/instance_id;  val = 192.168.65.130:9000
            // 向 etcd 注册服务并启动租约保活
            bool registry();
            ~SvcProvider();
        private:
            // 生成 etcd 中的实例键：/服务名/实例ID
            std::string make_key();
        private:
            std::string _reg_center_addr; //注册中心地址
            std::string _instance_id; //当前节点的标识
            std::string _svc_name;  // 节点能够提供的服务名称
            std::string _svc_addr;  // 节点地址
            std::shared_ptr<etcd::KeepAlive> _keepalive; //租约保活对象
            std::mutex _keepalive_mtx;                   // 保护 _keepalive 的替换
            std::atomic<bool> _reregistering{false};     // 防止重复重注册
            std::atomic<bool> _stopped{false};           // 析构中，停止重注册
    };
    // 服务发现监听器：监听 etcd 中服务节点的上线与下线
    class SvcWatcher {
        public:
            using ptr = std::shared_ptr<SvcWatcher>;
            //回调函数，在产生事件时传入：1-服务名称，2-节点地址，3-etcd 实例键（/svc/instance）
            // 服务节点变更回调：服务名、节点地址、etcd 实例键
            using ModCallbck = std::function<void(const std::string &, const std::string &, const std::string &)>;
            // 保存注册中心地址及上线/下线回调
            SvcWatcher(const std::string &reg_center_addr, ModCallbck &&online_callback, ModCallbck &&offline_callback);
            // 停止监听、结束同步线程并释放资源
            ~SvcWatcher();
            // 数据监控并不针对单独数据，而是针对 / 目录
            // 建立递归监听并拉取当前全量服务节点
            bool watch();
        private:
            // 处理 etcd watch 事件并触发上线/下线回调
            void callback(const etcd::Response &resp);
            void resync();   // 周期性地重新拉取全量节点，修复漏掉的/误删的事件
            void scheduleReconnect(); // watch 断开后延迟重连（不在回调线程内递归）
            void addKnownKey(const std::string &key);
            void removeKnownKey(const std::string &key);
            std::string parse_key(const std::string &key);// 从key中获取服务名称
        private:
            std::string _reg_center_addr; //注册中心地址
            ModCallbck _online_callback;  //服务上线回调函数
            ModCallbck _offline_callback; //服务下线回调函数
            std::shared_ptr<etcd::Watcher> _watcher; // 服务上下线监控对象
            std::thread _resync_thread;   // 周期性全量同步线程
            std::atomic<bool> _resync_stop{false};
            std::atomic<bool> _destroying{false};
            std::mutex _resync_mtx;
            std::condition_variable _resync_cv;
            std::shared_ptr<etcd::Client> _client;   // 复用 etcd 客户端，避免 resync 每 5s 新建 gRPC 连接
            std::mutex _watch_mtx;                   // watch/resync 串行化
            std::atomic<bool> _reconnecting{false};  // 防止重连风暴
            std::mutex _keys_mtx;                    // 保护已知节点集合
            std::set<std::string> _known_keys;       // 已上报的 etcd 实例键
    };
}