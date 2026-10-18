// SPDX-License-Identifier: GPL-3.0-only
#include "http_server.h"
#include "civetweb.h"
#include <cstring>
namespace cabinet {
int HttpServer::log_message(const mg_connection *conn,const char *message) {
    auto *self=static_cast<HttpServer *>(mg_get_user_data(mg_get_context(conn)));
    if(self) { std::lock_guard lock(self->error_mutex_); self->startup_error_=message?message:"CivetWeb error"; }
    return 1;
}
bool HttpServer::start(const Config &config,const StatusState &state,std::string &error) {
    error.clear(); if(context_) return true;
    if(!config.enabled) return true;
    state_=&state;
    { std::lock_guard lock(error_mutex_); startup_error_.clear(); }
    mg_init_library(0); initialized_=true;
    const std::string endpoint=config.bind_address+":"+std::to_string(config.port);
    const char *options[]={"listening_ports",endpoint.c_str(),"num_threads","2",
        "enable_keep_alive","no","request_timeout_ms","1000","max_request_size","4096","decode_url","no",nullptr};
    mg_callbacks callbacks{}; callbacks.begin_request=&dispatch; callbacks.log_message=&log_message;
    context_=mg_start(&callbacks,this,options);
    if(!context_) {
        { std::lock_guard lock(error_mutex_); error=startup_error_.empty()?"CivetWeb listener startup failed":startup_error_; }
        stop(); return false;
    }
    return true;
}
void HttpServer::stop() noexcept {
    if(context_) { mg_stop(context_); context_=nullptr; }
    if(initialized_) { mg_exit_library(); initialized_=false; }
    state_=nullptr;
}
int HttpServer::dispatch(mg_connection *conn) {
    auto *info=mg_get_request_info(conn);
    auto *self=static_cast<HttpServer *>(info->user_data);
    const bool head=std::strcmp(info->request_method,"HEAD")==0;
    int code; const char *phrase; const char *body; const char *allow="";
    if(!info->local_uri_raw || std::strcmp(info->local_uri_raw,"/status")!=0) {
        code=404; phrase="Not Found"; body="{\"error\":\"not_found\"}";
    } else if(std::strcmp(info->request_method,"GET")!=0) {
        code=405; phrase="Method Not Allowed"; body="{\"error\":\"method_not_allowed\"}"; allow="Allow: GET\r\n";
    } else {
        const auto availability=self->state_->snapshot();
        if(availability==Availability::Unavailable) { code=503; phrase="Service Unavailable"; body="{\"error\":\"state_unavailable\"}"; }
        else { code=200; phrase="OK"; body=availability==Availability::Free?"{\"status\":\"free\"}":"{\"status\":\"occupied\"}"; }
    }
    const size_t length=std::strlen(body);
    int sent;
    if(head) sent=mg_printf(conn,"HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n%s\r\n",code,phrase,allow);
    else sent=mg_printf(conn,"HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n%s\r\n",code,phrase,length,allow);
    if(sent>0 && !head) (void)mg_write(conn,body,length);
    return code;
}
}
