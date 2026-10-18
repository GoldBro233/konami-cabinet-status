// SPDX-License-Identifier: GPL-3.0-only
#include "iat_hooks.h"
#include "http_server.h"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {
using namespace cabinet;
HMODULE plugin_module=nullptr;
INIT_ONCE entry_once=INIT_ONCE_STATIC_INIT;
struct Runtime {
    SPICE_SDK_V0 sdk{};
    StatusState state;
    IatHooks hooks{state};
    HttpServer http;
    Config config;
    HANDLE stop_event=nullptr;
    HANDLE manager=nullptr;
    std::mutex lifecycle;
    std::atomic<bool> stopping{false};
    std::atomic<bool> destroyed{false};
    bool sdk_valid=false;
    int entry_result=0;

    void log(SPICE_SDK_LOG_LEVEL level,const std::string &message) const noexcept {
        if(sdk_valid && sdk.log) (void)sdk.log(level,"cabinet-status",message.c_str());
    }
    void capture_fault() noexcept {
        hooks.disable(); state.apply({Observation::Kind::CaptureFault,state.generation(),{},false});
    }
};
std::atomic<Runtime *> runtime{nullptr};

bool plugin_directory(Runtime &r,std::wstring &directory,std::string &error) {
    if(r.sdk.get_plugin_directory) {
        uint32_t length=0;
        auto status=r.sdk.get_plugin_directory(&plugin_module,nullptr,&length);
        if(status!=SPICE_SDK_STATUS_TOO_SMALL || !length || length>32768) { error="get_plugin_directory size query failed"; return false; }
        std::vector<wchar_t> buffer(length);
        status=r.sdk.get_plugin_directory(&plugin_module,buffer.data(),&length);
        if(status!=SPICE_SDK_STATUS_SUCCESS || length>buffer.size() || !length || buffer[length-1]!=0) { error="get_plugin_directory failed"; return false; }
        directory=buffer.data(); return true;
    }
    std::vector<wchar_t> buffer(32768);
    DWORD length=GetModuleFileNameW(plugin_module,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!length || length>=buffer.size()) { error="GetModuleFileNameW for plugin failed"; return false; }
    directory.assign(buffer.data(),length);
    const auto slash=directory.find_last_of(L"/\\");
    if(slash==directory.npos) { error="plugin module directory unavailable"; return false; }
    directory.resize(slash); return true;
}
bool read_configuration(Runtime &r,std::string &warning,std::string &error) {
    std::wstring directory;
    if(!plugin_directory(r,directory,error)) return false;
    const std::wstring path=directory+L"\\cabinet-status.ini";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) {
        DWORD code=GetLastError();
        if(code==ERROR_FILE_NOT_FOUND) { r.config=Config{}; warning="cabinet-status.ini missing; using defaults"; return true; }
        error="cannot open cabinet-status.ini (win32="+std::to_string(code)+")"; return false;
    }
    struct File { HANDLE handle; ~File(){ CloseHandle(handle); } } lease{file};
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size) || size.QuadPart<0 || size.QuadPart>65536) { error="configuration unreadable or exceeds 64 KiB"; return false; }
    std::string text(static_cast<size_t>(size.QuadPart),'\0');
    DWORD actual=0;
    if(!ReadFile(file,text.data(),static_cast<DWORD>(text.size()),&actual,nullptr) || actual!=text.size()) { error="configuration read failed"; return false; }
    if(!parse_config(text,r.config,error)) return false;
    warning=std::move(error); error.clear(); return true;
}
DWORD WINAPI manage(void *context) noexcept {
    auto &r=*static_cast<Runtime *>(context);
    try {
        if(WaitForSingleObject(r.stop_event,0)!=WAIT_TIMEOUT) return 0;
        std::string error;
        if(!r.http.start(r.config,r.state,error)) {
            r.capture_fault(); r.log(SPICE_SDK_LOG_LEVEL_WARNING,"HTTP disabled: "+error); return 0;
        }
        if(WaitForSingleObject(r.stop_event,0)!=WAIT_TIMEOUT) { r.http.stop(); return 0; }
        r.log(SPICE_SDK_LOG_LEVEL_INFO,"enabled=1 listening="+r.config.bind_address+":"+std::to_string(r.config.port)+"/status");
        uint64_t revision=std::numeric_limits<uint64_t>::max();
        bool decode_fault_logged=false;
        ULONGLONG checked=GetTickCount64();
        for(;;) {
            const auto wait=WaitForSingleObject(r.stop_event,100);
            if(wait!=WAIT_TIMEOUT) break;
            const auto now=GetTickCount64();
            if(now-checked>=500) {
                checked=now;
                if(!r.hooks.check()) r.log(SPICE_SDK_LOG_LEVEL_WARNING,"iat_replaced; observation disabled for this process");
            }
            DecodeDiagnostic diagnostic;
            if(!decode_fault_logged && r.hooks.first_decode_fault(diagnostic)) {
                decode_fault_logged=true;
                r.log(SPICE_SDK_LOG_LEVEL_WARNING,"first_decode_fault stage="+std::string(decode_point_name(diagnostic.point))+
                    " issue="+decode_issue_name(diagnostic.issue)+" node_type="+std::to_string(diagnostic.observed_type)+
                    " accessor_result="+std::to_string(diagnostic.accessor_result)+" avs_error="+std::to_string(diagnostic.avs_error)+
                    " network_root="+(diagnostic.network_root?"1":"0"));
            }
            const auto summary=r.state.summary();
            if(summary.revision!=revision) {
                revision=summary.revision;
                r.log(SPICE_SDK_LOG_LEVEL_INFO,"revision="+std::to_string(summary.revision)+" phase="+phase_name(summary.phase)+" reason="+reason_name(summary.reason));
            }
        }
        r.http.stop();
    } catch(...) {
        r.capture_fault(); r.http.stop();
        r.log(SPICE_SDK_LOG_LEVEL_WARNING,"management exception; service disabled");
    }
    return 0;
}
void __cdecl destroy() {
    auto *r=runtime.load(std::memory_order_acquire);
    if(!r || r->destroyed.exchange(true,std::memory_order_acq_rel)) return;
    r->stopping.store(true,std::memory_order_release);
    r->hooks.disable(); r->state.stop();
    std::lock_guard lock(r->lifecycle);
    r->hooks.disable();
    if(r->stop_event) SetEvent(r->stop_event);
    if(r->manager) {
        WaitForSingleObject(r->manager,INFINITE);
        CloseHandle(r->manager); r->manager=nullptr;
    }
    if(r->stop_event) { CloseHandle(r->stop_event); r->stop_event=nullptr; }
    r->log(SPICE_SDK_LOG_LEVEL_INFO,"stopped; IAT wrappers retained as forward-only");
    r->sdk_valid=false;
    // Process-lifetime records: a game thread may still be inside a wrapper.
}
BOOL CALLBACK initialize(PINIT_ONCE,void *parameter,void **) noexcept {
    Runtime *r=nullptr;
    try { r=new(std::nothrow) Runtime; } catch(...) { return TRUE; }
    if(!r) return TRUE;
    runtime.store(r,std::memory_order_release);
    try {
        std::lock_guard lock(r->lifecycle);
        auto *init=reinterpret_cast<spice_sdk_init_func *>(parameter);
        if(!init) return TRUE;
        r->stop_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!r->stop_event) return TRUE;
        r->sdk.size=sizeof(r->sdk);
        if(init(0,&destroy,&r->sdk)!=SPICE_SDK_STATUS_SUCCESS) {
            CloseHandle(r->stop_event); r->stop_event=nullptr; return TRUE;
        }
        r->sdk_valid=true;
        if(!r->sdk.log || !r->sdk.get_avs_info) { r->log(SPICE_SDK_LOG_LEVEL_WARNING,"required SDK helpers missing"); return TRUE; }
        std::string error,warning;
        if(!read_configuration(*r,warning,error)) {
            r->capture_fault(); r->log(SPICE_SDK_LOG_LEVEL_WARNING,"plugin disabled: "+error); r->entry_result=1; return TRUE;
        }
        if(!warning.empty()) r->log(SPICE_SDK_LOG_LEVEL_WARNING,warning);
        if(!r->config.enabled) { r->log(SPICE_SDK_LOG_LEVEL_INFO,"enabled=0; service disabled"); r->entry_result=1; return TRUE; }
        GameIdentity identity; std::string diagnostic; std::vector<std::string> callers;
        const bool supported=identify_game(r->sdk,identity,diagnostic,error);
        if(!diagnostic.empty()) r->log(SPICE_SDK_LOG_LEVEL_INFO,diagnostic);
        if(!supported) {
            r->capture_fault(); r->log(SPICE_SDK_LOG_LEVEL_WARNING,"capture unavailable: "+error);
        } else if(identity.kind==GameKind::Sdvx) {
            r->capture_fault(); r->log(SPICE_SDK_LOG_LEVEL_WARNING,"capture unavailable: SDVX identified; status adapter is not implemented");
        } else {
            const bool installed=r->hooks.install(identity,plugin_module,callers,error);
            for(const auto &caller:callers) r->log(SPICE_SDK_LOG_LEVEL_INFO,caller);
            if(!installed) { r->capture_fault(); r->log(SPICE_SDK_LOG_LEVEL_WARNING,"capture unavailable: "+error); }
        }
        if(r->stopping.load(std::memory_order_acquire)) { r->hooks.disable(); return TRUE; }
        r->manager=CreateThread(nullptr,0,&manage,r,0,nullptr);
        if(!r->manager) {
            r->capture_fault(); r->log(SPICE_SDK_LOG_LEVEL_WARNING,"management thread creation failed (win32="+std::to_string(GetLastError())+")");
        }
        r->entry_result=1;
    } catch(...) {
        r->capture_fault(); r->log(SPICE_SDK_LOG_LEVEL_WARNING,"initialization exception; service disabled");
    }
    return TRUE;
}
} // namespace
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) plugin_module=module;
    return TRUE;
}
SPICE_SDK_ENTRY_POINT spice_sdk_entry_point(spice_sdk_init_func *init) {
    InitOnceExecuteOnce(&entry_once,&initialize,reinterpret_cast<void *>(init),nullptr);
    auto *r=runtime.load(std::memory_order_acquire);
    return r?r->entry_result:0;
}
