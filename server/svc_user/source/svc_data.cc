#include "svc_data.h"

namespace svc_user {
    SvcData::SvcData(const std::shared_ptr<sw::redis::Redis> &redis,
        const std::shared_ptr<odb::database> &mysql,
        const vp_sync::CacheSync::ptr &cache_sync)
        : _redis(redis)
        , _mysql(mysql)
        , _cache_sync(cache_sync) {}
    //临时用户登录数据处理: 生成会话ID,向数据库新增临时会话信息
    std::string SvcData::tempLogin() {
        std::string ssid = JianLiuUtil::Random::code();
        vp_data::Session ss(ssid);
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
            sd.insert(ss);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
        return ssid;
    }
    //会话用户登录: 获取会话信息, 返回会话是否是临时会话
    bool SvcData::sessionLogin(const std::string &ssid) {
        bool result = false;
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
            auto ss = sd.selectBySid(ssid);
            if (!ss) {
                ERR("未找到会话信息: {}", ssid);
                throw vp_error::VPException(vp_error::VpError::SESSION_INVALID);
            }
            mtx.commit();
            if (ss->user_id()) return false; //表示当前会话是一个临时会话
            return true;
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //获取验证码: 生成验证码和验证码ID,添加验证码信息缓存; 返回验证码ID
    void SvcData::getVCode(const std::string &ssid, std::string &code_id, std::string &code, const std::string &email) {
        code_id = JianLiuUtil::Random::code();
        code = JianLiuUtil::Random::code(_code_length, JianLiuUtil::UuidType::DIGIT);
        try {
            vp_data::VerifyCodeData vcd(_redis);
            vcd.insert(code_id, ssid, code, email);
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //注册或登录: 获取验证码信息进行比对;  判断email是否已注册; 未注册-新增用户,新增身份信息,更新会话信息; 已注册-更新会话信息
    void SvcData::regOrLogin(const std::string &ssid, const std::string &cid, const std::string &code, const std::string &email) {
        try {
            odb::transaction mtx(_mysql->begin());
            //1. 通过验证码ID获取验证码信息,进行验证码的一致性比对,若错误则抛出异常
            vp_data::VerifyCodeData vcd(_redis);
            auto vc = vcd.get(cid);
            if (!vc || vc->session_id != ssid || vc->code_id != cid || vc->code != code || vc->email != email) {
                ERR("验证信息错误: {}-{}-{}", cid, ssid, code);
                throw vp_error::VPException(vp_error::VpError::VERIFY_CODE_ERROR);
            }
            vcd.remove(cid);
            //2. 判断邮箱是否已注册(未注册则注册+登录 / 已注册则登录)
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            bool ret = ud.isExist(email);
            if (ret == false) {
                //4. 未注册处理: 新增用户信息; 新增用户身份角色;  更新会话信息(有用户ID的会话信息)
                std::string uid = JianLiuUtil::Random::code();
                ud.insert(uid, email);
                vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_sync);
                uird.insert(uid, vp_data::IdentifyType::NORMAL, vp_data::RoleType::USER_NORNAL);
                vp_data::SessionData sd(mtx, _redis, _cache_sync);
                vp_data::Session ss(ssid, uid);
                sd.update(ss);
            }else {
                //3. 已注册处理: 获取会话信息; 若会话已登录则报错; 通过邮箱获取用户信息; 删除用户的所有会话; 更新会话信息(有用户ID的会话信息)
                vp_data::SessionData sd(mtx, _redis, _cache_sync);
                auto ss = sd.selectBySid(ssid);
                if (!ss) {
                    ERR("未找到会话信息: {}", ssid);
                    throw vp_error::VPException(vp_error::VpError::SESSION_INVALID);
                }
                auto uid = ss->user_id();
                if (uid) {
                    // 能进行注册/登录,必然当前会话是一个临时会话,这个会话中不应该有会话ID
                    ERR("当前客户端会话已登录: {}-{}", ssid, *uid);
                    throw vp_error::VPException(vp_error::VpError::SESSION_ALREADY_LOGIN);
                }
                auto user = ud.getUserByEmail(email);
                if (!user) {
                    ERR("没有通过邮箱找到用户信息: {}", email);
                    throw vp_error::VPException(vp_error::VpError::EMAIL_USER_NOT_EXISTS);
                }
                sd.removeByUid(user->user_id()); //删除用户历史会话(不支持多地登录)
                ss->set_user_id(user->user_id());
                sd.update(*ss);
            }
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //用户名登录: 通过用户名获取用户信息; 进行密码比对; 获取用户信息; 删除用户老会话, 更新会话信息
    void SvcData::normalLogin(const std::string &ssid, const std::string &username, const std::string &passwd) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            auto user = ud.verify(username, passwd);
            if (!user) {
                INF("用户名密码验证失败: {}", username);
                throw vp_error::VPException(vp_error::VpError::USERNAME_OR_PASSWD_ERROR);
            }
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
            sd.removeByUid(user->user_id());
            vp_data::Session ss(ssid, user->user_id());
            sd.update(ss);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //退出登录: 获取会话信息; 更新会话
    std::string SvcData::logout(const std::string &ssid) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
            vp_data::Session ss(ssid);
            sd.update(ss);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
        return ssid;
    }
    //修改昵称: 获取会话信息, 更新昵称
    void SvcData::setNickname(const std::string &ssid, const std::string &name) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
            auto ss = sd.selectBySid(ssid);
            if (!ss) {
                ERR("会话信息不存在: {}", ssid);
                throw vp_error::VPException(vp_error::VpError::SESSION_INVALID);
            }
            auto uid = ss->user_id();
            if (!uid) {
                ERR("会话用户不存在: {}", ssid);
                throw vp_error::VPException(vp_error::VpError::SESSION_NOT_LOGIN);
            }
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            ud.updateNickname(*uid, name);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //修改密码: 获取会话信息, 更新密码
    void SvcData::setPassword(const std::string &ssid, const std::string &passwd)  {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
            auto ss = sd.selectBySid(ssid);
            if (!ss) {
                ERR("会话信息不存在: {}", ssid);
                throw vp_error::VPException(vp_error::VpError::SESSION_INVALID);
            }
            auto uid = ss->user_id();
            if (!uid) {
                ERR("会话用户不存在: {}", ssid);
                throw vp_error::VPException(vp_error::VpError::SESSION_NOT_LOGIN);
            }
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            ud.updatePasswd(*uid, passwd);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //修改头像: 获取会话信息, 更新头像
    std::optional<std::string> SvcData::setAvatar(const std::string &ssid, const std::string &avatar){
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
            auto ss = sd.selectBySid(ssid);
            if (!ss) {
                ERR("会话信息不存在: {}", ssid);
                throw vp_error::VPException(vp_error::VpError::SESSION_INVALID);
            }
            auto uid = ss->user_id();
            if (!uid) {
                ERR("会话用户不存在: {}", ssid);
                throw vp_error::VPException(vp_error::VpError::SESSION_NOT_LOGIN);
            }
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            auto old_avatar = ud.updateAvatar(*uid, avatar);
            if (!old_avatar) {
                INF("更新用户头像: {} - {}", uid, avatar);
            }else {
                INF("更新用户头像: {} - {} - {}", uid, avatar, *old_avatar);
            }
            mtx.commit();
            return old_avatar;
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //修改状态: 获取会话信息; 获取会话用户身份角色判断是否管理员,以及是否启用; 获取目标用户身份角色; 修改用户状态
    void SvcData::setStatus(const std::string &ssid, const std::string &uid, vp_data::UserStatus status) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_sync);
            bool ret = uird.hasIdentify(*cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            ret = ud.isEnable(*cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //获取目标用户的身份角色信息-- 用于判断当前会话用户是管理员,而目标用户是超管
            auto dest_uirs = uird.list(uid);
            if (uird.hasRole(*cur_uid, vp_data::RoleType::ADMIN_NORMAL) && uird.hasRole(uid, vp_data::RoleType::ADMIN_SUPER)) {
                ERR("普管无法修改超管用户信息: {}-{}-{}", ssid, *cur_uid, uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            // 用户无法修改自身状态
            // if (*cur_uid == uid && !ud.hasStatus(uid, status)) {
            if (*cur_uid == uid) {
                ERR("用户无法修改自身状态: {}-{}", ssid, uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            ud.updateStatus(uid, status);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //获取用户详细信息: 获取会话信息; 获取目标用户基础用户信息,获取目标用户身份角色;获取目标用户统计数据;判断会话用户是否关注了目标用户
    vp_data::DetailUser::ptr SvcData::getUser(const std::string &ssid, const std::optional<std::string> &uid) {
        vp_data::DetailUser::ptr du;
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            std::string dst_uid;
            if (uid) dst_uid = *uid; //目标用户ID不为空,则设置目标用户ID
            else dst_uid = *cur_uid; //目标用户ID为空,则目标用户就是当前会话用户自己
            du = std::make_shared<vp_data::DetailUser>();
            //获取用户基础信息
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            du->user = ud.getUser(dst_uid);
            if (!du->user) {
                ERR("未找到目标用户信息: {}:{}", ssid, dst_uid);
                throw vp_error::VPException(vp_error::VpError::USER_NOT_EXIST);
            }
            //获取用户统计数据
            vp_data::UserCountData ucd(mtx, _redis);
            du->count = ucd.getCount(dst_uid);
            if (!du->count) {
                ERR("未找到用户统计数据: {}", dst_uid);
                throw vp_error::VPException(vp_error::VpError::USER_COUNT_NOT_EXISTS);
            }
            //获取用户身份角色信息
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_sync);
            du->uirs = uird.list(dst_uid);
            if (du->uirs.empty()) {
                ERR("未找到用户身份角色信息: {}", dst_uid);
                throw vp_error::VPException(vp_error::VpError::USER_IDENTIFY_ROLE_NOT_EXISTS);
            }
            //如果目标用户ID和当前用户ID不一样,则获取当前用户是否关注了目标用户的判断结果
            if (dst_uid != *cur_uid) {
                vp_data::FollowData fd(mtx);
                du->isFollowing = fd.judge(*cur_uid, dst_uid);
            }
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
        return du;
    }
    //新增关注: 获取会话信息; 新增关注关系(判断是否已关注,判断目标是否是自己)
    void SvcData::newFollow(const std::string &ssid, const std::string &uid) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            vp_data::FollowData fd(mtx);
            bool ret = fd.judge(*cur_uid, uid);
            if (ret == true) {
                ERR("当前会话用户已关注目标用户: {}-{}", *cur_uid, uid);
                throw vp_error::VPException(vp_error::VpError::USER_ALREADY_FOLLOWING);
            }
            fd.insert(*cur_uid, uid);
            //调整缓存中当前用户的关注数量，以及目标用户的粉丝数量
            vp_data::UserCountData ucd(mtx, _redis);
            ucd.updateFollowedCount(*cur_uid, 1);
            ucd.updateFollowerCount(uid, 1);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //取消关注: 获取会话信息; 删除关注关系
    void SvcData::delFollow(const std::string &ssid, const std::string &uid) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            vp_data::FollowData fd(mtx);
            bool ret = fd.judge(*cur_uid, uid);
            if (ret == false) {
                ERR("当前会话用户未关注目标用户: {}-{}", *cur_uid, uid);
                throw vp_error::VPException(vp_error::VpError::USER_NOT_FOLLOWING);
            }
            fd.remove(*cur_uid, uid);
            
            //调整缓存中当前用户的关注数量，以及目标用户的粉丝数量
            vp_data::UserCountData ucd(mtx, _redis);
            ucd.updateFollowedCount(*cur_uid, -1);
            ucd.updateFollowerCount(uid, -1);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //新增管理员: 获取会话信息,获取会话用户身份角色(判断是否是管理员,是否启用, 目标与自己的管理等级); 新增管理员信息/更新普通用户信息,添加身份信息
    void SvcData::newAdmin(const std::string &ssid, const api_message::AdminInfo &info) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_sync);
            bool ret = uird.hasIdentify(*cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            ret = ud.isEnable(*cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //获取目标用户的身份角色信息-- 用于判断当前会话用户是管理员,而目标用户是超管
            if (info.roletype() == (int)vp_data::RoleType::ADMIN_SUPER &&
                uird.hasRole(*cur_uid, vp_data::RoleType::ADMIN_NORMAL)) {
                ERR("普管无法新增超管用户信息: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }

            //新增管理员:1-当前管理员邮箱是否已经注册普通用户(未注册-先注册普通用户,然后新增身份角色信息; 已注册-只需要新增用户身份角色信息即可)
            ret = ud.isExist(info.email());
            if (ret == false) {
                std::string uid = JianLiuUtil::Random::code();
                ud.insert(uid, info.email());
                ud.updateAdmin(uid, info.nickname(), info.usermemo(), (vp_data::UserStatus)info.userstatus());
                uird.insert(uid, vp_data::IdentifyType::NORMAL, vp_data::RoleType::USER_NORNAL);
                uird.insert(uid, vp_data::IdentifyType::ADMIN, (vp_data::RoleType)info.roletype());
            }else {
                auto user = ud.getUserByEmail(info.email());
                if (!user) {
                    ERR("新增管理员是未找到邮箱用户: {}", info.email());
                    throw vp_error::VPException(vp_error::VpError::EMAIL_USER_NOT_EXISTS);
                }
                //如果目标用户已经是管理员,则报错
                ret = uird.hasIdentify(user->user_id(), vp_data::IdentifyType::ADMIN);
                if (ret == true) {
                    ERR("目标用户已经有管理员身份: {}-{}", ssid, user->user_id());
                    throw vp_error::VPException(vp_error::VpError::USER_ALREADY_HAS_ADMIN_IDENTIFY);
                }
                uird.insert(user->user_id(), vp_data::IdentifyType::ADMIN, (vp_data::RoleType)info.roletype());
                ud.updateAdmin(user->user_id(), info.nickname(), info.usermemo(), (vp_data::UserStatus)info.userstatus());
            }
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //编辑管理员: 获取会话信息,获取会话用户身份角色(判断是否是管理员,是否启用, 目标与自己的管理等级); 更新管理员信息
    // 如果更新的是自己的信息,则不能修改自己的状态
    void SvcData::setAdmin(const std::string &ssid, const api_message::AdminInfo &info) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_sync);
            bool ret = uird.hasIdentify(*cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            ret = ud.isEnable(*cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //获取目标用户的身份角色信息-- 用于判断当前会话用户是管理员,而目标用户是超管
            if (uird.hasRole(info.userid(), vp_data::RoleType::ADMIN_SUPER) &&
                uird.hasRole(*cur_uid, vp_data::RoleType::ADMIN_NORMAL)) {
                ERR("普管无法修改超管用户信息: {}-{}-{}", ssid, *cur_uid, info.userid());
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //编辑管理员
            if (info.userid() == *cur_uid && info.userstatus() == (int)vp_data::UserStatus::DISABLE) {
                ERR("管理员不能禁用自己: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            ud.updateAdmin(info.userid(), info.nickname(), info.usermemo(), (vp_data::UserStatus)(info.userstatus()));
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //删除管理员: 获取会话信息,获取会话用户身份角色(判断是否是管理员,是否启用, 目标与自己的管理等级); 删除用户身份角色
    void SvcData::delAdmin(const std::string &ssid, const std::string &uid) {
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_sync);
            bool ret = uird.hasIdentify(*cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            ret = ud.isEnable(*cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            //获取目标用户的身份角色信息-- 用于判断当前会话用户是管理员,而目标用户是超管
            if (uird.hasRole(uid, vp_data::RoleType::ADMIN_SUPER) &&
                uird.hasRole(*cur_uid, vp_data::RoleType::ADMIN_NORMAL)) {
                ERR("普管无法删除超管用户信息: {}-{}-{}", ssid, *cur_uid, uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            uird.remove(uid, vp_data::IdentifyType::ADMIN);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
    }
    //获取管理员列表: 获取会话信息(判断是否是管理员,是否启用, 目标与自己的管理等级); 获取管理员列表
    vp_data::StatusIdentifyUserList::ptr SvcData::getAdminList(const std::string &ssid, vp_data::UserStatus status, size_t page, size_t count) {
        vp_data::StatusIdentifyUserList::ptr result;
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_sync);
            bool ret = uird.hasIdentify(*cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            ret = ud.isEnable(*cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            result = ud.getIdentifyUserList(status, vp_data::IdentifyType::ADMIN, page, count);
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
        return result;
    }
    //通过邮箱获取管理员: 获取会话信息(判断是否是管理员,是否启用, 目标与自己的管理等级); 获取管理员信息
    vp_data::StatusIdentifyUser::ptr SvcData::getAdmin(const std::string &ssid, const std::string &email) {
        vp_data::StatusIdentifyUser::ptr result;
        try {
            odb::transaction mtx(_mysql->begin());
            vp_data::SessionData sd(mtx, _redis, _cache_sync);
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
            vp_data::UserIdentifyRoleData uird(mtx, _redis, _cache_sync);
            bool ret = uird.hasIdentify(*cur_uid, vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("当前用户不是管理员: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            vp_data::UserData ud(mtx, _redis, _cache_sync);
            ret = ud.isEnable(*cur_uid);
            if (ret == false) {
                ERR("当前用户不处于启用状态: {}-{}", ssid, *cur_uid);
                throw vp_error::VPException(vp_error::VpError::PERMISSION_DENIED);
            }
            result = std::make_shared<vp_data::StatusIdentifyUser>();
            result->user = ud.getUserByEmail(email);
            if (!result->user) {
                ERR("未找到邮箱对应的管理员用户: {}-{}", ssid, email);
                throw vp_error::VPException(vp_error::VpError::USER_NOT_EXIST);
            }
            // 如果目标用户不是管理员则报错
            ret = uird.hasIdentify(result->user->user_id(), vp_data::IdentifyType::ADMIN);
            if (ret == false) {
                ERR("目标用户已没有管理员身份: {}-{}", ssid, result->user->user_id());
                throw vp_error::VPException(vp_error::VpError::USER_ALREADY_HAS_NOT_ADMIN_IDENTIFY);
            }
            if (uird.hasRole(result->user->user_id(), vp_data::RoleType::ADMIN_SUPER)) {
                result->role_type = vp_data::RoleType::ADMIN_SUPER;
            }else {
                result->role_type = vp_data::RoleType::ADMIN_NORMAL;
            }
            mtx.commit();
        }catch (const vp_error::VPException &e) {
            throw;
        }catch (const std::exception &e) {
            throw vp_error::VPException(vp_error::VpError::SERVER_INERNAL_ERROR);
        }
        return result;
    }
}