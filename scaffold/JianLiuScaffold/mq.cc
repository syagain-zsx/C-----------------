#include "mq.h"
#include "log.h"
#include <chrono>

namespace JianLiuMq {

    // 生成死信交换机名称
    std::string declare_settings::dlx_exchange() const {
        return DLX_PREFIX + exchange;
    }
    // 生成死信队列名称
    std::string declare_settings::dlx_queue() const {
        return DLX_PREFIX + queue;
    }
    // 生成死信绑定键
    std::string declare_settings::dlx_binding_key() const {
        return DLX_PREFIX + binding_key;
    }

    // 把字符串类型转换为 AMQP 交换机类型
    AMQP::ExchangeType exchange_type(const std::string &type) {
        if (type == DIRECT) {
            return AMQP::ExchangeType::direct;
        }else if (type == FANOUT) {
            return AMQP::ExchangeType::fanout;
        }else if (type == TOPIC) {
            return AMQP::ExchangeType::topic;
        }else if (type == HEADERS) {
            return AMQP::ExchangeType::headers;
        }else if (type == DELAYED) {
            return AMQP::ExchangeType::direct;
        }
        ERR("交换机类型错误: {}", type);
        abort();
    }
    // 构造成员，启动事件循环
    MQClient::MQClient(const std::string &url)
        : _ev_loop(EV_DEFAULT)
        , _handler(_ev_loop)
        , _connection(&_handler, AMQP::Address(url))
        , _channel(&_connection) {
        // 发布请求统一投递到事件循环线程执行，避免 brpc 多线程并发操作同一 AMQP channel
        ev_async_init(&_publish_async, MQClient::publish_callback);
        _publish_async.data = this;
        ev_async_start(_ev_loop, &_publish_async);
        _async_thread = std::thread([this](){ ev_run(_ev_loop); });
    }
    // 发送异步请求，结束事件循环，等待异步线程结束
    MQClient::~MQClient() {
        _closing = true;
        if (!_async_thread.joinable()) {
            return;
        }
        // 如果析构发生在事件循环线程内部（例如回调里释放了最后一个引用），
        // join 自己会抛 "Resource deadlock avoided"，这里直接 detach
        if (std::this_thread::get_id() == _async_thread.get_id()) {
            _async_thread.detach();
            return;
        }
        ev_async_init(&_ev_async, MQClient::callback);
        ev_async_start(_ev_loop, &_ev_async);
        ev_async_send(_ev_loop, &_ev_async);
        _async_thread.join();// 等待异步线程结束
    }
    // libev 异步回调：安全地中断事件循环
    void MQClient::callback(struct ev_loop *loop, ev_async *watcher, int32_t revents) {
        //这个接口不能跨线程调用
        ev_break(loop, EVBREAK_ALL);
    }
    //声明交换机，声明队列，并绑定交换机和队列，如果是延时队列，则还要创建配套的死信交换机和队列
    //声明交换机和队列以及绑定成功后，需要等待，等待实际交换机和队列声明成功，再返回
    void MQClient::_declared(const declare_settings &settings, AMQP::Table &args, bool is_dlx) {
        if (_degraded) {
            WRN("MQ 已降级，跳过声明: {}", settings.queue);
            return;
        }
        std::unique_lock<std::mutex> declared_lock(_declared_mtx); //这个锁用于互斥
        {
            std::lock_guard<std::mutex> init_lock(_mtx);
            _declared_done = false;
        }
        //1. 根据参数is_dlx，判断是否是死信队列，取出要使用的的交换机名称，队列名称，绑定关键字
        std::string exchange, queue, binding_key;
        if (is_dlx) {
            exchange = settings.dlx_exchange();
            queue = settings.dlx_queue();
            binding_key = settings.dlx_binding_key();
        }else {
            exchange = settings.exchange;
            queue = settings.queue;
            binding_key = settings.binding_key;
        }
        AMQP::ExchangeType type = exchange_type(settings.exchange_type);
        // 声明交换机，声明队列，并绑定交换机和队列
        auto finish = [this]() {
            {
                std::lock_guard<std::mutex> notify_lock(_mtx);
                _declared_done = true;
            }
            _cv.notify_all();
        };
        _channel.declareExchange(exchange, type)
            .onSuccess([=, this](){
                _channel.declareQueue(queue, args)
                   .onSuccess([=, this](const std::string &name,
                        uint32_t messagecount,
                        uint32_t consumercount){
                        _channel.bindQueue(exchange, queue, binding_key)
                           .onSuccess([=, this](){
                                // 声明成功，通知外部
                                finish();
                            })
                           .onError([=, this](const char *message) {
                               ERR("绑定队列失败: {}-{} - {}", exchange, queue, message);
                               finish();
                           });
                    })
                    .onError([=, this](const char *message) {
                        ERR("声明队列失败: {} - {}", queue, message);
                        finish();
                    });
            })
           .onError([=, this](const char *message) {
                ERR("声明交换机失败: {} - {}", exchange, message);
                finish();
            });
        std::unique_lock<std::mutex> lock(_mtx);
        if (_cv.wait_for(lock, std::chrono::seconds(3), [this]() { return _declared_done; }) == false) {
            // 不再每个队列都等 30s：第一次超时后整体降级，避免 MQ 异常时启动被拖到几分钟
            _degraded = true;
            ERR("声明交换机/队列超时(3s)，MQ 降级运行（后续发布/订阅跳过）: {} / {}", exchange, queue);
            return;
        }
    }
    // 声明交换机、队列及绑定关系
    void MQClient::declare(const declare_settings &settings) {        //1. 判断当前是否是延时队列
        AMQP::Table args;
        if (settings.exchange_type == DELAYED) {
            //声明死信交换机和队列，并进行绑定
            _declared(settings, args, true);
            // 声明常规交换机和队列，并进行绑定
            args[DEAD_LETTER_EXCHANGE] = settings.dlx_exchange();
            args[DEAD_LETTER_BINDING_KEY] = settings.dlx_binding_key();
            args[MESSAGE_TTL] = settings.delayed_ttl; //过期时间5s
            
        }
        _declared(settings, args, false);
    }
    // 向指定交换机和路由键发布消息：入队后由事件循环线程真正 publish
    bool MQClient::publish(const std::string &exchange, const std::string &routing_key, const std::string &body) {
        if (_closing || _degraded) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(_publish_mtx);
            if (_publish_tasks.size() >= 10000) {
                ERR("MQ 发布队列积压(>=10000)，丢弃消息: {} / {}", exchange, routing_key);
                return false;
            }
            _publish_tasks.push_back(PublishTask{exchange, routing_key, body});
        }
        ev_async_send(_ev_loop, &_publish_async);
        return true;
    }

    void MQClient::publish_callback(struct ev_loop *, struct ev_async *watcher, int32_t) {
        auto *self = static_cast<MQClient *>(watcher->data);
        if (self) self->_drainPublish();
    }

    void MQClient::_drainPublish() {
        std::deque<PublishTask> tasks;
        {
            std::lock_guard<std::mutex> lock(_publish_mtx);
            tasks.swap(_publish_tasks);
        }
        for (auto &task : tasks) {
            if (!_channel.publish(task.exchange, task.routing_key, task.body)) {
                ERR("发布消息失败: {} / {}", task.exchange, task.routing_key);
            }
        }
    }

    // 订阅指定队列并注册消息回调
    void MQClient::consume(const std::string &queue, const MessageCallback &callback) {
        if (_degraded) {
            WRN("MQ 已降级，跳过订阅: {}", queue);
            return;
        }
        std::unique_lock<std::mutex> declared_lock(_declared_mtx);
        {
            std::lock_guard<std::mutex> init_lock(_mtx);
            _consumed_done = false;
        }
        _channel.consume(queue) 
            .onMessage([this, callback](const AMQP::Message &message,  //收到的消息
                uint64_t deliveryTag,    //消息的唯一标识
                bool redelivered){
                bool ok = true;
                try {
                    callback(message.body(), message.bodySize());
                } catch (const std::exception &e) {
                    ok = false;
                    ERR("消费消息异常: {}", e.what());
                } catch (...) {
                    ok = false;
                    ERR("消费消息异常: 未知异常");
                }
                // 成功才 ack；失败 requeue，避免消息静默丢失
                if (ok) {
                    _channel.ack(deliveryTag);
                } else {
                    _channel.reject(deliveryTag, true);
                }
            })
            .onError([this, q = std::string(queue)](const char *message) {
                ERR("订阅队列消息失败: {} - {}", q, message);
                {
                    std::lock_guard<std::mutex> notify_lock(_mtx);
                    _consumed_done = true;
                }
                _cv.notify_all();
            })
            .onSuccess([this, q = std::string(queue)](){
                INF("订阅队列消息成功: {}", q);
                {
                    std::lock_guard<std::mutex> notify_lock(_mtx);
                    _consumed_done = true;
                }
                _cv.notify_all();
            });
        std::unique_lock<std::mutex> lock(_mtx);
        if (_cv.wait_for(lock, std::chrono::seconds(3), [this]() { return _consumed_done; }) == false) {
            ERR("订阅队列超时(3s)，MQ 订阅降级: {}", queue);
            return;
        }
    }
    // 有些场景下，我们程序只进行消息的处理，没有其他业务，因此就希望能够阻塞当前主线程
    void MQClient::wait() {
        _async_thread.join();
    }

    //构造函数内，对成员进行初始化，并声明套件内的交换机和队列
    Publisher::Publisher(const MQClient::ptr &mq_client, const declare_settings &settings)
        : _mq_client(mq_client)
        , _settings(settings) {
        _mq_client->declare(_settings);
    }
    // 按配置的交换机和绑定键发布消息
    bool Publisher::publish(const std::string &body) {
        //发布消息的时候，不管是延时队列，还是普通队列，都是将消息发布到普通交换机
        return _mq_client->publish(_settings.exchange, _settings.binding_key, body);
    }

    //构造函数内，对成员进行初始化，并声明套件内的交换机和队列
    Subscriber::Subscriber(const MQClient::ptr &mq_client, const declare_settings &settings)
       : _mq_client(mq_client)
       , _settings(settings) {
        _mq_client->declare(_settings);
    }

    //订阅消息的时候，要注意：如果是延时队列，则实际订阅的是配套的死信队列
    void Subscriber::consume(MessageCallback &&cb) {
        _callback = std::move(cb);
        //如果是延时队列，顶订阅的是死信队列消息，否则订阅的是常规队列消息
        if (_settings.exchange_type == DELAYED) {
            _mq_client->consume(_settings.dlx_queue(), _callback);
        }else {
            _mq_client->consume(_settings.queue, _callback);
        }
    }
}