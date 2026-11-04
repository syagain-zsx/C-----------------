// ============================================================
//  信道（Channel）流程 · 最小可运行示例
//
//  不改动任何现有业务代码，只用项目里真实的脚手架类把下面这条链路
//  从头到尾跑一遍并打印每一步：
//
//    1) 启动下游实例      brpc::Server::Start
//    2) 注册到 etcd       SvcProvider::registry()
//3) 订阅节点变更      SvcWatcher::watch()
//    4) 上线 -> 建信道    SvcChannels::addNode -> Channels::insert
//    5) 取信道(轮询)      SvcChannels::getNode -> Channels::select
//    6) 走信道发 RPC      DemoService_Stub(channel.get())->echo(...)
//    7) 下线 -> 摘信道    SvcChannels::delNode -> Channels::remove
//    8) 无节点            getNode 返回空 (网关里对应抛 SERVICE_NOT_FOUND)
// ============================================================
#include <JianLiuScaffold/rpc.h>
#include <JianLiuScaffold/etcd.h>
#include <JianLiuScaffold/log.h>

#include "demo.pb.h"

#include <brpc/channel.h>
#include <brpc/closure_guard.h>
#include <brpc/controller.h>
#include <brpc/server.h>

#include <etcd/Client.hpp>
#include <gflags/gflags.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

DEFINE_string(registry_center_addr, "http://dev-etcd:2379", "etcd 注册中心地址");
DEFINE_string(svc_name, "demo", "服务名, 注册路径为 /<svc_name>/<instance_id>");
DEFINE_int32(node_count, 2, "启动的下游服务实例个数");
DEFINE_int32(base_port, 19009, "第一个实例的端口");

