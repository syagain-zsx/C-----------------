
#include <message.pb.h>
#include "svc_mq.h"
#include <filesystem>

namespace svc_transcode {
    TranscodeMQ::TranscodeMQ(const SvcData::ptr &svc_data,
        const JianLiuHls::HLSTranscoder::ptr &transcoder,
        const JianLiuMq::MQClient::ptr &mq_client,
        const JianLiuMq::declare_settings &trans_settings,
        const std::string &temp_path,
        const std::string &access_url)
        : _access_url(access_url)
        , _temp_path(temp_path)
        , _svc_data(svc_data)
        , _worker(std::make_shared<SvcWorker>())
        , _transcoder(transcoder){
        std::filesystem::create_directories(_temp_path);
        _subscriber = JianLiuMq::MQFactory::create<JianLiuMq::Subscriber>(mq_client, trans_settings);
        _subscriber->consume(std::bind(&TranscodeMQ::callback, this, std::placeholders::_1, std::placeholders::_2));
    }
    bool TranscodeMQ::transcode(const std::string &video_id) {
        std::string local_path = _temp_path + "/" + video_id;
        std::string m3u8_path = _temp_path + "/" + video_id + ".m3u8";
        std::vector<std::string> old_pieces_path;
        try {
            //2. 通过视频ID获取视频元信息，提取视频文件ID
            auto video = _svc_data->getVideo(video_id);
            std::string file_id = video->video_fid();
            //3. 通过视频文件ID，获取文件元信息，提取文件存储路径
            auto file = _svc_data->getFile(file_id);
            std::string file_path = file->path();
            //4. 从FDFS中通过视频存储路径，下载视频文件数据到本地
            bool ret = JianLiuFdfs::FDFSClient::download_to_file(file_path, local_path);
            if (ret == false) {
                ERR("文件下载失败: {} - {} - {} - {}", video_id, file_id, file_path, local_path);
                throw vp_error::VPException(vp_error::VpError::TRANSCODE_FAILED);
            }
            //5. 调用hls转码器，对视频文件进行转码： 会生成一堆转码后的文件
            ret = _transcoder->transcode(local_path, m3u8_path);
            if (ret == false) {
                ERR("文件转码失败: {} - {}", local_path, m3u8_path);
                throw vp_error::VPException(vp_error::VpError::TRANSCODE_FAILED);
            }
            //    1. m3u8文件： 内部记录的所有的分片信息，以及分片获取方式
            //    2. ts文件： 视频分片数据
            //6. 解析m3u8文件，获取到所有的分片信息
            JianLiuHls::M3U8Info m3u8_info(m3u8_path);
            ret = m3u8_info.parse();
            if (ret == false) {
                ERR("m3u8文件解析失败: {}", m3u8_path);
                throw vp_error::VPException(vp_error::VpError::TRANSCODE_FAILED);
            }
            auto &ts_pieces = m3u8_info.pieces();
            for (auto &piece : ts_pieces) {
                std::string ts_local_path = _temp_path + "/" + piece.second;
                old_pieces_path.push_back(ts_local_path);
            }
            for (auto &piece : ts_pieces) {
                //6. 将生成的ts文件，上传到FDFS中,并获取到分片各自的存储路径（用于修改m3u8文件中的分片路径）
                // <#EXTINF:10.0, http://xxx:90000/segment1.ts>
                std::string ts_local_path = _temp_path + "/" + piece.second;
                auto ts_remote_path = JianLiuFdfs::FDFSClient::upload_from_file(ts_local_path);
                if (!ts_remote_path) {
                    ERR("ts文件上传失败: {} (video={})", ts_local_path, video_id);
                    throw vp_error::VPException(vp_error::VpError::TRANSCODE_FAILED);
                }
                // 为生成的ts文件，生成一个新的文件元信息，并添加到数据库中
                std::string file_id = JianLiuUtil::Random::code();
                piece.second = _access_url + file_id;
                vp_data::File ts_file;
                ts_file.set_file_id(file_id);
                ts_file.set_path(*ts_remote_path);
                ts_file.set_size(std::filesystem::file_size(ts_local_path));
                ts_file.set_mime("video/MP2T");
                ts_file.set_uploader_uid(file->uploader_uid());
                _svc_data->newFile(ts_file);
            }
            
            //7. 修改m3u8文件中的分片路径， 将m3u8文件上传到FDFS中，并获取到m3u8文件的存储路径
            m3u8_info.write();
            auto m3u8_remote_path = JianLiuFdfs::FDFSClient::upload_from_file(m3u8_path);
            if (!m3u8_remote_path) {
                ERR("m3u8文件上传失败: {} (video={})", m3u8_path, video_id);
                throw vp_error::VPException(vp_error::VpError::TRANSCODE_FAILED);
            }
            //8. 将m3u8文件的存储路径，更新到文件元信息中（并修改文件mime）
            file->set_path(*m3u8_remote_path);
            file->set_size(std::filesystem::file_size(m3u8_path));
            file->set_mime("application/x-mpegURL");
            _svc_data->setFile(file);
            //9. 修改视频元信息状态：转码中->审核中
            video->set_status(vp_data::VideoStatus::AUDITING);
            _svc_data->setVideo(video);
            //11. 删除FDFS中转码前的视频文件
            JianLiuFdfs::FDFSClient::remove(file_path);
            DBG("视频转码成功: {}", video_id);
        } catch (std::exception &e) {
            ERR("视频转码失败: {}", e.what());
        }
        //10. 删除所有的临时文件
        std::filesystem::remove(local_path);
        std::filesystem::remove(m3u8_path);
        for (auto &piece : old_pieces_path) {
            std::filesystem::remove(piece);
        }
        return true;
    }

    void TranscodeMQ::callback(const char *body, size_t len) {
        //1. 对消息进行反序列化，提取消息中的视频ID
        api_message::HLSTranscodeMsg msg;
        bool ret = msg.ParseFromArray(body, len);
        if (ret == false) {
            ERR("转码消息反序列化失败！");
            return;
        }
        for (int i = 0; i < msg.video_id_size(); i++) {
            std::string video_id = msg.video_id(i);
            // 转码耗时较长，放到工作线程池里执行，避免阻塞 amqpcpp 的事件循环
            auto task = std::bind(&TranscodeMQ::transcode, this, video_id);
            _worker->addTask(std::move(task));
            DBG("视频转码任务添加成功: {}", video_id);
        }
    }
}