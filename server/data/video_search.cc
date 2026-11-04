#include "video_search.h"

namespace vp_data {
    const std::string VideoSearchData::_index_name = "video";
    const float VideoSearchData::_title_boost = 3.0;
    const float VideoSearchData::_desc_boost = 1.0;
    const std::string VideoSearchData::_key_video_id = "videoId";
    const std::string VideoSearchData::_key_video_title = "videoTitle";
    const std::string VideoSearchData::_key_video_desc = "videoDesc";
    const std::string VideoSearchData::_key_video_status = "videoStatus";
    bool VideoSearchData::_enabled = true;
    VideoSearchData::VideoSearchData(const JianLiuIcs::ESClient::ptr &es_client) :_es_client(es_client){}
    void VideoSearchData::init(const JianLiuIcs::ESClient::ptr &es_client) {
        if (!es_client) {
            WRN("Elasticsearch 未配置，搜索功能降级");
            _enabled = false;
            return;
        }
        JianLiuIcs::Indexer indexer(_index_name);
        indexer.tokenizer("ikmax");
        indexer.field(_key_video_id)->type("keyword");
        indexer.field(_key_video_status)->type("boolean");
        auto video_title = indexer.field(_key_video_title);
        video_title->type("text");
        video_title->analyzer("ikmax");
        video_title->boost(3.0);
        auto video_desc = indexer.field(_key_video_desc);
        video_desc->type("text");
        video_desc->analyzer("ikmax");
        video_desc->boost(1.0);
        bool ret = false;
        try {
            ret = es_client->create(indexer);
        } catch (const std::exception &e) {
            WRN("Elasticsearch 连接失败({})，搜索功能降级，其余业务不受影响", e.what());
            _enabled = false;
            return;
        }
        if (ret == false) {
            WRN("创建视频检索索引失败，搜索功能降级，其余业务不受影响");
            _enabled = false;
            return;
        }
        DBG("创建视频搜索索引成功!");
    }
    void VideoSearchData::insert(const std::string &vid, const std::string &title, const std::string &desc, bool status) {
        if (!_enabled) { return; }   // ES 不可用时降级
        JianLiuIcs::Inserter inserter(_index_name, vid);
        inserter.add(_key_video_id, vid);
        inserter.add(_key_video_title, title);
        inserter.add(_key_video_desc, desc);
        inserter.add(_key_video_status, status);
        auto res = _es_client->insert(inserter);
        if (res == false) {
            ERR("向ES中新增视频信息失败!");
            throw vp_error::VPException(vp_error::VpError::ELASTIC_OP_ERROR);
        }
    }
    void VideoSearchData::update(const std::string &vid, const std::string &title, const std::string &desc, bool status) {
        if (!_enabled) { return; }   // ES 不可用时降级
        JianLiuIcs::Inserter inserter(_index_name, vid);
        inserter.add(_key_video_id, vid);
        inserter.add(_key_video_title, title);
        inserter.add(_key_video_desc, desc);
        inserter.add(_key_video_status, status);
        auto res = _es_client->insert(inserter);
        if (res == false) {
            ERR("向ES中更新视频信息失败!");
            throw vp_error::VPException(vp_error::VpError::ELASTIC_OP_ERROR);
        }
    }
    void VideoSearchData::remove(const std::string &vid) {
        if (!_enabled) { return; }   // ES 不可用时降级
        JianLiuIcs::Deleter deleter(_index_name, vid);
        auto res = _es_client->remove(deleter);
        if (res == false) {
            WRN("从ES中删除视频信息失败!");
        }
    }
    void VideoSearchData::remove() {
        if (!_enabled) { return; }   // ES 不可用时降级
        auto res = _es_client->remove(_index_name);
        if (res == false) {
            WRN("从ES中删除视频信息索引失败!");
        }
    }
    std::vector<std::string> VideoSearchData::search(const std::string &key, size_t page, size_t count) {
        if (!_enabled) { return {}; }   // ES 不可用时降级：返回空结果
        JianLiuIcs::Searcher searcher(_index_name);
        auto query = searcher.query();
        auto must = query->must();
        auto mm = query->must()->multi_match();
        mm->appendField(_key_video_title);
        mm->appendField(_key_video_desc);
        mm->setQuery(key);
        query->must_not()->term(_key_video_status)->setValue(false);
        searcher.from(page * count);
        searcher.size(count);
        auto res = _es_client->search(searcher);
        if (!res) {
            ERR("从ES中搜索视频信息失败!");
            throw vp_error::VPException(vp_error::VpError::ELASTIC_OP_ERROR);
        }
        std::vector<std::string> result;
        if (!res || (*res).isNull() == true) {
            if (!res) {
                ERR("从ES中搜索视频信息失败!");
            }else {
                ERR("检索结果为空: {}", *JianLiuUtil::JSON::serialize(*res));
            }
            return result;
        }
        if ((*res).isArray() == false) {
            throw vp_error::VPException(vp_error::VpError::ELASTIC_OP_ERROR);
        }
        const Json::Value &videos = *res;
        int sz = videos.size();
        for (int i = 0; i < sz; i++) {
            result.push_back(videos[i][_key_video_id].asString());
        }
        return result;
    }
}