namespace {

// 下游服务实现: 等价于项目里的 user/file/video 服务的 rpc 实现
class DemoServiceImpl : public demo::DemoService {
public:
    explicit DemoServiceImpl(std::string addr) : _addr(std::move(addr)) {}
    void echo(::google::protobuf::RpcController* controller,
              const ::demo::EchoReq* request,
              ::demo::EchoRsp* response,
              ::google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        response->set_msg(request->msg());
        response->set_server(_addr);   // 告诉调用方: 这次请求是谁处理的
    }
private:
    std::string _addr;
};

void step(const std::string &title) {
    std::cout << "\n---------- " << title << " ----------" << std::endl;
}

// 打印 etcd 里的注册表, 直观看到 key / value 的对应关系
void dump_registry(const std::string &svc_name, const std::string &reg_addr) {
    etcd::Client cli(reg_addr);
    auto resp = cli.ls("/" + svc_name).get();
    if (!resp.is_ok()) {
        std::cout << "  读取注册表失败: " << resp.error_message() << std::endl;
        return;
    }
    for (auto &v : resp.values()) {
        std::cout << "  etcd key=" << v.key() << "  value=" << v.as_string() << std::endl;
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    google::ParseCommandLineFlags(&argc, &argv, true);
    JianLiuLog::init();   // 脚手架里所有 DBG/INF 都依赖它

    // ============================================================
    // 阶段 1: 启动 N 个下游服务实例 (brpc 服务端)
    // ============================================================
    step("阶段1: 启动下游服务实例 (RpcServerFactory::create)");
    std::vector<std::shared_ptr<brpc::Server>> servers;
    std::vector<std::string> addrs;
    for (int i = 0; i < FLAGS_node_count; ++i) {
        int port = FLAGS_base_port + i;
        std::string addr = "127.0.0.1:" + std::to_string(port);
        // RpcServerFactory 内部: AddService(SERVER_OWNS_SERVICE) + Start(port)
        servers.push_back(JianLiuRpc::RpcServerFactory::create(port, new DemoServiceImpl(addr)));
        addrs.push_back(addr);
        std::cout << "  实例#" << (i + 1) << " 已监听 " << addr << std::endl;
    }

    // ============================================================
    // 阶段 2: 注册到 etcd -- SvcProvider::registry()
    // ============================================================
    step("阶段2: 注册到 etcd (SvcProvider::registry: lease + put + KeepAlive)");
    std::vector<std::unique_ptr<JianLiuSvc::SvcProvider>> providers;
    for (auto &addr : addrs) {
        auto provider = std::make_unique<JianLiuSvc::SvcProvider>(
            FLAGS_registry_center_addr, FLAGS_svc_name, addr);
        if (!provider->registry()) {
            std::cerr << "注册失败" << std::endl;
            return 1;
        }
        providers.push_back(std::move(provider));
    }
    dump_registry(FLAGS_svc_name, FLAGS_registry_center_addr);

    // ============================================================
    // 阶段 3: 服务发现 -- SvcWatcher::watch() 把事件喂给 SvcChannels
    // ============================================================
    step("阶段3: 服务发现 (SvcWatcher::watch -> SvcChannels::addNode -> 建信道)");
    JianLiuRpc::SvcChannels channels;
    channels.setWatch(FLAGS_svc_name);   // 先声明"我要关注 demo 这个服务"
    auto online_cb = [&channels](const std::string &svc, const std::string &addr, const std::string &key) {
        channels.addNode(svc, addr, key);
    };
    auto offline_cb = [&channels](const std::string &svc, const std::string &addr, const std::string &key) {
        (void)addr;
        channels.delNode(svc, key);
    };
    JianLiuSvc::SvcWatcher watcher(FLAGS_registry_center_addr,
                                   std::move(online_cb), std::move(offline_cb));
    watcher.watch();

    // 等 watch / ls 把节点灌进信道池
    JianLiuRpc::ChannelPtr channel;
    for (int i = 0; i < 100 && !channel; ++i) {
        channel = channels.getNode(FLAGS_svc_name);
        if (!channel) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    if (!channel) {
        std::cerr << "超时: 没有等到任何节点" << std::endl;
        return 1;
    }
    std::cout << "  信道池已就绪, 开始取信道" << std::endl;

    // ============================================================
    // 阶段 4: 轮询取信道 + 真正走信道发一次 RPC
    // ============================================================
    step("阶段4: getNode 取信道(轮询) + 走信道发 RPC");
    for (int i = 0; i < 4; ++i) {
        auto addr = channels.getNodeAddr(FLAGS_svc_name);
        std::cout << "  第" << (i + 1) << "次取信道 -> " << (addr ? *addr : "<无>") << std::endl;
    }

    demo::EchoReq req;
    req.set_msg("hello channel");
    demo::EchoRsp rsp;
    brpc::Controller cntl;
    demo::DemoService_Stub stub(channel.get());   // <-- 信道交给 stub
    stub.echo(&cntl, &req, &rsp, nullptr);         // <-- 异步接口, 这里同步等结果
    if (cntl.Failed()) {
        std::cerr << "RPC 失败: " << cntl.ErrorText() << std::endl;
        return 1;
    }
    std::cout << "  RPC 成功! 回显=" << rsp.msg()
              << "  实际处理节点=" << rsp.server() << std::endl;

    // ============================================================
    // 阶段 5: 节点下线 -- 摘掉信道
    //   真实场景: 进程挂了 -> 不再续租 -> 租约 10s 到期 -> etcd 自动删 key
    //            -> watcher 收到 DELETE -> delNode -> 摘信道
    //   这里为了秒级看到效果, 直接调 etcd 的 rm, 等价于租约到期那一刻
    // ============================================================
    step("阶段5: 节点下线 (etcd 删 key -> watcher DELETE -> delNode -> 摘信道)");
    {
        etcd::Client cli(FLAGS_registry_center_addr);
        auto resp = cli.ls("/" + FLAGS_svc_name).get();
        std::string victim_key;
        for (auto &v : resp.values()) {
            if (v.as_string() == addrs[0]) {
                victim_key = v.key();
                break;
            }
        }
        std::cout << "  删除注册键 " << victim_key << " (对应节点 " << addrs[0] << ")" << std::endl;
        providers[0].reset();          // 先停掉保活, 避免它把节点重新注册回来
        cli.rm(victim_key).get();
    }

    // 等下线事件被处理: 此时取 4 次信道, 应该全是同一个存活节点
    bool removed = false;
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::vector<std::string> picked;
        for (int k = 0; k < 4; ++k) {
            auto a = channels.getNodeAddr(FLAGS_svc_name);
            picked.push_back(a ? *a : "<无>");
        }
        bool same = true;
        for (auto &p : picked) {
            if (p != picked[0]) { same = false; break; }
        }
        if (same && picked[0] != addrs[0]) {
            std::cout << "  下线事件已生效, 第" << (i + 1) << "次轮询结果: ";
            for (auto &p : picked) { std::cout << p << " "; }
            std::cout << std::endl;
            removed = true;
            break;
        }
    }
    if (!removed) {
        std::cerr << "下线事件未生效" << std::endl;
    }

    // ============================================================
    // 阶段 6: 节点全部下线 -> 没有信道 (网关里对应抛 SERVICE_NOT_FOUND)
    // ============================================================
    step("阶段6: 所有节点下线 -> getNode 返回空");
    {
        etcd::Client cli(FLAGS_registry_center_addr);
        auto resp = cli.ls("/" + FLAGS_svc_name).get();
        for (auto &v : resp.values()) {
            std::cout << "  删除注册键 " << v.key() << std::endl;
            cli.rm(v.key()).get();
        }
    }
    providers.clear();
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto c = channels.getNode(FLAGS_svc_name);
        if (!c) {
            std::cout << "  已无任何可用信道 (网关此时会抛 SERVICE_NOT_FOUND)" << std::endl;
            break;
        }
    }

    std::cout << "\n===== 信道全流程演示结束 =====" << std::endl;
    return 0;
}