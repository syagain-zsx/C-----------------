#include "video_idlist.h"
#include <message.pb.h>

namespace vp_data{
    
    const size_t VideoIdListData::_list_cache_size = 1000;
    const size_t VideoIdListData::_expire_time = 600;
    const std::string VideoIdListData::_key_main_page_list = "main_page_list";
    const std::string VideoIdListData::_key_user_page_list_prefix = "user_page_list_";
    const std::string VideoIdListData::_key_catgory_page_list_prefix = "catgory_page_list_";
    const std::string VideoIdListData::_key_tag_page_list_prefix = "tag_page_list_";
    VideoIdListData::VideoIdListData(odb::transaction &mtx, const std::shared_ptr<sw::redis::Redis> &redis, const vp_sync::CacheSync::ptr &sync)
        : _mysql(mtx.database()), _redis(redis), _cache_sync(sync) {}
    std::vector<std::string> VideoIdListData::getMainPageList(size_t page, size_t count) {
        std::vector<std::string> result;
        // 1. 从缓存获取视频ID列表：    
        result = getMainPageListFromCache(page, count);
        //  1-获取到了足量数据；  则直接返回数据
        if (result.size() == count) return result;
        //  0- 缓存未命中；2-获取的数据不足量   -》 从数据库获取数据
        
        // 2. 从数据库获取视频ID列表： 如果时未命中则添加缓存
        if (result.size() == 0 && count <= _list_cache_size) {
            auto list = getMainPageListFromDB(); //获取1000条数据
            //缓存未命中，则获取1000条数据添加缓存
            insertMainPageListToCache(list);
            //组织要返回的ID，需要注意的时list中可能有1000条数据，人家获取的只是其中的区间ID即可
            for (size_t i = page * count; i < (page+1) * count && i < list.size(); i++) {
                result.push_back(list[i].video_id);
            }
        }else {
            //不需要添加缓存，直接组织数据进行返回即可
            auto list = getMainPageListFromDB(page, count); //获取指定区间数据
            //组织要返回的ID
            result.clear(); //清空一下返回列表，防止数据重复
            for (size_t i = 0; i < count && i < list.size(); i++) {
                result.push_back(list[i].video_id);
            }
        }
        // 3. 返回数据
        return result;
    }
    //缓存数据量小于1000，或者视频ID在列表中
    void VideoIdListData::syncMainPageCache(const std::string &video_id) {
        auto rtx = _redis->transaction(false, false);
        auto redis = rtx.redis();
        // 1. 获取缓存key
        std::string key = getMainPageKey();
        // 2. 判断视频ID，是否则当前的列表缓存中； 若在则需要同步删除缓存后返回
        auto score = redis.zscore(key, video_id);
        if (score)  return syncCache(key);
        // 3. 从缓存获取数据量，判断是否小于1000； 若小于1000则需要同步删除缓存后返回
        auto size = redis.zcard(key);
        if (size < _list_cache_size) return syncCache(key);
    }

