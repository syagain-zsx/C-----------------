#pragma once

// ---------------------------------------------------------------------------
// 这个头文件做了一件事：把 brpc 的“样板代码”收进 4 个小工具类里。
//
// 直接写 brpc 时，每个客户端都要重复：
//   建 Channel -> 填 ChannelOptions -> Init -> 选节点 -> 造 Closure -> 发请求
// 这些和业务无关的代码，就是所谓的“二次封装”要藏起来的部分。
//
// 先认识 4 个 brpc/protobuf 的基础名词，后面注释会反复出现：
//   Channel : 客户端到“某一个服务节点（ip:port）”的连接对象。
//   Service : 服务端实现具体接口的类，基类由 .proto 文件自动生成。
//   Stub    : 客户端调用代理，拿着 Channel 就能像调本地函数一样发 RPC。
//   Closure : protobuf 的回调接口。异步请求完成后，框架会调用它的 Run()。
// ---------------------------------------------------------------------------

#include <brpc/channel.h>
#include <brpc/server.h>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rpcwrap {
//rpc:romote procedure call 远程过程调用
// brpc::Channel 用 shared_ptr 管理：
// 多个线程/多次调用可以安全地共用一个 Channel，最后一个引用释放时才真正销毁。
using ChannelPtr = std::shared_ptr<brpc::Channel>;

// 一个服务的全部节点信道 + 轮询选路
//
// 举例：服务 "cal" 有 3 个节点 127.0.0.1:9000、9001、9002，
// 那么一个 Channels 对象里就存着 3 组 <地址, Channel>。
// 每次 select() 按顺序取下一个，这就是最简单的“轮询负载均衡”。
class Channels {
public:
    // 起个别名，外部就能用 Channels::ptr 来声明智能指针，少写点字。
    using ptr = std::shared_ptr<Channels>;

    // 新增一个节点：为这个地址创建 Channel 并尝试初始化。
    // 如果地址连不上，就直接不加入节点池（不会抛异常）。
    void insert(const std::string& addr);

    // 删除一个节点。服务下线、健康检查失败时调用。
    void remove(const std::string& addr);

    // 轮询选出一个可用 Channel；一个节点都没有时返回空指针。
    ChannelPtr select();

    // 轮询选出一个节点地址；一个节点都没有时返回 std::nullopt。
    // 和 select() 的区别只是返回“地址字符串”，方便打日志看这次发给了谁。
    std::optional<std::string> selectAddr();

private:
    std::mutex mtx_;  // 保护下面的 idx_ 和 channels_，多线程同时选节点也不会出错
    uint32_t idx_ = 0;  // 轮询下标：0,1,2,...,n-1,0,1,... 一直循环
    std::vector<std::pair<std::string, ChannelPtr>> channels_;  // (地址, 信道) 列表
};

// 服务名 -> Channels 的映射
//
// 一个进程里可能要调用很多个服务，所以再用一张表把所有“节点池”管起来：
//   maps_["cal"]  -> cal  服务的 Channels（里面是 cal 的所有节点）
//   maps_["user"] -> user 服务的 Channels
class SvcChannels {
public:
    // 预先声明“我要关注某个服务”，之后才能给它 addNode。
    // 作用相当于先占个位置，服务名写错了会在 addNode 时被忽略，方便排查。
    void setWatch(const std::string& svc);

    // 给某个服务增加节点。地址通常来自配置中心或服务发现（如 etcd）。
    void addNode(const std::string& svc, const std::string& addr);

    // 某个服务下线一个节点。
    void delNode(const std::string& svc, const std::string& addr);

    // 从某个服务的节点池里轮询取一个 Channel。
    ChannelPtr getNode(const std::string& svc);

    // 同上，但返回地址字符串，方便打日志。
    std::optional<std::string> getNodeAddr(const std::string& svc);

private:
    // 按服务名找到对应的 Channels；找不到返回空指针。
    // 注意：它只在函数内部短暂加锁，返回时锁已经释放，不会拖住后面的操作。
    Channels::ptr channelsOf(const std::string& svc);

    std::mutex mtx_;                                        // 保护 maps_
    std::unordered_map<std::string, Channels::ptr> maps_;   // 服务名 -> 节点池
};

// lambda -> google::protobuf::Closure
//
// brpc 的异步接口只认 google::protobuf::Closure*，
// 但业务同学更想直接写 lambda。这个小工厂负责把两者接起来：
//     auto* done = ClosureFactory::create([]{ ... });
//     stub.Add(cntl, req, rsp, done);
// 请求结束后，brpc 会调用 done->Run()，内部再执行你写的 lambda。
class ClosureFactory {
public:
    using callback_t = std::function<void()>;   // 任意“无参数、无返回值”的可调用对象

    // 创建 Closure。
    // 注意：返回的对象由 brpc 在 Run() 之后自动 delete，
    //       调用方千万不要自己 delete，否则会二次释放导致崩溃。
    static google::protobuf::Closure* create(callback_t cb);

private:
    // 真正被 Closure 持有的是这个对象：把 lambda 存进去，保证回调执行前它一直活着。
    struct Object {
        callback_t callback;
    };
    // brpc 最终调用的静态函数，参数是上面 Object 的 shared_ptr。
    static void run(std::shared_ptr<Object> obj);
};

// 一行创建并启动 brpc Server
//
// 原本起一个服务要：new Server -> AddService -> 填 ServerOptions -> Start，
// 这里合并成一次调用。内部出错会打印原因并 abort()，适合示例和小工具。
class RpcServerFactory {
public:
    // port：监听端口；svc：在堆上 new 出来的服务对象。
    // 内部使用 SERVER_OWNS_SERVICE，服务对象由 Server 负责 delete，调用方不要自己删。
    static std::shared_ptr<brpc::Server> create(int port,
                                                google::protobuf::Service* svc);
};

}  // namespace rpcwrap
