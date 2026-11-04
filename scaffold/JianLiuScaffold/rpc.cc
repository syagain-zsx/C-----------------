#include "rpc.h"
#include "log.h"

namespace JianLiuRpc {
    
// 构造轮询下标为 0 的节点集合
Channels::Channels() : _idx(0) {};
// 去重后为节点地址创建 brpc 信道并加入集合
void Channels::insert(const std::string &addr) {
    std::unique_lock<std::mutex> lock(_mtx);
    //0. 去重：watch 事件与全量拉取可能重复上报同一节点
    for (auto &item : _channels) {
        if (item.first == addr) {
            return;
        }
    }
    //1. 实例化channel对象
    auto channel = std::make_shared<brpc::Channel>();
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = 30000;
    channel->Init(addr.c_str(), &options);
    //3. 将对象添加到vector中
    _channels.push_back(std::make_pair(addr, channel));
}
// 按地址从集合中移除节点信道
void Channels::remove(const std::string &addr) {
    //1. 加锁
    std::unique_lock<std::mutex> lock(_mtx);
    //3. 从vector中移除channel
    for (auto vit = _channels.begin(); vit != _channels.end(); ++vit) {
        if (addr == vit->first) {
            _channels.erase(vit);
            break;
        }
    }
}
// 轮询返回下一个节点的 brpc 信道
ChannelPtr Channels::select() {
    //1. 加锁
    std::unique_lock<std::mutex> lock(_mtx);
    if (_channels.size() == 0) {
        return ChannelPtr();
    }
    //2. 获取下标索引
    size_t idx = (_idx++) % _channels.size();
    //3. 返回下标对应的节点信道
    return _channels[idx].second;
}
// 轮询返回下一个节点的服务地址
std::optional<std::string> Channels::selectAddr() {
    std::unique_lock<std::mutex> lock(_mtx);
    if (_channels.size() == 0) {
        return std::optional<std::string>();
    }
    //2. 获取下标索引
    size_t idx = (_idx++) % _channels.size();
    //3. 返回下标对应的节点地址
    return _channels[idx].first;
}


// 按服务名查找节点集合，不存在返回空
Channels::ptr SvcChannels::_Channels(const std::string &svc_name) {
    //限定作用域是为了只保护获取集合的过程，而不需要保护添加结果的过程
    std::unique_lock<std::mutex> lock(_mtx);
    auto it = _maps.find(svc_name);
    if (it == _maps.end()) {
        return Channels::ptr();
    }
    return it->second;
}
// 为指定服务初始化节点集合
void SvcChannels::setWatch(const std::string &svc_name) {
    // 针对指定服务，初始化构造集合，并添加管理
    std::unique_lock<std::mutex> lock(_mtx);
    auto channels = std::make_shared<Channels>();
    _maps.insert(std::make_pair(svc_name, channels));
}
// 记录实例键并维护服务地址对应的信道
void SvcChannels::addNode(const std::string &svc_name, const std::string &addr, const std::string &key) {
    std::unique_lock<std::mutex> lock(_mtx);
    auto &instances = _instances[svc_name];
    // 同一地址可能对应多个 etcd 实例键（重启后新实例还是同一地址），只要有一个就保留信道
    bool existed = false;
    for (auto &item : instances) {
        if (item.second == addr) {
            existed = true;
            break;
        }
    }
    instances[key] = addr;
    if (existed) {
        return;
    }
    auto it = _maps.find(svc_name);
    if (it == _maps.end()) {
        DBG("{} 有节点上线：{}, 未找到管理对象", svc_name, addr);
        return;
    }
    DBG("添加 {} 服务节点 {} 管理", svc_name, addr);
    it->second->insert(addr);
}
// 按实例键移除节点，地址无其他实例时摘除信道
void SvcChannels::delNode(const std::string &svc_name, const std::string &key) {
    std::unique_lock<std::mutex> lock(_mtx);
    auto sit = _instances.find(svc_name);
    if (sit == _instances.end()) {
        return;
    }
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) {
        return;
    }
    std::string addr = kit->second;
    sit->second.erase(kit);
    // 若该地址还有其它实例键（例如同地址的新实例），则不摘除信道
    for (auto &item : sit->second) {
        if (item.second == addr) {
            DBG("{} 节点 {} 仍有其它实例，保留信道", svc_name, addr);
            return;
        }
    }
    auto it = _maps.find(svc_name);
    if (it == _maps.end()) {
        return;
    }
    DBG("移除 {} 服务节点 {} 管理", svc_name, addr);
    it->second->remove(addr);
}
// 按服务名轮询获取节点信道
ChannelPtr SvcChannels::getNode(const std::string &svc_name) {
    // 加锁，查找集合，没找到则直接返回
    // 找到了：获取集合，通过集合，获取节点
    Channels::ptr channels = _Channels(svc_name);
    if (!channels) { return ChannelPtr(); }
    return channels->select();
}
// 按服务名轮询获取节点地址
std::optional<std::string> SvcChannels::getNodeAddr(const std::string &svc_name) {
    Channels::ptr channels = _Channels(svc_name);
    if (!channels) { return std::optional<std::string>(); }
    return channels->selectAddr();
}


// 把回调包装成 brpc Closure
google::protobuf::Closure* ClosureFactory::create(callback_t &&cb) {
    auto obj = std::make_shared<Object>();
    obj->callback = std::move(cb);
    return brpc::NewCallback(&ClosureFactory::asyncCallback, obj);
}
// 执行 Closure 中保存的回调
void ClosureFactory::asyncCallback(const Object::ptr obj) {
    obj->callback();
}


// 注册服务并启动 brpc 服务器
std::shared_ptr<brpc::Server> RpcServerFactory::create(int port, google::protobuf::Service *svc) {
    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;
    auto server = std::make_shared<brpc::Server>();
    int ret = server->AddService(svc, brpc::SERVER_OWNS_SERVICE);
    if (ret == -1) {
        ERR("添加RPC服务失败!");
        abort();
    }
    ret = server->Start(port, &options);
    if (ret == -1) {
        ERR("启动服务器失败!");
        abort();
    }
    return server;
}

}