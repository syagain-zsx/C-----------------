#include "svc_rpc.h"

namespace svc_user {
    UserServiceImpl::UserServiceImpl(const SvcData::ptr &svc_data, 
        const JianLiuMq::Publisher::ptr &publisher,
        const JianLiuCode::MailClient::ptr &mail_client) 
        : _svc_data(svc_data), _publisher(publisher), _mail_client(mail_client){}
    void UserServiceImpl::tempLogin(::google::protobuf::RpcController* controller,
        const ::api_message::TempLoginReq* request,
        ::api_message::TempLoginRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素: 请求ID
        const std::string &rid = request->requestid();
        try {
            //3. 为客户端新建临时会话.并获取会话ID
            auto ssid = _svc_data->tempLogin();
            //4. 组织响应,将会话ID,响应给客户端.
            auto result = response->mutable_result();
            result->set_sessionid(ssid);
        }catch (const vp_error::VPException &e) {
            ERR("会话登录数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("会话登录数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("临时登录处理完毕: {}", rid);
    }
    void UserServiceImpl::sessionLogin(::google::protobuf::RpcController* controller,
        const ::api_message::SessionLoginReq* request,
        ::api_message::SessionLoginRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素: 请求ID, 会话ID
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        try {
            // 获取会话信息,获取会话是否是临时会话的判断结果
            bool ret = _svc_data->sessionLogin(ssid);
            auto result = response->mutable_result();
            result->set_isguest(ret);
        }catch (const vp_error::VPException &e) {
            ERR("会话登录数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("会话登录数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("会话登录处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::getCode(::google::protobuf::RpcController* controller,
        const ::api_message::GetCodeReq* request,
        ::api_message::GetCodeRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素: 请求ID, 会话ID, 邮箱地址
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        const std::string &mail = request->email();
        try {
            //生成验证码ID & 验证码, 添加缓存管理
            std::string code_id, code;
            _svc_data->getVCode(ssid, code_id, code, mail);
            //通过邮箱客户端向用户推送验证码
            bool ret = _mail_client->send(mail, code);
            if (ret == false) {
                throw vp_error::VPException(vp_error::VpError::MAIL_CODE_ERROR);
            }
            //组织响应:向响应中填充验证码ID
            auto result = response->mutable_result();
            result->set_codeid(code_id);
        }catch (const vp_error::VPException &e) {
            ERR("获取验证码数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("获取验证码数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("获取验证码处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::vcodeLogin(::google::protobuf::RpcController* controller,
        const ::api_message::CodeLoginReq* request,
        ::api_message::CodeLoginRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 邮箱, 验证码ID, 验证码
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const std::string &mail = request->email();
        const std::string &code = request->verifycode();
        const std::string &code_id = request->codeid();
		try {
			//数据处理
            _svc_data->regOrLogin(ssid, code_id, code, mail);
		}catch (const vp_error::VPException &e) {
			ERR("验证码注册登录数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("验证码注册登录数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("验证码注册登录处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::passwdLogin(::google::protobuf::RpcController* controller,
        const ::api_message::PasswordLoginReq* request,
        ::api_message::PasswordLoginRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 用户名, 密码
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
		const std::string &name = request->username();
		const std::string &pass = request->password();
		try {
			_svc_data->normalLogin(ssid, name, pass);
		}catch (const vp_error::VPException &e) {
			ERR("密码登录数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("密码登录数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("密码登录处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::logout(::google::protobuf::RpcController* controller,
        const ::api_message::LogoutReq* request,
        ::api_message::LogoutRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
		try {
			auto new_ssid = _svc_data->logout(ssid);
            auto result = response->mutable_result();
            result->set_sessionid(new_ssid);
		}catch (const vp_error::VPException &e) {
			ERR("退出登录数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("退出登录数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("退出登录处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::setAvatar(::google::protobuf::RpcController* controller,
        const ::api_message::SetAvatarReq* request,
        ::api_message::SetAvatarRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 头像文件ID
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const std::string &file_id = request->fileid();
		try {
			//1. 数据处理
            auto old_avatar_fid = _svc_data->setAvatar(ssid, file_id);
            //2. 发布文件删除消息
            if (old_avatar_fid) {
                api_message::DeleteFileMsg message;
                message.add_file_id(*old_avatar_fid);
                bool ret = _publisher->publish(message.SerializeAsString());
                if (ret == false) {
                    throw vp_error::VPException(vp_error::VpError::MQ_OP_FAILED);
                }
            }
		}catch (const vp_error::VPException &e) {
			ERR("设置头像数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("设置头像数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("设置头像处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::setNickname(::google::protobuf::RpcController* controller,
        const ::api_message::SetNicknameReq* request,
        ::api_message::SetNicknameRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID , 昵称
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const std::string &name = request->nickname();
		try {
			_svc_data->setNickname(ssid, name);
		}catch (const vp_error::VPException &e) {
			ERR("设置昵称数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("设置昵称数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("设置昵称处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::setPassword(::google::protobuf::RpcController* controller,
        const ::api_message::SetPasswordReq* request,
        ::api_message::SetPasswordRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID , 密码
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const std::string &pass = request->password();
		try {
			_svc_data->setPassword(ssid, pass);
		}catch (const vp_error::VPException &e) {
			ERR("设置密码数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("设置密码数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("设置密码处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::setStatus(::google::protobuf::RpcController* controller,
        const ::api_message::SetUserStatusReq* request,
        ::api_message::SetUserStatusRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID , 用户ID, 状态
		const auto &rid = request->requestid();
		const auto &ssid = request->sessionid();
        const auto &uid = request->userid();
        const auto status = (vp_data::UserStatus)request->userstatus();
		try {
			_svc_data->setStatus(ssid, uid, status);
		}catch (const vp_error::VPException &e) {
			ERR("设置状态数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("设置状态数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("设置状态处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::getUserInfo(::google::protobuf::RpcController* controller,
        const ::api_message::GetUserInfoReq* request,
        ::api_message::GetUserInfoRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 用户ID(有可能不存在)
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        std::optional<std::string> uid;
        if (request->has_userid()) {
            uid = request->userid();
        }
		try {
            //1. 数据处理
            auto user = _svc_data->getUser(ssid, uid);
            //2. 组织响应结果
			auto result = response->mutable_result();
            auto user_info = result->mutable_userinfo();
            user_info->set_userid(user->user->user_id());
            user_info->set_email(user->user->email());
            user_info->set_nickname(user->user->nickname());
            user_info->set_userstatus((int)user->user->status());
            user_info->set_userctime(boost::posix_time::to_simple_string(user->user->reg_time()));
            if (user->user->remark()) user_info->set_usermemo(user->user->remark().get());
            if (user->user->avatar()) user_info->set_avatarfileid(user->user->avatar().get());
            for (auto uir : user->uirs) {
                user_info->add_roletype((int)uir.role_type());
                user_info->add_identifytype((int)uir.identify_type());
            }
            user_info->set_likecount(user->count->vlike_count);
            user_info->set_playcount(user->count->vplay_count);
            user_info->set_followedcount(user->count->followed_count);
            user_info->set_followercount(user->count->follower_count);
            if (user->isFollowing) user_info->set_isfollowing(*(user->isFollowing)); // 存在该字段才设置,不存在就不设置
		}catch (const vp_error::VPException &e) {
			ERR("获取用户信息数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("获取用户信息数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("获取用户信息处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::newAttention(::google::protobuf::RpcController* controller,
        const ::api_message::NewFollowReq* request,
        ::api_message::NewFollowRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 用户ID
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const std::string &uid = request->userid();
		try {
			_svc_data->newFollow(ssid, uid);
		}catch (const vp_error::VPException &e) {
			ERR("新增关注数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("新增关注数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("新增关注处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::delAttention(::google::protobuf::RpcController* controller,
        const ::api_message::DelFollowReq* request,
        ::api_message::DelFollowRsp* response,
        ::google::protobuf::Closure* done)  {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素: 请求ID, 会话ID, 用户ID
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        const std::string &uid = request->userid();
        try {
            _svc_data->delFollow(ssid, uid);
        }catch (const vp_error::VPException &e) {
            ERR("取消关注数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("取消关注数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("取消关注处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::newAdministrator(::google::protobuf::RpcController* controller,
        const ::api_message::NewAdminReq* request,
        ::api_message::NewAdminRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 管理员信息
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &admin_info = request->userinfo();
		try {
			_svc_data->newAdmin(ssid, admin_info);
		}catch (const vp_error::VPException &e) {
			ERR("新增管理员数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("新增管理员数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("新增管理员处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::delAdministrator(::google::protobuf::RpcController* controller,
        const ::api_message::DelAdminReq* request,
        ::api_message::DelAdminRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
		const std::string &uid = request->userid();
		try {
			_svc_data->delAdmin(ssid, uid);
		}catch (const vp_error::VPException &e) {
			ERR("删除管理员数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("删除管理员数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("删除管理员处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::setAdministrator(::google::protobuf::RpcController* controller,
        const ::api_message::SetAdminReq* request,
        ::api_message::SetAdminRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 管理员信息
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &admin_info = request->userinfo();
		try {
			_svc_data->setAdmin(ssid, admin_info);
		}catch (const vp_error::VPException &e) {
			ERR("编辑管理员数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("编辑管理员数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("编辑管理员处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::getAdminByEmail(::google::protobuf::RpcController* controller,
        const ::api_message::GetAdminReq* request,
        ::api_message::GetAdminRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 邮箱
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const std::string &email = request->email();
		try {
			auto user = _svc_data->getAdmin(ssid, email);
            auto result = response->mutable_result();
            auto admin_info = result->mutable_userinfo();
            admin_info->set_userid(user->user->user_id());
            admin_info->set_nickname(user->user->bakname().get()); //管理员昵称获取的是备用昵称
            admin_info->set_email(user->user->email());
            admin_info->set_roletype((int)user->role_type);
            admin_info->set_userstatus((int)user->user->status());
            if (user->user->remark()) admin_info->set_usermemo(user->user->remark().get());
		}catch (const vp_error::VPException &e) {
			ERR("通过邮箱获取管理员信息数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("通过邮箱获取管理员信息数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("通过邮箱获取管理员信息处理完毕: {}-{}", rid, ssid);
    }
    void UserServiceImpl::getAdminListByStatus(::google::protobuf::RpcController* controller,
        const ::api_message::GetAdminListReq* request,
        ::api_message::GetAdminListRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 使用done管理起,对done进行管理: 在函数结束时调用done->Run(); -表示rpc处理完毕
		brpc::ClosureGuard done_guard(done);
		vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素: 请求ID, 会话ID, 用户状态,页码,每页数量
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        vp_data::UserStatus status = (vp_data::UserStatus)request->userstatus();
        size_t page = request->pageindex() > 0 ? request->pageindex() - 1 : 0; //默认应该从第0页开始;但是有可能客户端是从第1页开始算的
        size_t count = request->pagecount();
        DBG("获取管理员列表: {} {}", page, count);
		try {
			//1. 数据处理
            auto list = _svc_data->getAdminList(ssid, status, page, count);
            //2. 组织响应
            auto result = response->mutable_result();
            result->set_totalcount(list->total);
            for (auto &user : list->users) {
                auto admin_info = result->add_userlist();
                admin_info->set_userid(user.user->user_id());
                if (user.user->bakname()) admin_info->set_nickname(user.user->bakname().get()); //管理员昵称获取的是备用昵称
                admin_info->set_email(user.user->email());
                admin_info->set_roletype((int)user.role_type);
                admin_info->set_userstatus((int)user.user->status());
                if (user.user->remark()) admin_info->set_usermemo(user.user->remark().get());
            }
		}catch (const vp_error::VPException &e) {
			ERR("获取管理员列表处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("获取管理员列表数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("获取管理员列表处理完毕: {}-{}", rid, ssid);
    }
}