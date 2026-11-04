#pragma once
#include <iostream>
#include <vector>
#include <optional>
extern "C" {
#include <fastdfs/fdfs_client.h>
#include <fastcommon/logger.h>
}


namespace JianLiuFdfs {
    // FastDFS 客户端初始化配置
    struct fdfs_settings {
        std::vector<std::string> trackers;
        int connect_timeout = 30;
        int network_timeout = 30;
        bool use_connection_pool = true;
        int connection_pool_max_idle_time = 3600;
    };

    // FastDFS 文件客户端，封装上传、下载和删除操作
    class FDFSClient {
        public:
            // 初始化客户端全局配置
            static void init(const fdfs_settings &settings);
            // 释放 FastDFS 全局资源
            static void destroy();
            // 上传内存数据，成功返回 file_id
            static std::optional<std::string> upload_from_buff(const std::string &buff);
            // 上传本地文件，成功返回 file_id
            static std::optional<std::string> upload_from_file(const std::string &path);
            // 按 file_id 下载到内存缓冲区
            static bool download_to_buff(const std::string &file_id, std::string &buff);
            // 按 file_id 下载到本地文件
            static bool download_to_file(const std::string &file_id, const std::string &path);
            // 按 file_id 删除文件
            static bool remove(const std::string &file_id);
    };
}