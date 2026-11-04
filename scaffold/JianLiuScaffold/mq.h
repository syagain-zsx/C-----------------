#pragma once
#include <amqpcpp.h>
#include <ev.h>
#include <amqpcpp/libev.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <deque>
#include <atomic>
#include <string>

namespace JianLiuMq {
    // RabbitMQ 常用交换机类型常量
    const std::string DIRECT = "direct";
    const std::string FANOUT = "fanout";
    const std::string TOPIC = "topic";
    const std::string HEADERS = "headers";
    const std::string DELAYED = "delayed";
    const std::string DLX_PREFIX = "dlx_";
    const std::string DEAD_LETTER_EXCHANGE = "x-dead-letter-exchange";
    const std::string DEAD_LETTER_BINDING_KEY = "x-dead-letter-routing-key";
    const std::string MESSAGE_TTL = "x-message-ttl";
    // 交换机、队列及绑定关系的声明配置
    struct declare_settings {
        std::string exchange;
        // 交换机类型： direct、fanout、topic、headers、delayed
        std::string exchange_type;
        std::string queue;
        std::string binding_key;
        size_t delayed_ttl = 0;

        // 生成死信交换机名称
        std::string dlx_exchange() const ;
        // 生成死信队列名称
        std::string dlx_queue() const;
        // 生成死信绑定键
        std::string dlx_binding_key() const ;
    };
    // 把字符串类型转换为 AMQP 交换机类型
    extern AMQP::ExchangeType exchange_type(const std::string &type);
    // 消息消费回调：消息体指针与长度
    using MessageCallback = std::function<void(const char*, size_t)>;
    // RabbitMQ 客户端：封装连接、声明、发布和消费
    class MQClient {
        public:
            using ptr = std::shared_ptr<MQClient>;
            MQClient(const std::string &url); // 构造成员，启动事件循环
            ~MQClient(); // 发送异步请求，结束事件循环，等待异步线程结束
            //声明交换机，声明队列，并绑定交换机和队列，如果是延时队列，则还要创建配套的死信交换机和队列
            //声明交换机和队列以及绑定成功后，需要等待，等待实际交换机和队列声明成功，再返回
            void declare(const declare_settings &settings); 
            // 向指定交换机和路由键发布消息
            bool publish(const std::string &exchange, const std::string &routing_key, const std::string &body);
            // 订阅指定队列并注册消息回调
            void consume(const std::string &queue, const MessageCallback &callback);
            // 启动事件循环（预留接口）
            void start();
            // 阻塞等待事件循环线程结束
            void wait();
        private:
            // libev 异步回调：安全地中断事件循环
            static void callback(struct ev_loop *loop, ev_async *watcher, int32_t revents);
            // 在事件循环线程内真正执行 publish，避免多线程直接操作同一 AMQP channel
            static void publish_callback(struct ev_loop *loop, ev_async *watcher, int32_t revents);
            void _drainPublish();
            // 声明常规交换机和队列，并进行绑定
            void _declared(const declare_settings &settings, AMQP::Table &args, bool is_dlx = false);
        private:
            struct PublishTask {
                std::string exchange;
                std::string routing_key;
                std::string body;
            };
            std::mutex _declared_mtx;
            std::mutex _mtx;
            std::condition_variable _cv;
            bool _declared_done = false;   // 声明完成标志（条件变量谓词）
            bool _consumed_done = false;   // 订阅完成标志
            std::atomic<bool> _degraded{false};  // 声明超时后降级，避免后续每次再等 30s
            std::atomic<bool> _closing{false};
            std::mutex _publish_mtx;
            std::deque<PublishTask> _publish_tasks;
            struct ev_loop *_ev_loop;
            struct ev_async _ev_async;
            struct ev_async _publish_async;
            AMQP::LibEvHandler _handler;
            AMQP::TcpConnection _connection;
            AMQP::TcpChannel _channel;
            std::thread _async_thread;
    };

    // 消息发布者：封装交换机/队列声明与发布
    class Publisher {
        public:
            using ptr = std::shared_ptr<Publisher>;
            //构造函数内，对成员进行初始化，并声明套件内的交换机和队列
            Publisher(const MQClient::ptr &mq_client, const declare_settings &settings);
            // 按配置的交换机和绑定键发布消息
            bool publish(const std::string &body);
        private:
            MQClient::ptr _mq_client;
            declare_settings _settings;
    };

    // 消息订阅者：封装队列声明与消费
    class Subscriber {
        public:
            using ptr = std::shared_ptr<Subscriber>;
            //构造函数内，对成员进行初始化，并声明套件内的交换机和队列
            Subscriber(const MQClient::ptr &mq_client, const declare_settings &settings);
            //订阅消息的时候，要注意：如果是延时队列，则实际订阅的是配套的死信队列
            void consume(MessageCallback &&cb);
        private:
            MQClient::ptr _mq_client;
            declare_settings _settings;
            MessageCallback _callback;
    };

    // 通用对象工厂
    class MQFactory {
        public:
            // 提供一个通用的工厂实现各个不同类的对象的创建
            template<typename R, typename... Args>
            static std::shared_ptr<R> create(Args&&... args) {
                return std::make_shared<R>(std::forward<Args>(args)...);
            }
    };
}