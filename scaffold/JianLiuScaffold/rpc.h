#pragma once
#include <butil/logging.h>
#include <brpc/server.h>
#include <brpc/channel.h>
#include <mutex>
#include <optional>

namespace JianLiuRpc {
// brpc 信道智能指针
using ChannelPtr = std::shared_ptr<brpc::Channel>;

// 单个服务的节点集合
// 维护该服务下所有在线实例的地址与 brpc 信道，支持节点上下线与轮询选取
class Channels {
    public:
        //引用自身
        using ptr = std::shared_ptr<Channels>;
        // 构造空的节点集合
        Channels();
        //1. 新增节点：地址去重后创建信道并加入列表
        void insert(const std::string &addr);
        //2. 删除节点：按地址移除对应的信道
        void remove(const std::string &addr);
        //3. 获取节点信道：轮询取下一个实例的 brpc 信道
        ChannelPtr select();
        //4. 获取节点地址：轮询取下一个实例的服务地址
        std::optional<std::string> selectAddr();
    private:
        std::mutex _mtx;
        std::string _svc_name; // 服务名称
        uint32_t _idx; //轮询下标
        std::vector<std::pair<std::string, ChannelPtr>> _channels;
};

// 多服务的节点管理器
// 以服务名分组维护节点集合，并与 etcd 实例键绑定，支撑服务发现
class SvcChannels {
    public:
        using ptr = std::shared_ptr<SvcChannels>;
        SvcChannels() = default;
        // 初始化并注册指定服务的节点集合（开启监听前调用）
        void setWatch(const std::string &svc_name);
        // 新增节点：记录实例键与地址的映射，地址首次出现时建立信道
        // key 为 etcd 实例键（/svc/instance），用于区分同地址的新旧实例
        void addNode(const std::string &svc_name, const std::string &addr, const std::string &key);
        // 删除节点：按实例键移除，该地址无其它实例时摘除信道
        void delNode(const std::string &svc_name, const std::string &key);
        // 获取某服务的节点信道（轮询）
        ChannelPtr getNode(const std::string &svc_name);
        // 获取某服务的节点地址（轮询）
        std::optional<std::string> getNodeAddr(const std::string &svc_name);
    private:
        // 按服务名查找其节点集合，不存在则返回空指针
        Channels::ptr _Channels(const std::string &svc_name);
    private:
        std::mutex _mtx;
        std::unordered_map<std::string, Channels::ptr> _maps;
        // svc_name -> (etcd 实例键 -> 地址)
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _instances;
};

// 回调闭包工厂
// 把 std::function 包装成 brpc 可异步执行的 Closure
class ClosureFactory {
    public:
        // 异步执行的回调函数类型
        using callback_t = std::function<void()>;
        // 创建一个携带回调的 Closure，交由 brpc 异步执行
        static google::protobuf::Closure* create(callback_t &&cb);
    private:
        struct Object{
            using ptr = std::shared_ptr<Object>;
            callback_t callback;
        };
        // Closure 的实际执行体：取出并调用回调
        static void asyncCallback(const Object::ptr obj);
};

// RPC 服务端工厂
// 统一完成服务注册与端口监听，返回可复用的 brpc::Server
class RpcServerFactory {
    public:
        // 启动一个 RPC 服务：注册 svc 并监听 port
        // 默认svc是堆上new出来的对象，将管理权移交给rpc服务器进行管理
        static std::shared_ptr<brpc::Server> create(int port, google::protobuf::Service *svc);
};

}