    std::string VideoIdListData::getMainPageKey() {
        return _key_main_page_list;
    }
    std::string VideoIdListData::getUserPageKey(const std::string &uid) {
        return _key_user_page_list_prefix + uid;
    }
    std::string VideoIdListData::getCatgoryPageKey(size_t catgory_type) {
        return _key_catgory_page_list_prefix + std::to_string(catgory_type);
    }
    std::string VideoIdListData::getTagPageKey(size_t tag_type) {
        return _key_tag_page_list_prefix + std::to_string(tag_type);
    }
    //删除缓存，并发布延迟删除消息
    void VideoIdListData::syncCache(const std::string &key) {
        try {
            auto rtx = _redis->transaction(false, false);
            auto redis = rtx.redis();
            redis.del(key);
            _cache_sync->sync(key);
        }catch (const sw::redis::Error &e) {
            ERR("同步缓存数据失败: {}-{}", key, e.what());
            throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
        }
    }
    std::vector<VideoMainIdList> VideoIdListData::getMainPageListFromDB() {
        //获取默认1000条数据
        std::vector<VideoMainIdList> return_val;
        try {
            typedef odb::query<VideoMainIdList> Query;
            typedef odb::result<VideoMainIdList> Result;
            Query cond(Query::status == VideoStatus::ONLINE);
            auto limit = DESC_LIMIT_PAGE(0, _list_cache_size); //从偏移0开始获取1000条数据
            Result result(_mysql.query<VideoMainIdList>(cond + limit));
            for (auto &r : result) {
                return_val.push_back(r);
            }
        }catch (const odb::exception &e) {
            ERR("从数据库获取全量主页视频ID列表失败: {}", e.what());
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
        return return_val;
    }
    void VideoIdListData::insertMainPageListToCache(const std::vector<VideoMainIdList> &list) {
        // 1. 获取缓存key
        std::string key = getMainPageKey();
        try {
            // 2. 组织数据
            std::unordered_map<std::string, double> video_info;
            for (auto &r : list) {
                video_info[r.video_id] = (size_t)boost::posix_time::to_time_t(r.order_field);
            }
            // 3. 添加缓存
            if (video_info.empty() == false) {
                auto rtx = _redis->transaction(false, false);
                auto redis = rtx.redis();
                redis.zadd(key, video_info.begin(), video_info.end());
                // 4. 设置过期时间
                redis.expire(key, _expire_time + JianLiuUtil::Random::number(0, _expire_time));
            }
        }catch (const sw::redis::Error &e) {
            ERR("向缓存添加主页视频列表失败: {}-{}", key, e.what());
            throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
        }
        
    }
    std::vector<VideoMainIdList> VideoIdListData::getMainPageListFromDB(size_t page, size_t count) {
        //获取指定区间数据
        std::vector<VideoMainIdList> return_val;
        try {
            typedef odb::query<VideoMainIdList> Query;
            typedef odb::result<VideoMainIdList> Result;
            Query cond(Query::status == VideoStatus::ONLINE);
            auto limit = DESC_LIMIT_PAGE(page, count); //获取区间数据
            Result result(_mysql.query<VideoMainIdList>(cond + limit));
            for (auto &r : result) {
                return_val.push_back(r);
            }
        }catch (const odb::exception &e) {
            ERR("从数据库获取区间主页视频ID列表失败: {}", e.what());
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
        return return_val;
    }
    std::vector<std::string> VideoIdListData::getMainPageListFromCache(size_t page, size_t count) {
        std::vector<std::string> return_val;
        //1. 获取缓存key
        std::string key = getMainPageKey();
        try {
            auto rtx = _redis->transaction(false, false);
            auto redis = rtx.redis();
            //2. 从缓存获取数据:以降序获取
            redis.zrevrange(key, page * count, (page+1) * count - 1, std::back_inserter(return_val));
        }catch (const sw::redis::Error &e) {
            ERR("从缓存获取主页视频列表失败: {}-{}", key, e.what());
            throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
        }
        return return_val;
    }

    
    VideoUserStatusIdList::ptr VideoIdListData::getUserPageList(const std::string &uid, VideoStatus status, size_t page, size_t count) {
        VideoUserStatusIdList::ptr result = std::make_shared<VideoUserStatusIdList>();
        // 1. 从缓存获取视频ID列表：    获取的时上架视频列表时从缓存获取
        result->total = getUserPageListCountFromDB(uid, status);
        if (status == VideoStatus::ONLINE) {
            result->videos = getUserPageListFromCache(uid, page, count);
            //  1-获取到了足量数据；  则直接返回数据
            if (result->videos.size() != 0) return result;
            //  0- 缓存未命中；2-获取的数据不足量   -》 从数据库获取数据
        }
        // 2. 从数据库获取视频ID列表： 如果时未命中则添加缓存
        auto list = getUserPageListFromDB(uid, status); 
        //缓存未命中，则获取用户所有数据添加缓存，  缓存中只存放上架状态的视频ID列表
        if (status == VideoStatus::ONLINE) insertUserPageListToCache(uid, list);
        //组织要返回的ID，需要注意的时list中可能有1000条数据，人家获取的只是其中的区间ID即可
        for (size_t i = page * count; i < (page+1) * count && i < list.size(); i++) {
            result->videos.push_back(list[i].video_id);
        }
        // 3. 返回数据
        return result;
        
    }
    std::vector<std::string> VideoIdListData::getCatgoryPageList(size_t catgory_type, size_t page, size_t count) {
        std::vector<std::string> result;
        // 1. 从缓存获取视频ID列表：    
        result = getCatgoryPageListFromCache(catgory_type, page, count);
        //  1-获取到了足量数据；  则直接返回数据
        if (result.size() == count) return result;
        //  0- 缓存未命中；2-获取的数据不足量   -》 从数据库获取数据
        
        // 2. 从数据库获取视频ID列表： 如果时未命中则添加缓存
        if (result.size() == 0 && count <= _list_cache_size) {
            auto list = getCatgoryPageListFromDB(catgory_type); //获取1000条数据
            // for (int i = 0; i < list.size(); i++) {
            //     DBG("获取到的视频ID: {} - {}", i, list[i].video_id);
            // }
            //缓存未命中，则获取1000条数据添加缓存
            insertCatgoryPageListToCache(catgory_type, list);
            //组织要返回的ID，需要注意的时list中可能有1000条数据，人家获取的只是其中的区间ID即可
            for (size_t i = page * count; i < (page+1) * count && i < list.size(); i++) {
                // DBG("获取到的视频ID: {} - {}", i, list[i].video_id);
                result.push_back(list[i].video_id);
            }
        }else {
            //不需要添加缓存，直接组织数据进行返回即可
            auto list = getCatgoryPageListFromDB(catgory_type, page, count); //获取指定区间数据
            //组织要返回的ID
            result.clear(); //清空一下返回列表，防止数据重复
            for (size_t i = 0; i < count && i < list.size(); i++) {
                result.push_back(list[i].video_id);
            }
        }
        // 3. 返回数据
        return result;
    }
    std::vector<std::string> VideoIdListData::getTagPageList(size_t tag_type, size_t page, size_t count) {
        std::vector<std::string> result;
        // 1. 从缓存获取视频ID列表：    
        result = getTagPageListFromCache(tag_type, page, count);
        //  1-获取到了足量数据；  则直接返回数据
        if (result.size() == count) return result;
        //  0- 缓存未命中；2-获取的数据不足量   -》 从数据库获取数据
        
        // 2. 从数据库获取视频ID列表： 如果时未命中则添加缓存
        if (result.size() == 0 && count <= _list_cache_size) {
            auto list = getTagPageListFromDB(tag_type); //获取1000条数据
            //缓存未命中，则获取1000条数据添加缓存
            insertTagPageListToCache(tag_type, list);
            //组织要返回的ID，需要注意的时list中可能有1000条数据，人家获取的只是其中的区间ID即可
            for (size_t i = page * count; i < (page+1) * count && i < list.size(); i++) {
                result.push_back(list[i].video_id);
            }
        }else {
            //不需要添加缓存，直接组织数据进行返回即可
            auto list = getTagPageListFromDB(tag_type, page, count); //获取指定区间数据
            //组织要返回的ID
            result.clear(); //清空一下返回列表，防止数据重复
            for (size_t i = 0; i < count && i < list.size(); i++) {
                result.push_back(list[i].video_id);
            }
        }
        // 3. 返回数据
        return result;
    }
    StatusVideoList::ptr VideoIdListData::getStatusPageList(VideoStatus status, size_t page, size_t count) {
        return getStatusPageListFromDB(status, page, count);
    }
    void VideoIdListData::syncPageListCache(const std::string &vid, const std::string &uid, size_t catgory_type, std::vector<size_t> tag_type){
        syncMainPageCache(vid);
        syncUserPageCache(uid, vid);
        syncCatgoryPageCache(catgory_type, vid);
        for (auto &t : tag_type) {
            syncTagPageCache(t, vid);
        }
    }

    void VideoIdListData::syncUserPageCache(const std::string &uid, const std::string &video_id) {
        // 1. 获取缓存key
        std::string key = getUserPageKey(uid);
        return syncCache(key);
    }
    void VideoIdListData::syncCatgoryPageCache(size_t catgory_type, const std::string &video_id) {
        auto rtx = _redis->transaction(false, false);
        auto redis = rtx.redis();
        // 1. 获取缓存key
        std::string key = getCatgoryPageKey(catgory_type);
        // 2. 判断视频ID，是否则当前的列表缓存中； 若在则需要同步删除缓存后返回
        auto score = redis.zscore(key, video_id);
        if (score)  return syncCache(key);
        // 3. 从缓存获取数据量，判断是否小于1000； 若小于1000则需要同步删除缓存后返回
        auto size = redis.zcard(key);
        if (size < _list_cache_size) return syncCache(key);
    }
    void VideoIdListData::syncTagPageCache(size_t tag_type, const std::string &video_id) {
        auto rtx = _redis->transaction(false, false);
        auto redis = rtx.redis();
        // 1. 获取缓存key
        std::string key = getTagPageKey(tag_type);
        // 2. 判断视频ID，是否则当前的列表缓存中； 若在则需要同步删除缓存后返回
        auto score = redis.zscore(key, video_id);
        if (score)  return syncCache(key);
        // 3. 从缓存获取数据量，判断是否小于1000； 若小于1000则需要同步删除缓存后返回
        auto size = redis.zcard(key);
        if (size < _list_cache_size) return syncCache(key);
    }

    std::vector<VideoUserPlayIdList> VideoIdListData::getUserPageListFromDB(const std::string &uid, VideoStatus status) {
        //获取默认1000条数据
        std::vector<VideoUserPlayIdList> return_val;
        try {
            typedef odb::query<VideoUserPlayIdList> Query;
            typedef odb::result<VideoUserPlayIdList> Result;
            // 如果获取的状态时UNKNOWN，则获取非待转码状态的所有视频， 否则就获取指定状态
            Query cond;
            if (status == VideoStatus::UNKNOWN) {
                cond = Query(Query::status != VideoStatus::TRANSCODING && 
                    Query::status != VideoStatus::TRANSCODE_FAILED &&
                    Query::uploader_uid == uid);
            }else {
                cond = Query(Query::status == status && Query::uploader_uid == uid);
            }
            
            // auto limit = DESC_LIMIT_PAGE(0, _list_cache_size); //从偏移0开始获取1000条数据
            Result result(_mysql.query<VideoUserPlayIdList>(cond));
            for (auto &r : result) {
                return_val.push_back(r);
            }
        }catch (const odb::exception &e) {
            ERR("从数据库获取全量用户视频ID列表失败: {}", e.what());
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
        return return_val;
    }
    size_t VideoIdListData::getUserPageListCountFromDB(const std::string &uid, VideoStatus status) {
        try {
            typedef odb::query<VideoUserPlayIdListCount> Query;
            typedef odb::result<VideoUserPlayIdListCount> Result;
            // 如果获取的状态时UNKNOWN，则获取非待转码状态的所有视频， 否则就获取指定状态
            Query cond;
            if (status == VideoStatus::UNKNOWN) {
                cond = Query(Query::status != VideoStatus::TRANSCODING && 
                    Query::status != VideoStatus::TRANSCODE_FAILED &&
                    Query::uploader_uid == uid);
            }else {
                cond = Query(Query::status == status && Query::uploader_uid == uid);
            }
            
            // auto limit = DESC_LIMIT_PAGE(0, _list_cache_size); //从偏移0开始获取1000条数据
            VideoUserPlayIdListCount::ptr result(_mysql.query_one<VideoUserPlayIdListCount>(cond));
            return result? result->count : 0;
        }catch (const odb::exception &e) {
            ERR("从数据库获取全量用户视频ID列表失败: {}", e.what());
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
    }


    std::vector<VideoCatgoryPlayIdList> VideoIdListData::getCatgoryPageListFromDB(size_t catgory_type) {
        //获取默认1000条数据
        return getCatgoryPageListFromDB(catgory_type, 0, _list_cache_size);
    }
    std::vector<VideoCatgoryPlayIdList> VideoIdListData::getCatgoryPageListFromDB(size_t catgory_type, size_t page, size_t count) {
        //获取默认1000条数据
        std::vector<VideoCatgoryPlayIdList> return_val;
        try {
            // _mysql.tracer(odb::stderr_full_tracer);
            typedef odb::query<VideoCatgoryPlayIdList> Query;
            typedef odb::result<VideoCatgoryPlayIdList> Result;
            Query cond(Query::Video::status == VideoStatus::ONLINE && Query::VideoCatgoryTag::catgory_id == catgory_type);
            auto limit = DESC_LIMIT_PAGE(page, count); //从偏移0开始获取1000条数据
            Result result(_mysql.query<VideoCatgoryPlayIdList>(cond + limit));
            for (auto &r : result) {
                return_val.push_back(r);
            }
        }catch (const odb::exception &e) {
            ERR("从数据库获取分类视频ID列表失败: {}", e.what());
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
        return return_val;
    }
    std::vector<VideoTagPlayIdList> VideoIdListData::getTagPageListFromDB(size_t tag_type) {
        return getTagPageListFromDB(tag_type, 0, _list_cache_size);
    }
    std::vector<VideoTagPlayIdList> VideoIdListData::getTagPageListFromDB(size_t tag_type, size_t page, size_t count) {
        //获取默认1000条数据
        std::vector<VideoTagPlayIdList> return_val;
        try {
            typedef odb::query<VideoTagPlayIdList> Query;
            typedef odb::result<VideoTagPlayIdList> Result;
            Query cond(Query::Video::status == VideoStatus::ONLINE && Query::VideoCatgoryTag::tag_id == tag_type);
            auto limit = DESC_LIMIT_PAGE(page, count); //从偏移0开始获取1000条数据
            Result result(_mysql.query<VideoTagPlayIdList>(cond + limit));
            for (auto &r : result) {
                return_val.push_back(r);
            }
        }catch (const odb::exception &e) {
            ERR("从数据库获取标签视频ID列表失败: {}", e.what());
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
        return return_val;
    }
    StatusVideoList::ptr VideoIdListData::getStatusPageListFromDB(VideoStatus status, size_t page, size_t count) {
        //获取默认1000条数据
        StatusVideoList::ptr return_val;
        try {
            //获取视频信息列表
            typedef odb::query<VideoStatusIdList> Query;
            typedef odb::result<VideoStatusIdList> Result;
            Query cond;
            if (status != VideoStatus::UNKNOWN) {
                cond = Query(Query::status == status);
            }
            auto limit = ASC_LIMIT_PAGE(page, count); //从偏移0开始获取1000条数据
            Result result(_mysql.query<VideoStatusIdList>(cond + limit));

            //获取指定状态视频总量
            typedef odb::query<VideoStatusIdList> CountQuery;
            VideoStatusCount::ptr count_result(_mysql.query_one<VideoStatusCount>(cond));

            return_val = std::make_shared<StatusVideoList>();
            if (count_result) return_val->total = count_result->count;
            for (auto &r : result) {
                return_val->videos.push_back(r.video);
            }
        }catch (const odb::exception &e) {
            ERR("从数据库获取标签视频ID列表失败: {}", e.what());
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
        return return_val;
    }

    void VideoIdListData::insertUserPageListToCache(const std::string &uid, const std::vector<VideoUserPlayIdList> &list) {
         // 1. 获取缓存key
         std::string key = getUserPageKey(uid);
         try {
             // 2. 组织数据
             std::unordered_map<std::string, double> video_info;
             for (auto &r : list) {
                 video_info[r.video_id] = r.order_field;
             }
             // 3. 添加缓存
             
            if (video_info.empty() == false) {
                auto rtx = _redis->transaction(false, false);
                auto redis = rtx.redis();
                redis.zadd(key, video_info.begin(), video_info.end());
                // 4. 设置过期时间
                redis.expire(key, _expire_time + JianLiuUtil::Random::number(0, _expire_time));
            }
         }catch (const sw::redis::Error &e) {
             ERR("向缓存添加用户视频列表失败: {}-{}", key, e.what());
             throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
         }
    }
    void VideoIdListData::insertCatgoryPageListToCache(size_t catgory_type, const std::vector<VideoCatgoryPlayIdList> &list) {
        // 1. 获取缓存key
        std::string key = getCatgoryPageKey(catgory_type);
        try {
            // 2. 组织数据
            std::unordered_map<std::string, double> video_info;
            for (auto &r : list) {
                video_info[r.video_id] = r.order_field;
            }
            
            if (video_info.empty() == false) {
                auto rtx = _redis->transaction(false, false);
                auto redis = rtx.redis();
                // 3. 添加缓存
                redis.zadd(key, video_info.begin(), video_info.end());
                // 4. 设置过期时间
                redis.expire(key, _expire_time + JianLiuUtil::Random::number(0, _expire_time));
            }
        }catch (const sw::redis::Error &e) {
            ERR("向缓存添加分类视频列表失败: {}-{}", key, e.what());
            throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
        }
    }
    void VideoIdListData::insertTagPageListToCache(size_t tag_type, const std::vector<VideoTagPlayIdList> &list) {
        // 1. 获取缓存key
        std::string key = getTagPageKey(tag_type);
        try {
            // 2. 组织数据
            std::unordered_map<std::string, double> video_info;
            for (auto &r : list) {
                video_info[r.video_id] = r.order_field;
            }
            
            if (video_info.empty() == false) {
                auto rtx = _redis->transaction(false, false);
                auto redis = rtx.redis();
                // 3. 添加缓存
                redis.zadd(key, video_info.begin(), video_info.end());
                // 4. 设置过期时间
                redis.expire(key, _expire_time + JianLiuUtil::Random::number(0, _expire_time));
            }
        }catch (const sw::redis::Error &e) {
            ERR("向缓存添加标签视频列表失败: {}-{}", key, e.what());
            throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
        }
    }

    std::vector<std::string> VideoIdListData::getUserPageListFromCache(const std::string &uid, size_t page, size_t count) {
        std::vector<std::string> return_val;
        //1. 获取缓存key
        std::string key = getUserPageKey(uid);
        try {
            auto rtx = _redis->transaction(false, false);
            auto redis = rtx.redis();
            //2. 从缓存获取数据:以降序获取
            redis.zrevrange(key, page * count, (page+1) * count - 1, std::back_inserter(return_val));
        }catch (const sw::redis::Error &e) {
            ERR("从缓存获取用户视频列表失败: {}-{}", key, e.what());
            throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
        }
        return return_val;
    }
    std::vector<std::string> VideoIdListData::getCatgoryPageListFromCache(size_t catgory_type, size_t page, size_t count) {
        std::vector<std::string> return_val;
        //1. 获取缓存key
        std::string key = getCatgoryPageKey(catgory_type);
        try {
            auto rtx = _redis->transaction(false, false);
            auto redis = rtx.redis();
            //2. 从缓存获取数据:以降序获取
            redis.zrevrange(key, page * count, (page+1) * count - 1, std::back_inserter(return_val));
        }catch (const sw::redis::Error &e) {
            ERR("从缓存获取分类视频列表失败: {}-{}", key, e.what());
            throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
        }
        return return_val;
    }
    std::vector<std::string> VideoIdListData::getTagPageListFromCache(size_t tag_type, size_t page, size_t count) {
        std::vector<std::string> return_val;
        //1. 获取缓存key
        std::string key = getTagPageKey(tag_type);
        try {
            auto rtx = _redis->transaction(false, false);
            auto redis = rtx.redis();
            //2. 从缓存获取数据:以降序获取
            redis.zrevrange(key, page * count, (page+1) * count - 1, std::back_inserter(return_val));
        }catch (const sw::redis::Error &e) {
            ERR("从缓存获取标签视频列表失败: {}-{}", key, e.what());
            throw vp_error::VPException(vp_error::VpError::REDIS_OP_FAILED);
        }
        return return_val;
    }
}