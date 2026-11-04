#include "svc_data.h"

namespace svc_video {

    SvcData::SvcData(const std::shared_ptr<sw::redis::Redis> &redis, 
        const std::shared_ptr<odb::database> &mysql,
        const JianLiuIcs::ESClient::ptr &es_client,
        const vp_sync::CacheSync::ptr &cache_to_db_sync,
        const vp_sync::CacheSync::ptr &cache_delete_sync,
        const JianLiuMq::Publisher::ptr &del_file_publisher,
        const JianLiuMq::Publisher::ptr &transcode_publisher)
        : _redis(redis)
        , _mysql(mysql)
        , _es_client(es_client)
        , _cache_to_db_sync(cache_to_db_sync)
        , _cache_delete_sync(cache_delete_sync)
        , _del_file_publisher(del_file_publisher)
        , _transcode_publisher(transcode_publisher) {}
    std::string SvcData::getSessionUser(const std::string &ssid, odb::transaction &mtx) {
        vp_data::SessionData sd(mtx, _redis, _cache_delete_sync);
        auto ss = sd.selectBySid(ssid);
        if (!ss) {
            ERR("会话信息不存在: {}", ssid);
            throw vp_error::VPException(vp_error::VpError::SESSION_INVALID);
        }
        auto cur_uid = ss->user_id();
        if (!cur_uid) {
            ERR("会话用户不存在: {}", ssid);
            throw vp_error::VPException(vp_error::VpError::SESSION_NOT_LOGIN);
        }
        return *cur_uid;
    }
    void SvcData::newVideo(const std::string &ssid, const api_message::NewVideoInfo &info) {
        try {
            odb::transaction mtx(_mysql->begin());
            // 1. 通过会话ID，获取会话信息，提取当前上传用户ID（若会话不存在则直接抛异常返回）
            std::string cur_uid = getSessionUser(ssid, mtx);
            // 2. 构造视频元信息对象，初始化状态为转码中状态 
            std::string video_id = JianLiuUtil::Random::code(_uuid_length);
            vp_data::Video  video;
            video.set_video_id(video_id);
            video.set_title(info.videotitle());
            video.set_summary(info.videodesc());
            video.set_cover_fid(info.photofileid());
            video.set_video_fid(info.videofileid());
            video.set_uploader_uid(cur_uid);
            video.set_duration(info.duration());
            // video.set_size(info.size());
            // 3. 向数据库中新增视频元信息
            vp_data::VideoData vd(mtx, _redis, _cache_delete_sync);
            vd.insert(video);
            // 4. 向数据库中新增视频分类标签信息
            vp_data::VideoCatgoryTagData vctd(mtx);
            for (int i = 0; i < info.videotag_size(); i++) {
                vctd.insert(video_id, info.videotype(), info.videotag(i));
            }
            // 5. 提交事务
            mtx.commit();
            // 6. 发布视频转码消息
            api_message::HLSTranscodeMsg msg;
            msg.add_video_id(video_id);
            _transcode_publisher->publish(msg.SerializeAsString());
        }catch (const vp_error::VPException &e) {
            ERR("新增视频元信息失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    void SvcData::delVideo(const std::string &ssid, const std::string &vid) {
        try {
            odb::transaction mtx(_mysql->begin());
            // 1. 通过会话ID，获取会话信息，提取当前上传用户ID（若会话不存在则直接抛异常返回）
            std::string cur_uid = getSessionUser(ssid, mtx);
            // 3. 通过视频ID，删除并获取视频信息 （判断上传用户是否一致）
            vp_data::VideoData vd(mtx, _redis, _cache_delete_sync);
            auto video = vd.get(vid);
            if (!video) {
                ERR("视频信息不存在: {}", vid);
                throw vp_error::VPException(vp_error::VpError::VIDEO_NOT_EXISTS);
            }
            if (video->uploader_uid() != cur_uid) {
                ERR("视频上传用户不一致: {}, {}", video->uploader_uid(), cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            
            // 10. 获取视频分类标签信息，同步视频列表缓存
            vp_data::VideoCatgoryTagData vctd(mtx);
            size_t catgory_id;
            std::vector<size_t> tag_ids;
            vctd.getCatgoryTags(vid, catgory_id, tag_ids);

            // 4. 删除视频信息
            vd.remove(vid);
            // 5. 删除视频分类标签信息
            vctd.remove(vid);
            // 6. 删除视频点赞信息
            vp_data::VideoLikeData vld(mtx);
            vld.remove(vid);
            // 7. 删除视频弹幕信息
            vp_data::BarrageData bd(mtx);
            bd.remove(vid);
            // 8. 删除搜索引擎中视频信息
            vp_data::VideoSearchData vsd(_es_client);
            vsd.remove(vid);
            // 9. 提交事务（先提交，成功后再发删除文件消息，避免回滚后文件已被删）
            vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
            vild.syncPageListCache(video->video_id(), video->uploader_uid(), catgory_id, tag_ids);
            // 10. 提交事务
            mtx.commit();
            api_message::DeleteFileMsg msg;
            msg.add_file_id(video->video_fid());
            msg.add_file_id(video->cover_fid());
            _del_file_publisher->publish(msg.SerializeAsString());
        }catch (const vp_error::VPException &e) {
            ERR("删除视频失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    bool SvcData::judgeLike(const std::string &ssid, const std::string &vid) {
        try {
            // 1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            // 2. 通过会话ID，获取会话信息，提取当前上传用户ID（若会话不存在则直接抛异常返回）
            std::string cur_uid = getSessionUser(ssid, mtx);
            // 3. 判断该用户是否对指定视频进行了点赞
            vp_data::VideoLikeData vld(mtx);
            bool result = vld.judge(vid, cur_uid);
            // 4. 提交事务
            mtx.commit();
            return result;
        }catch (const vp_error::VPException &e) {
            ERR("点赞判断失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    void SvcData::likeVideo(const std::string &ssid, const std::string &vid) {
        try {
            // 1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            // 2. 通过会话ID，获取会话信息，提取当前上传用户ID（若会话不存在则直接抛异常返回）
            std::string cur_uid = getSessionUser(ssid, mtx);
            // 3. 判断该用户是否对指定视频进行了点赞
            vp_data::VideoLikeData vld(mtx);
            bool result = vld.judge(vid, cur_uid);
            if (!result) {
                // 4. 若没有点赞，则新增点赞信息
                vld.insert(vid, cur_uid);
            }else {
                // 5. 若已经点赞，则删除点赞信息
                vld.cancel(vid, cur_uid);
            }
            // 6. 更新缓存中的视频点赞量，以及用户视频点赞量
            vp_data::VideoCountData vcd(mtx, _redis, _cache_to_db_sync);
            vcd.updateVideoLikeCount(vid, result ? -1 : 1);
            vp_data::UserCountData ucd(mtx, _redis);
            ucd.updateLikeCount(cur_uid, result? -1 : 1);
            // 7. 提交事务
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            ERR("视频点赞操作失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    void SvcData::playVideo(const std::string &ssid, const std::string &vid) {
        try {
            // 1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            // 2. 通过视频ID获取视频信息，提取视频上传用户ID
            vp_data::VideoData vd(mtx, _redis, _cache_delete_sync);
            auto video = vd.get(vid);
            if (!video) {
                ERR("视频信息不存在: {}", vid);
                throw vp_error::VPException(vp_error::VpError::VIDEO_NOT_EXISTS);
            }
            // 3. 更新视频播放量
            vp_data::VideoCountData vcd(mtx, _redis, _cache_to_db_sync);
            vcd.updateVideoPlayCount(vid, 1); //这个更新的播放量更新的是缓存数据
            // 4. 更新用户视频播放量
            // 假如：更新用户统计数据的时候，用户统计数据不在缓存中，则不对缓存数据进行操作
            //    但是这时候数据库中用户视频播放量时0
            //    这时候如果获取用户详细信息，会获取用户统计数据，这时候从数据库拿到的视频播放量就是0
            vp_data::UserCountData ucd(mtx, _redis);
            ucd.updatePlayCount(video->uploader_uid(), 1);
            // 5. 提交事务
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            ERR("视频播放量更新失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    void SvcData::newBarrage(const std::string &ssid, const api_message::BarrageInfo &info) {
        try {
            // 1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            // 2. 通过会话ID，获取会话信息，提取当前上传用户ID（若会话不存在则直接抛异常返回）
            std::string cur_uid = getSessionUser(ssid, mtx);
            // 3. 生成弹幕ID，构造弹幕信息对象
            std::string barrage_id = JianLiuUtil::Random::code(_uuid_length);
            vp_data::Barrage barrage;
            barrage.set_barrage_id(barrage_id);
            barrage.set_video_id(info.videoid());
            barrage.set_user_id(cur_uid);
            barrage.set_content(info.barragecontent());
            barrage.set_time(info.barragetime());
            // 4. 向数据库中新增弹幕信息
            vp_data::BarrageData bd(mtx);
            bd.insert(barrage);
            // 5. 提交事务
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            ERR("新增弹幕信息失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    std::vector<api_message::BarrageInfo> SvcData::getBarrage(const std::string &ssid, const std::string &vid) {
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //2. 通过视频ID，获取视频弹幕列表
            vp_data::BarrageData bd(mtx);
            auto barrage_list = bd.list(vid);
            //3. 根据获取到的弹幕信息列表，构造返回值
            std::vector<api_message::BarrageInfo> result;
            for (auto &barrage : barrage_list) {
                api_message::BarrageInfo info;
                info.set_barrageid(barrage->barrage_id());
                info.set_videoid(barrage->video_id());
                info.set_userid(barrage->user_id());
                info.set_barragecontent(barrage->content());
                info.set_barragetime(barrage->time());
                result.push_back(info);
            }
            //4. 提交事务
            mtx.commit();
            return result;
        }catch (const vp_error::VPException &e) {
            ERR("获取弹幕列表失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    void SvcData::checkVideo(const std::string &ssid, const std::string &vid, bool result) {
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //2. 通过会话ID，获取会话信息，提取操作用户ID
            std::string cur_uid = getSessionUser(ssid, mtx);
            //3. 判断操作用户是否是管理员
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_delete_sync);
            bool ret = uird.hasIdentify(cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //4. 判断该管理员是否是启用状态
            vp_data::UserData ud(mtx, _redis, _cache_delete_sync);
            ret = ud.isEnable(cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //5. 通过视频ID，从数据库获取视频信息
            vp_data::VideoData vd(mtx, _redis, _cache_delete_sync);
            auto video = vd.getFromDB(vid);
            if (!video) {
                ERR("视频信息不存在: {}", vid);
                throw vp_error::VPException(vp_error::VpError::VIDEO_NOT_EXISTS);
            }
            //6. 更新视频信息（状态&审核用户）
            video->set_status(result? vp_data::VideoStatus::ONLINE : vp_data::VideoStatus::REJECT);
            video->set_auditor_uid(cur_uid);
            vd.update(video);
            //7. 若审核状态为通过，则向ES中新增视频信息（并初始化检索状态为true）
            if (result) {
                vp_data::VideoSearchData vsd(_es_client);
                std::string desc = video->summary() ? video->summary().get() : "";
                vsd.insert(video->video_id(), video->title(), desc, true);

                vp_data::VideoCatgoryTagData vctd(mtx);
                size_t catgory_id;
                std::vector<size_t> tag_ids;
                vctd.getCatgoryTags(vid, catgory_id, tag_ids);
                vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
                vild.syncPageListCache(video->video_id(), video->uploader_uid(), catgory_id, tag_ids);
            }
            //8. 提交事务
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            ERR("视频审核失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    void SvcData::onlineVideo(const std::string &ssid, const std::string &vid) {
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //2. 通过会话ID，获取会话信息，提取操作用户ID
            std::string cur_uid = getSessionUser(ssid, mtx);
            //3. 判断操作用户是否是管理员
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_delete_sync);
            bool ret = uird.hasIdentify(cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //4. 判断该管理员是否是启用状态
            vp_data::UserData ud(mtx, _redis, _cache_delete_sync);
            ret = ud.isEnable(cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //5. 通过视频ID，从数据库获取视频信息
            vp_data::VideoData vd(mtx, _redis, _cache_delete_sync);
            auto video = vd.getFromDB(vid);
            if (!video) {
                ERR("视频信息不存在: {}", vid);
                throw vp_error::VPException(vp_error::VpError::VIDEO_NOT_EXISTS);
            }
            //6. 更新视频信息（状态）
            video->set_status(vp_data::VideoStatus::ONLINE);
            vd.update(video);
            //7. 将ES中视频的检索状态值为true
            vp_data::VideoSearchData vsd(_es_client);
            std::string desc = video->summary() ? video->summary().get() : "";
            vsd.update(video->video_id(), video->title(), desc, true);

            
            vp_data::VideoCatgoryTagData vctd(mtx);
            size_t catgory_id;
            std::vector<size_t> tag_ids;
            vctd.getCatgoryTags(vid, catgory_id, tag_ids);
            vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
            vild.syncPageListCache(video->video_id(), video->uploader_uid(), catgory_id, tag_ids);
            //8. 提交事务
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            ERR("视频上架失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    void SvcData::offlineVideo(const std::string &ssid, const std::string &vid) {
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //2. 通过会话ID，获取会话信息，提取操作用户ID
            std::string cur_uid = getSessionUser(ssid, mtx);
            //3. 判断操作用户是否是管理员
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_delete_sync);
            bool ret = uird.hasIdentify(cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //4. 判断该管理员是否是启用状态
            vp_data::UserData ud(mtx, _redis, _cache_delete_sync);
            ret = ud.isEnable(cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //5. 通过视频ID，从数据库获取视频信息
            vp_data::VideoData vd(mtx, _redis, _cache_delete_sync);
            auto video = vd.getFromDB(vid);
            if (!video) {
                ERR("视频信息不存在: {}", vid);
                throw vp_error::VPException(vp_error::VpError::VIDEO_NOT_EXISTS);
            }
            //6. 更新视频信息（状态）
            video->set_status(vp_data::VideoStatus::OFFLINE);
            vd.update(video);
            //7. 将ES中视频的检索状态值为false
            vp_data::VideoSearchData vsd(_es_client);
            std::string desc = video->summary() ? video->summary().get() : "";
            vsd.update(video->video_id(), video->title(), desc, false);
            
            vp_data::VideoCatgoryTagData vctd(mtx);
            size_t catgory_id;
            std::vector<size_t> tag_ids;
            vctd.getCatgoryTags(vid, catgory_id, tag_ids);
            vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
            vild.syncPageListCache(video->video_id(), video->uploader_uid(), catgory_id, tag_ids);
            //8. 提交事务
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            ERR("视频下架失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    std::vector<api_message::VideoInfo> SvcData::getDetailVideoList(std::vector<std::string> &vid_list
        , odb::transaction &mtx) {
        std::vector<api_message::VideoInfo> result;
        //5. 若列表不为空，则获取相关视频信息
        vp_data::VideoData vd(mtx, _redis, _cache_delete_sync);
        auto video_list = vd.list(vid_list);
        //6. 若列表不为空，则获取相关用户信息
        std::unordered_set<std::string> uids;
        for (auto &it : video_list) {
            uids.insert(it.second->uploader_uid());
            // uids.insert(video->auditor_uid());
        }
        vp_data::UserData ud(mtx, _redis, _cache_delete_sync);
        auto user_list = ud.getListById(uids);
        //7. 组织响应进行返回
        for (auto &vid: vid_list) {
            if (video_list.find(vid) == video_list.end()) {
                WRN("未找到视频信息: {}", vid);
                continue;
            }
            auto video = video_list[vid];
            if (user_list.find(video->uploader_uid()) == user_list.end()) {
                WRN("未找到用户信息: {}", video->uploader_uid());
                continue;
            }
            auto user = user_list[video->uploader_uid()];
            //获取视频统计数据
            vp_data::VideoCountData vcd(mtx, _redis, _cache_to_db_sync);
            auto count = vcd.getCount(vid);
            if (!count) {
                WRN("未找到视频统计信息: {}", vid);
                continue;
            }
            //获取视频分类标签信息
            vp_data::VideoCatgoryTagData vctd(mtx);
            size_t catgory_id = 0;
            std::vector<size_t> tag_ids;
            vctd.getCatgoryTags(vid, catgory_id, tag_ids);
            api_message::VideoInfo info;
            info.set_videoid(video->video_id());
            info.set_userid(video->uploader_uid());
            if (user->avatar())info.set_useravatarid(user->avatar().get());
            info.set_nickname(user->nickname());
            if (catgory_id != 0) info.set_videotype(catgory_id);
            for (auto &tag_id : tag_ids) { info.add_videotag(tag_id); }
            info.set_videofileid(video->video_fid());
            info.set_photofileid(video->cover_fid());
            info.set_likecount(count->like_count);
            info.set_playcount(count->play_count);
            info.set_videosize(video->size());
            if (video->summary())info.set_videodesc(video->summary().get());
            info.set_videotitle(video->title());
            info.set_videoduration(video->duration());
            info.set_videouptime(boost::posix_time::to_simple_string(video->upload_time()));
            info.set_videostatus((int)video->status());
            result.push_back(info);
        }
        return result;
    }
    api_message::userVideoListResult SvcData::getUserPageList(const std::string &ssid, 
        const std::optional<std::string> &uid, 
        vp_data::VideoStatus status,
        size_t page, size_t count) {
        api_message::userVideoListResult result;
        // 防御非法分页：page 过大或 count=0/过大都会导致越界/巨大 key/整数溢出
        if (page > 100000) return result;
        if (count == 0) count = 20;
        if (count > 100) count = 100;
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //2. 通过会话ID，获取会话信息，提取操作用户ID
            std::string cur_uid = getSessionUser(ssid, mtx);
            //3. 判断用户ID是否存在，若不存在，则使用操作用户ID
            std::string target_uid = uid? *uid : cur_uid;
            //4. 通过用户ID，获取用户视频ID列表
            vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
            DBG("获取用户视频列表：{} - {} - {}", target_uid, page, count);
            //这里的返回值，是一个结构，结构中包含两个成员：视频ID列表， 视频总数
            auto vid_list = vild.getUserPageList(target_uid, status, page, count); 
            //5. 获取详细视频信息列表，这里的返回值只有一个：是视频信息列表
            auto list = getDetailVideoList(vid_list->videos, mtx);
            //6. 组织响应进行返回
            result.set_totalcount(vid_list->total);
            auto video_list = result.mutable_videolist();
            for (auto &video : list) {
                auto src = result.add_videolist();
                src->CopyFrom(video);
            }
            //8. 提交事务
            mtx.commit();
            return result;
        }catch (const vp_error::VPException &e) {
            ERR("获取用户视频列表失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    std::vector<api_message::VideoInfo> SvcData::getMainPageList(size_t page, size_t count) {
        std::vector<api_message::VideoInfo> result;
        // 防御非法分页：page 过大或 count=0/过大都会导致越界/巨大 key/整数溢出
        if (page > 100000) return result;
        if (count == 0) count = 20;
        if (count > 100) count = 100;
        // 首页列表短缓存（3s）：热点页请求直接命中 Redis，避免重复的 DB/Redis 往返。
        // 视频上下架/新增最多延迟 3s 可见，测试脚本需轮询。
        const std::string cache_key = "cache_main_page_" + std::to_string(page) + "_" + std::to_string(count);
        try {
            auto cached = _redis->get(cache_key);
            if (cached) {
                api_message::AllVideoListResult cache_msg;
                if (cache_msg.ParseFromString(*cached)) {
                    return std::vector<api_message::VideoInfo>(cache_msg.videolist().begin(),
                                                               cache_msg.videolist().end());
                }
            }
        } catch (const std::exception &e) {
            WRN("读取首页列表缓存失败: {}", e.what());
        }
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //4. 通过用户ID，获取用户视频ID列表
            vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
            auto vid_list = vild.getMainPageList(page, count);
            //5. 获取详细视频信息列表
            result = getDetailVideoList(vid_list, mtx);
            //8. 提交事务
            mtx.commit();
            //9. 写入列表短缓存
            try {
                api_message::AllVideoListResult cache_msg;
                for (auto &video : result) {
                    cache_msg.add_videolist()->CopyFrom(video);
                }
                std::string bytes;
                if (cache_msg.SerializeToString(&bytes)) {
                    _redis->setex(cache_key, 3, bytes);
                }
            } catch (const std::exception &e) {
                WRN("写入首页列表缓存失败: {}", e.what());
            }
            return result;
        }catch (const vp_error::VPException &e) {
            ERR("获取主页视频列表失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    std::vector<api_message::VideoInfo> SvcData::getCatgoryPageList(size_t catgory_id, size_t page, size_t count) {
        std::vector<api_message::VideoInfo> result;
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //4. 通过用户ID，获取用户视频ID列表
            vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
            auto vid_list = vild.getCatgoryPageList(catgory_id, page, count);
            //5. 获取详细视频信息列表
            result = getDetailVideoList(vid_list, mtx);
            //8. 提交事务
            mtx.commit();
            return result;
        }catch (const vp_error::VPException &e) {
            ERR("获取分类视频列表失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    std::vector<api_message::VideoInfo> SvcData::getTagPageList(size_t tag_id, size_t page, size_t count) {
        std::vector<api_message::VideoInfo> result;
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //4. 通过用户ID，获取用户视频ID列表
            vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
            auto vid_list = vild.getTagPageList(tag_id, page, count);
            //5. 获取详细视频信息列表
            result = getDetailVideoList(vid_list, mtx);
            //8. 提交事务
            mtx.commit();
            return result;
        }catch (const vp_error::VPException &e) {
            ERR("获取标签视频列表失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    std::vector<api_message::VideoInfo> SvcData::getSearchPageList(const std::string &search_key, size_t page, size_t count) {
        std::vector<api_message::VideoInfo> result;
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //4. 通过用户ID，获取用户视频ID列表
            vp_data::VideoSearchData vsd(_es_client);
            auto vid_list = vsd.search(search_key, page, count);
            //5. 获取详细视频信息列表
            result = getDetailVideoList(vid_list, mtx);
            //8. 提交事务
            mtx.commit();
            return result;
        }catch (const vp_error::VPException &e) {
            ERR("获取关键字视频列表失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    std::vector<api_message::VideoInfo> SvcData::getStatusPageList(vp_data::VideoStatus status, size_t page, size_t count, size_t *total) {
        std::vector<api_message::VideoInfo> result;
        try {
            //1. 初始化事务对象
            odb::transaction mtx(_mysql->begin());
            //4. 通过用户ID，获取用户视频ID列表
            vp_data::VideoIdListData vild(mtx, _redis, _cache_delete_sync);
            auto video_list = vild.getStatusPageList(status, page, count);
            //6. 若列表不为空，则获取相关用户信息
            std::unordered_set<std::string> uids;
            for (auto &video : video_list->videos) {
                uids.insert(video->uploader_uid());
                if (video->auditor_uid()) uids.insert(video->auditor_uid().get());
            }
            vp_data::UserData ud(mtx, _redis, _cache_delete_sync);
            auto user_list = ud.getListById(uids);
            //7. 组织响应进行返回
            for (auto &video: video_list->videos) {
                //获取用户信息
                vp_data::User::ptr upload_user, auditor_user;
                if (user_list.find(video->uploader_uid()) == user_list.end()) {
                    WRN("未找到上传者信息: {}", video->uploader_uid());
                    continue;
                }
                upload_user = user_list[video->uploader_uid()];
                if (video->auditor_uid()) {
                    if (user_list.find(video->auditor_uid().get()) == user_list.end()) {
                        WRN("未找到审核员信息: {}", video->auditor_uid().get());
                        continue;
                    }
                    auditor_user = user_list[video->auditor_uid().get()];
                }
                //获取视频统计数据
                vp_data::VideoCountData vcd(mtx, _redis, _cache_to_db_sync);
                auto count = vcd.getCount(video->video_id());
                if (!count) {
                    WRN("未找到视频统计信息: {}", video->video_id());
                    continue;
                }
                //获取视频分类标签信息
                vp_data::VideoCatgoryTagData vctd(mtx);
                size_t catgory_id = 0;
                std::vector<size_t> tag_ids;
                vctd.getCatgoryTags(video->video_id(), catgory_id, tag_ids);

                api_message::VideoInfo info;
                info.set_videoid(video->video_id());
                info.set_userid(video->uploader_uid());
                if (upload_user->avatar())info.set_useravatarid(upload_user->avatar().get());
                info.set_nickname(upload_user->nickname());
                if (catgory_id != 0) info.set_videotype(catgory_id);
                for (auto &tag_id : tag_ids) { info.add_videotag(tag_id); }
                info.set_videofileid(video->video_fid());
                info.set_photofileid(video->cover_fid());
                info.set_likecount(count->like_count);
                info.set_playcount(count->play_count);
                info.set_videosize(video->size());
                if (video->summary())info.set_videodesc(video->summary().get());
                info.set_videotitle(video->title());
                info.set_videoduration(video->duration());
                info.set_videouptime(boost::posix_time::to_simple_string(video->upload_time()));
                info.set_videostatus((int)video->status());
                if (auditor_user) info.set_checkerid(auditor_user->user_id());
                if (auditor_user && auditor_user->avatar()) info.set_checkeravatar(auditor_user->avatar().get());
                if (auditor_user && auditor_user->bakname()) info.set_checkername(auditor_user->bakname().get());

                result.push_back(info);
            }
            *total = video_list->total;
            
            //8. 提交事务
            mtx.commit();
            return result;
        }catch (const vp_error::VPException &e) {
            ERR("获取状态视频列表失败: {}", e.what());
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
}