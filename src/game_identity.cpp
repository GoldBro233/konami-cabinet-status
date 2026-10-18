// SPDX-License-Identifier: GPL-3.0-only
#include "game_identity.h"
#include <psapi.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <type_traits>
namespace cabinet {
bool loaded_image(HMODULE module,std::span<const std::byte> &out,std::string &error) {
    MODULEINFO info{};
    if(!module || !GetModuleInformation(GetCurrentProcess(),module,&info,sizeof(info)) || !info.SizeOfImage) {
        error="cannot inspect loaded module"; return false;
    }
    out={static_cast<const std::byte *>(info.lpBaseOfDll),info.SizeOfImage}; return true;
}
bool identify_game(const SPICE_SDK_V0 &sdk,GameIdentity &out,std::string &diagnostic,std::string &error) {
    SPICE_SDK_AVS_INFO info{};
    if(!sdk.get_avs_info || sdk.get_avs_info(&info)!=SPICE_SDK_STATUS_SUCCESS) { error="get_avs_info failed"; return false; }
    info.model[3]=0; info.ext[10]=0;
    diagnostic="model="+std::string(info.model)+" date="+info.ext;
    if(std::strcmp(info.model,"LDJ") || std::strcmp(info.ext,"2026081900")) { error="unsupported game model/date"; return false; }
    out.game=GetModuleHandleW(L"bm2dx.dll"); out.avs=GetModuleHandleW(L"avs2-core.dll");
    if(!out.game || !out.avs) { error="required bm2dx.dll or avs2-core.dll is not loaded"; return false; }
    PeIdentity pe{}; std::span<const std::byte> game;
    if(!loaded_image(out.game,game,error) || !pe_identity(game,pe,error)) return false;
    char pe_text[100]; std::snprintf(pe_text,sizeof(pe_text)," machine=%04x timestamp=%08x entry=%08x",pe.machine,pe.timestamp,pe.entry_point);
    diagnostic+=pe_text;
    if(sdk.get_module_info) {
        SPICE_SDK_MODULE_INFO helper{}; helper.size=sizeof(helper);
        if(sdk.get_module_info(L"bm2dx.dll",&helper)!=SPICE_SDK_STATUS_SUCCESS || helper.machine!=pe.machine || helper.timestamp!=pe.timestamp || helper.entry_point!=pe.entry_point || helper.image_size!=pe.image_size || helper.base!=reinterpret_cast<uintptr_t>(out.game)) {
            error="SDK module identity unavailable or inconsistent"; return false;
        }
    }
    if(pe.machine!=0x8664 || pe.timestamp!=0x6a713632 || pe.entry_point!=0x00b1cccc) { error="unsupported bm2dx PE identity"; return false; }
    std::vector<wchar_t> path(32768);
    DWORD length=GetModuleFileNameW(out.avs,path.data(),static_cast<DWORD>(path.size()));
    if(!length || length>=path.size()) { error="cannot obtain AVS module filename"; return false; }
    DWORD ignored=0; DWORD size=GetFileVersionInfoSizeW(path.data(),&ignored);
    if(!size) { error="AVS version resource missing"; return false; }
    std::vector<std::byte> data(size);
    if(!GetFileVersionInfoW(path.data(),0,size,data.data())) { error="AVS version resource unreadable"; return false; }
    VS_FIXEDFILEINFO *version=nullptr; UINT bytes=0;
    if(!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void **>(&version),&bytes) || bytes<sizeof(*version) || version->dwSignature!=0xfeef04bd) { error="invalid AVS file version"; return false; }
    char version_text[80]; std::snprintf(version_text,sizeof(version_text)," avs=%u.%u.%u.%u",HIWORD(version->dwFileVersionMS),LOWORD(version->dwFileVersionMS),HIWORD(version->dwFileVersionLS),LOWORD(version->dwFileVersionLS));
    diagnostic+=version_text;
    if(version->dwFileVersionMS!=MAKELONG(17,2) || version->dwFileVersionLS!=MAKELONG(0,4)) { error="unsupported AVS file version"; return false; }
    auto resolve=[&](auto &fn,const char *name) {
        fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(out.avs,name));
        if(!fn) { error=std::string("missing AVS export ")+name; return false; } return true;
    };
    auto &f=out.functions;
    if(!resolve(f.destroy,"XCgsqzn0000091") || !resolve(f.search,"XCgsqzn00000a1") || !resolve(f.traverse,"XCgsqzn00000a6") ||
       !resolve(f.name,"XCgsqzn00000a7") || !resolve(f.node_type,"XCgsqzn00000a8") || !resolve(f.read,"XCgsqzn00000ab") ||
       !resolve(f.refer,"XCgsqzn00000af") || !resolve(f.get_error,"XCgsqzn000009e") || !resolve(f.clear_error,"XCgsqzn000009d") ||
       !resolve(f.fs_open,"XCgsqzn000004e") || !resolve(f.fs_mount,"XCgsqzn000004b")) return false;
    out.targets={ImportTarget{"XCgsqzn0000091",0,reinterpret_cast<uint64_t>(f.destroy)},
                 ImportTarget{"XCgsqzn000004e",0,reinterpret_cast<uint64_t>(f.fs_open)},
                 ImportTarget{"XCgsqzn000004b",0,reinterpret_cast<uint64_t>(f.fs_mount)}};
    std::span<const std::byte> avs;
    if(!loaded_image(out.avs,avs,error)) return false;
    for(auto &target:out.targets) if(!pe_export_ordinal(avs,target.name,target.ordinal,error)) return false;
    return true;
}
}
