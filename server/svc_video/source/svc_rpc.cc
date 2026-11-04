#include "svc_rpc.h"


namespace svc_video {
    VideoServiceImpl::VideoServiceImpl(const SvcData::ptr &data): _data(data) {}
    void VideoServiceImpl::newVideo(::google::protobuf::RpcController* controller,
        const ::api_message::NewVideoReq* request,
        ::api_message::NewVideoRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &video_info = request->videoinfo();
        //3. 处理请求
		try {
			_data->newVideo(ssid, video_info);
		}catch (const vp_error::VPException &e) {
			ERR("新增视频数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("新增视频数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
        //4. 组织响应
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("新增视频处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::removeVideo(::google::protobuf::RpcController* controller,
        const ::api_message::RemoveVideoReq* request,
        ::api_message::RemoveVideoRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &video_id = request->videoid();
        //3. 处理请求
		try {
			_data->delVideo(ssid, video_id);
		}catch (const vp_error::VPException &e) {
			ERR("删除视频数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("删除视频数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
        //4. 组织响应
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("删除视频处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::judgeLike(::google::protobuf::RpcController* controller,
        const ::api_message::JudgeLikeReq* request,
        ::api_message::JudgeLikeRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &video_id = request->videoid();
        //3. 处理请求
		try {
			bool ret = _data->judgeLike(ssid, video_id);
            auto result = response->mutable_result();
            result->set_islike(ret);
		}catch (const vp_error::VPException &e) {
			ERR("点赞判断数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("点赞判断数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
        //4. 组织响应
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("点赞判断处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::setLike(::google::protobuf::RpcController* controller,
        const ::api_message::setLikeReq* request,
        ::api_message::setLikeRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &video_id = request->videoid();
        //3. 处理请求
		try {
			_data->likeVideo(ssid, video_id);
		}catch (const vp_error::VPException &e) {
			ERR("点赞操作数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("点赞操作数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
        //4. 组织响应
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("点赞操作处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::setPlay(::google::protobuf::RpcController* controller,
        const ::api_message::PlayVideoReq* request,
        ::api_message::PlayVideoRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &video_id = request->videoid();
        //3. 处理请求
		try {
			_data->playVideo(ssid, video_id);
		}catch (const vp_error::VPException &e) {
			ERR("播放操作数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("播放操作数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
        //4. 组织响应
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("播放操作处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::newBarrage(::google::protobuf::RpcController* controller,
        const ::api_message::newBarrageReq* request,
        ::api_message::newBarrageRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &barrage_info = request->barrageinfo();
        const auto &video_id = request->videoid();
        //3. 处理请求
		try {
            api_message::BarrageInfo info;
            info.CopyFrom(barrage_info);
            info.set_videoid(video_id);
			_data->newBarrage(ssid, info);
		}catch (const vp_error::VPException &e) {
			ERR("新增弹幕数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("新增弹幕数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
        //4. 组织响应
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("新增弹幕处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::getBarrage(::google::protobuf::RpcController* controller,
        const ::api_message::getBarrageListReq* request,
        ::api_message::getBarrageListRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &video_id = request->videoid();
        //3. 处理请求
		try {
			auto barrage_list = _data->getBarrage(ssid, video_id);
            auto result = response->mutable_result();
            for (auto &barrage : barrage_list) {
                auto src = result->add_barragelist();
                src->CopyFrom(barrage);
            }
		}catch (const vp_error::VPException &e) {
			ERR("获取弹幕数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("获取弹幕数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
        //4. 组织响应
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("获取弹幕处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::checkVideo(::google::protobuf::RpcController* controller,
        const ::api_message::checkVideoReq* request,
        ::api_message::checkVideoRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
		//2. 提取请求要素
		const std::string &rid = request->requestid();
		const std::string &ssid = request->sessionid();
        const auto &video_id = request->videoid();
        const auto result = request->checkresult();
        //3. 处理请求
		try {
			_data->checkVideo(ssid, video_id, result);
		}catch (const vp_error::VPException &e) {
			ERR("视频审核数据处理异常: {}-{}", rid, e.what());
			res_status_code = e.errCode();
		}catch (const std::exception &e) {
			ERR("视频审核数据处理错误: {}-{}", rid, e.what());
			res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
		}
        //4. 组织响应
		response->set_requestid(rid);
		response->set_errorcode((int)res_status_code);
		response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
		DBG("视频审核处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::saleVideo(::google::protobuf::RpcController* controller,
        const ::api_message::saleVideoReq* request,
        ::api_message::saleVideoRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        const auto &video_id = request->videoid();
        //3. 处理请求
        try {
            _data->onlineVideo(ssid, video_id);
        }catch (const vp_error::VPException &e) {
            ERR("视频上架数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("视频上架数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        //4. 组织响应
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("视频上架处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::haltVideo(::google::protobuf::RpcController* controller,
        const ::api_message::haltVideoReq* request,
        ::api_message::haltVideoRsp* response,
        ::google::protobuf::Closure* done)  {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        const auto &video_id = request->videoid();
        //3. 处理请求
        try {
            _data->offlineVideo(ssid, video_id);
        }catch (const vp_error::VPException &e) {
            ERR("视频下架数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("视频下架数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        //4. 组织响应
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("视频下架处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::userVideoList(::google::protobuf::RpcController* controller,
        const ::api_message::userVideoListReq* request,
        ::api_message::userVideoListRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        auto page_index = request->pageindex() > 0 ? request->pageindex() - 1 : 0;
        const auto page_count = request->pagecount();
        std::optional<std::string> user_id;
        if (request->has_userid()) {
            user_id = request->userid();
        }
        vp_data::VideoStatus status;
        if (request->has_videostatus()) {
            status = (vp_data::VideoStatus)request->videostatus();
        }else {
            status = vp_data::VideoStatus::ONLINE;
        }
        //3. 处理请求
        try {
            auto video_list = _data->getUserPageList(ssid, user_id, status, page_index, page_count);
            response->mutable_result()->CopyFrom(video_list);
        }catch (const vp_error::VPException &e) {
            ERR("获取用户视频列表数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("获取用户视频列表数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        //4. 组织响应
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("获取用户视频列表处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::statusVideoList(::google::protobuf::RpcController* controller,
        const ::api_message::StatusVideoListReq* request,
        ::api_message::StatusVideoListRsp* response,
        ::google::protobuf::Closure* done){
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        auto page_index = request->pageindex() > 0 ? request->pageindex() - 1 : 0;
        const auto page_count = request->pagecount();
        vp_data::VideoStatus status = (vp_data::VideoStatus)request->videostatus();
        //3. 处理请求
        try {
            size_t total = 0;
            auto video_list = _data->getStatusPageList(status, page_index, page_count, &total);
            auto result = response->mutable_result();
            result->set_totalcount(total);
            for (auto &video : video_list) {
                auto src = result->add_videolist();
                src->CopyFrom(video);
            }
        }catch (const vp_error::VPException &e) {
            ERR("获取状态视频列表数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("获取状态视频列表数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        //4. 组织响应
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("获取状态视频列表处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::allVideoList(::google::protobuf::RpcController* controller,
        const ::api_message::AllVideoListReq* request,
        ::api_message::AllVideoListRsp* response,
        ::google::protobuf::Closure* done){
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        auto page_index = request->pageindex() > 0 ? request->pageindex() - 1 : 0;
        const auto page_count = request->pagecount();
        //3. 处理请求
        try {
            auto video_list = _data->getMainPageList(page_index, page_count);
            auto result = response->mutable_result();
            for (auto &video : video_list) {
                auto src = result->add_videolist();
                src->CopyFrom(video);
            }
        }catch (const vp_error::VPException &e) {
            ERR("获取主页视频列表数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("获取主页视频列表数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        //4. 组织响应
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("获取主页视频列表处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::typeVideoList(::google::protobuf::RpcController* controller,
        const ::api_message::TypeVideoListReq* request,
        ::api_message::TypeVideoListRsp* response,
        ::google::protobuf::Closure* done){
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        auto page_index = request->pageindex() > 0 ? request->pageindex() - 1 : 0;
        const auto page_count = request->pagecount();
        const auto catgory_id = request->videotypeid();
        //3. 处理请求
        try {
            auto video_list = _data->getCatgoryPageList(catgory_id, page_index, page_count);
            auto result = response->mutable_result();
            for (auto &video : video_list) {
                auto src = result->add_videolist();
                src->CopyFrom(video);
            }
        }catch (const vp_error::VPException &e) {
            ERR("获取分类视频列表数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("获取分类视频列表数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        //4. 组织响应
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("获取分类视频列表处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::tagVideoList(::google::protobuf::RpcController* controller,
        const ::api_message::TagVideoListReq* request,
        ::api_message::TagVideoListRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        auto page_index = request->pageindex() > 0 ? request->pageindex() - 1 : 0;
        const auto page_count = request->pagecount();
        const auto tag_id = request->videotag();
        //3. 处理请求
        try {
            auto video_list = _data->getTagPageList(tag_id, page_index, page_count);
            auto result = response->mutable_result();
            for (auto &video : video_list) {
                auto src = result->add_videolist();
                src->CopyFrom(video);
            }
        }catch (const vp_error::VPException &e) {
            ERR("获取标签视频列表数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("获取标签视频列表数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        //4. 组织响应
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("获取标签视频列表处理完毕: {}-{}", rid, ssid);
    }
    void VideoServiceImpl::keyVideoList(::google::protobuf::RpcController* controller,
        const ::api_message::keyVideoListReq* request,
        ::api_message::keyVideoListRsp* response,
        ::google::protobuf::Closure* done) {
        //1. 准备响应状态码，以及对done进行管理
        brpc::ClosureGuard done_guard(done);
        vp_error::VpError res_status_code = vp_error::VpError::SUCCESS;
        //2. 提取请求要素
        const std::string &rid = request->requestid();
        const std::string &ssid = request->sessionid();
        auto page_index = request->pageindex() > 0 ? request->pageindex() - 1 : 0;
        const auto page_count = request->pagecount();
        const auto search_key = request->searchkey();
        //3. 处理请求
        try {
            auto video_list = _data->getSearchPageList(search_key, page_index, page_count);
            auto result = response->mutable_result();
            for (auto &video : video_list) {
                auto src = result->add_videolist();
                src->CopyFrom(video);
            }
        }catch (const vp_error::VPException &e) {
            ERR("获取搜索视频列表数据处理异常: {}-{}", rid, e.what());
            res_status_code = e.errCode();
        }catch (const std::exception &e) {
            ERR("获取搜索视频列表数据处理错误: {}-{}", rid, e.what());
            res_status_code = vp_error::VpError::SERVER_INERNAL_ERROR;
        }
        //4. 组织响应
        response->set_requestid(rid);
        response->set_errorcode((int)res_status_code);
        response->set_errormsg(vp_error::vp_error_to_string(res_status_code));
        DBG("获取搜索视频列表处理完毕: {}-{}", rid, ssid);
    }
}