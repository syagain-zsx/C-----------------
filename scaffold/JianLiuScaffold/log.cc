#include "log.h"
#include <cstdio>

namespace JianLiuLog
{
    //全局化定义
    std::shared_ptr<spdlog::logger> g_logger;

    // 按配置创建同步/异步、stdout/file 日志器
    void init(const log_settings &settings)
    {
        // 0. 格式化参数不匹配时只打印错误，避免异常从日志路径传播到业务线程
        spdlog::set_error_handler([](const std::string &msg) {
            fprintf(stderr, "[spdlog] %s\n", msg.c_str());
        });
        // 1. 判断日志器类型 - async/sync
        // 2. 判断输出目标 -- stdout, file
        // 3. 创建日志器
        if (settings.async == true)
        {
            // 异步日志
            if (settings.path == "stdout")
            {
                g_logger = spdlog::stdout_color_mt<spdlog::async_factory>("stdout_logger");
            }
            else
            {
                g_logger = spdlog::basic_logger_mt<spdlog::async_factory>("file_logger", settings.path);
            }
        }
        else
        {
            //同步日志
            if (settings.path == "stdout")
            {
                g_logger = spdlog::stdout_color_mt("stdout_logger");
            }
            else
            {
                g_logger = spdlog::basic_logger_mt("file_logger", settings.path);
            }
        }
        // 4. 设置日志等级
        g_logger->set_level(spdlog::level::level_enum(settings.level));
        // 5. 设置日志格式
        g_logger->set_pattern(settings.format);
    }
}