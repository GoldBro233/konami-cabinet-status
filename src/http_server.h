// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "config.h"
#include "status_state.h"
#include <mutex>
struct mg_context;
struct mg_connection;
namespace cabinet {
class HttpServer {
public:
    bool start(const Config &,const StatusState &,std::string &error);
    void stop() noexcept;
    HttpServer()=default;
    HttpServer(const HttpServer &)=delete;
    HttpServer &operator=(const HttpServer &)=delete;
private:
    static int dispatch(mg_connection *);
    static int log_message(const mg_connection *,const char *);
    mg_context *context_{};
    const StatusState *state_{};
    bool initialized_{};
    std::mutex error_mutex_;
    std::string startup_error_;
};
}
