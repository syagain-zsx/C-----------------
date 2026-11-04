#include "svc_data.h"


namespace svc_file {
    SvcData::SvcData(const std::shared_ptr<sw::redis::Redis> &redis,
        const std::shared_ptr<odb::database> &mysql,
        const vp_sync::CacheSync::ptr &cache_sync)
        : _redis(redis)
        , _mysql(mysql)
        , _cache_sync(cache_sync) {}
    std::string SvcData::getSessionUser(const std::string &session_id) {
        std::string res;
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData tbl_session(mtx, _redis, _cache_sync);
            auto ss = tbl_session.selectBySid(session_id);
            mtx.commit();
            if (!ss || !ss->user_id()) {
                throw vp_error::VPException(vp_error::VpError::INVALID_SESSION);
            }
            res = *ss->user_id();
        }catch(const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
        return res;
    }
    void SvcData::newFileMeta(vp_data::File &file) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::FileData tbl_file(mtx);
            tbl_file.insert(file);
            mtx.commit();
        }catch (std::exception &e) {
            ERR("新增文件信息异常: {}", e.what());
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
    }
    void SvcData::removeFileMeta(const std::string &file_id) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::FileData tbl_file(mtx);
            tbl_file.remove(file_id);
            mtx.commit();
        } catch (std::exception &e) {
            ERR("删除文件信息异常: {} - {}", e.what(), file_id);
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
    }
    vp_data::File::ptr SvcData::getFileMeta(const std::string &file_id) {
        vp_data::File::ptr file;
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::FileData tbl_file(mtx);
            file = tbl_file.selectById(file_id);
            mtx.commit();
            if (!file) {
                throw vp_error::VPException(vp_error::VpError::FILE_NOT_EXIST);
            }
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (std::exception &e) {
            ERR("查询文件信息异常: {} - {}", e.what(), file_id);
            throw vp_error::VPException(vp_error::VpError::DATABASE_OP_FAILED);
        }
        return file;
    }
}