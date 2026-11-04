#pragma once
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/opt.h>
    #include <libavutil/error.h>
}
#include <iostream>
#include <string>
#include <vector>
#include <memory>

namespace JianLiuHls {

    const std::string HLS_EXTM3U = "#EXTM3U";
    const std::string HLS_VERSION = "#EXT-X-VERSION:";
    const std::string HLS_TARGETDURATION = "#EXT-X-TARGETDURATION:";
    const std::string HLS_SEQUENCE = "EXT-X-MEDIA-SEQUENCE:";
    const std::string HLS_PLAYLIST_TYPE = "EXT-X-PLAYLIST-TYPE";
    const std::string HLS_INDEPENDENT_SEGMENTS = "#EXT-X-INDEPENDENT-SEGMENTS";
    const std::string HLS_ENDLIST = "#EXT-X-ENDLIST";
    const std::string HLS_EXTINF = "#EXTINF:";

    // M3U8 播放列表解析与写回工具
    class M3U8Info {
        public:
            using ptr = std::shared_ptr<M3U8Info>;
            using StrPair = std::pair<std::string, std::string>;
            // 保存待解析或写入的 m3u8 文件名
            M3U8Info(const std::string &filename);
            // 解析 m3u8 文件，分离头部标签与分片信息
            bool parse();
            // 把头部标签与分片信息写回 m3u8 文件
            bool write();
            // 返回 m3u8 头部标签列表
            std::vector<std::string>& headers();
            // 返回分片信息（EXTINF 与分片文件名）列表
            std::vector<StrPair>& pieces();
        private:
            std::string _filename;
            std::vector<std::string> _headers;
            std::vector<StrPair> _pieces;
    };
    // HLS 转码配置
    struct hls_setttings {
        size_t hls_time;
        std::string playlist_type;
        std::string base_url;
    };
    // HLS 转码器：把输入媒体文件切分为 HLS 分片
    class HLSTranscoder {
        public:
            using ptr = std::shared_ptr<HLSTranscoder>;
            // 保存 HLS 转码配置
            HLSTranscoder(const hls_setttings &settings);
            // 执行转码并生成 m3u8 与 ts 分片
            bool transcode(const std::string &input, const std::string &output);
        private:
            // 将 FFmpeg 错误码转换为可读字符串
            const char *avError(int err_code);
        private:
            hls_setttings _settings;
    };
}