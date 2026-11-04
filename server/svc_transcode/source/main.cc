#include <gflags/gflags.h>
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

DEFINE_string(mq_url, "amqp://admin:123456@192.168.65.128:5672/", "消息队列服务器地址");

DEFINE_int32(mysql_port, 3306, "mysql端口");
DEFINE_int32(mysql_connection_pool_size, 5, "mysql连接池大小");
DEFINE_string(mysql_host, "192.168.65.128", "mysql服务器地址");
DEFINE_string(mysql_user, "root", "mysql用户名");
DEFINE_string(mysql_passwd, "123456", "mysql密码");
DEFINE_string(mysql_db, "vptest", "mysql数据库名");

DEFINE_string(transcode_queue_info, R"({"exchange": "hls_transcode_exchange", "type": "direct", "queue": "hls_transcode_queue", "binding_key": "hls_transcode_queue"})", "视频转码队列信息");

DEFINE_int32(hls_time, 10, "HLS分片时长");
DEFINE_string(hls_play_type, "vod", "HLS播放类型");
DEFINE_string(hls_base_url, "/HttpService/downloadVideo/?fileId=", "HLS播放类型");

DEFINE_string(fdfs_tracker_host, "192.168.65.128:22122", "FASTDFS服务器地址");

DEFINE_string(temp_dir, "./temp_path", "临时文件存放路径");

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
    JianLiuLog::init(lsettings);

    JianLiuOdb::mysql_settings msettings = {
        .host =     FLAGS_mysql_host,
        .user =     FLAGS_mysql_user,
        .passwd =   FLAGS_mysql_passwd,
        .db =       FLAGS_mysql_db,
        .connection_pool_size = (unsigned int)FLAGS_mysql_connection_pool_size
    };
   
    JianLiuHls::hls_setttings hsettings = {
        .hls_time = (size_t)FLAGS_hls_time,
        .playlist_type = FLAGS_hls_play_type
    };

    JianLiuFdfs::fdfs_settings fsettings = {
       .trackers = {FLAGS_fdfs_tracker_host}
    };
    
    svc_transcode::TranscodeServerBuilder builder;
    auto server = builder.withMqUrl(FLAGS_mq_url)
        .withTempDir(FLAGS_temp_dir)
        .withHLSBaseUrl(FLAGS_hls_base_url)
        .withMysqlSettings(msettings)
        .withMqTranscodeSettings(FLAGS_transcode_queue_info)
        .withFDFSSettings(fsettings)
        .withHLSSettings(hsettings)
        .build();
    server->wait();
    return 0;
}