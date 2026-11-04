#include <JianLiuScaffold/util.h>
#include <JianLiuScaffold/log.h>
#include "svc_server.h"
#include <pplx/threadpool.h>
#include <cstdio>
#include <exception>


DEFINE_int32(log_level, 1, "日志等级: 1-debug;2-info;3-warn;4-error; 6-off");
DEFINE_bool(log_async, false, "日志是否异步"); 
DEFINE_string(log_format, "[%H:%M:%S][%-7l]: %v", "日志格式");
DEFINE_string(log_path, "stdout", "日志输出目标");


DEFINE_int32(listen_port, 9002, "用户子服务监听端口");

DEFINE_string(mq_url, "amqp://admin:123456@192.168.65.128:5672/", "消息队列地址");

DEFINE_string(registry_center_addr, "http://192.168.65.128:2379", "注册中心地址");
DEFINE_string(service_name, "user", "服务名称");
DEFINE_string(service_addr, "192.168.65.128:9002", "服务地址");

DEFINE_int32(mysql_port, 3306, "mysql端口");
DEFINE_int32(mysql_connection_pool_size, 5, "mysql连接池大小");
DEFINE_string(mysql_host, "192.168.65.128", "mysql服务器地址");
DEFINE_string(mysql_user, "root", "mysql用户名");
DEFINE_string(mysql_passwd, "123456", "mysql密码");
DEFINE_string(mysql_db, "vptest", "mysql数据库名");

DEFINE_int32(redis_port, 6379, "redis端口");
DEFINE_int32(redis_connection_pool_size, 5, "redis连接池大小");
DEFINE_string(redis_host, "192.168.65.128", "redis服务器地址");
DEFINE_string(redis_passwd, "123456", "redis密码");
DEFINE_string(redis_user, "default", "redis用户名");

DEFINE_string(delete_file_queue_info, R"({"exchange": "delete_file_exchange", "type": "direct", "queue": "delete_file_queue", "binding_key": "delete_file_queue"})", "删除文件队列信息");
DEFINE_string(cache_del_queue_info, R"({"exchange": "delete_cache_exchange", "type": "delayed", "queue": "delete_cache_queue", "binding_key": "delete_cache_queue", "delayed_ttl":3})", "删除缓存队列信息");

DEFINE_string(mail_username, "JzSan521@163.com", "邮箱用户名");
DEFINE_string(mail_password, "UBTtCJ76iezXC9ws", "邮箱密码或授权码");
DEFINE_string(mail_url, "smtps://smtp.163.com:465", "邮箱服务器地址");
DEFINE_string(mail_from, "JzSan521@163.com", "发送端邮箱地址信息");


int main(int argc, char *argv[])
{
    try {
        crossplat::threadpool::initialize_with_threads(4);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "init cpprest threadpool skipped: %s\n", e.what());
    }
    google::ParseCommandLineFlags(&argc, &argv, true);
    JianLiuLog::log_settings  lsettings = {
        .async = FLAGS_log_async,
        .level = FLAGS_log_level,
        .format = FLAGS_log_format,
        .path = FLAGS_log_path,
    };
    //初始化全局日志模块
    JianLiuLog::init(lsettings);

    JianLiuOdb::mysql_settings msettings = {
        .host =     FLAGS_mysql_host,
        .user =     FLAGS_mysql_user,
        .passwd =   FLAGS_mysql_passwd,
        .db =       FLAGS_mysql_db,
        .connection_pool_size = (unsigned int)FLAGS_mysql_connection_pool_size
    };
    
    JianLiuRedis::redis_settings rsettings = {
        .port = FLAGS_redis_port,
        .host = FLAGS_redis_host,
        .user = FLAGS_redis_user,
        .passwd = FLAGS_redis_passwd,
        .connection_pool_size = (size_t)FLAGS_redis_connection_pool_size
    };

    svc_user::registry_settings reg_settings = {
        .registry_center_addr = FLAGS_registry_center_addr,
        .service_name = FLAGS_service_name,
        .service_addr = FLAGS_service_addr
     };

    JianLiuCode::mail_settings esettings = {
        .username = FLAGS_mail_username,
        .password = FLAGS_mail_password,
        .url = FLAGS_mail_url,
        .from = FLAGS_mail_from
    };

    svc_user::UserServerBuilder builder;
    auto server = builder.withListenPort(FLAGS_listen_port)
        .withMqUrl(FLAGS_mq_url)
        .withRegistrySettings(reg_settings)
        .withMysqlSettings(msettings)
        .withRedisSettings(rsettings)
        .withMqDelFileSettings(FLAGS_delete_file_queue_info)
        .withMqDelCacheSettings(FLAGS_cache_del_queue_info)
        .withEmailSettings(esettings)
        .build();
    //启动服务
    server->start();
    return 0;
}