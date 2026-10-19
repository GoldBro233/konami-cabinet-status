// SPDX-License-Identifier: GPL-3.0-only
#include "sdvx_protocol.h"
#include <cstring>
#include <string_view>
namespace cabinet {
bool valid_sdvx_refid(const std::array<char,32> &id) noexcept {
    for(size_t i=0;i<16;++i) {
        const char c=id[i];
        if(!((c>='0' && c<='9') || (c>='A' && c<='F') || (c>='a' && c<='f'))) return false;
    }
    return id[16]=='\0';
}
bool is_sdvx_idle_path(const char *path) noexcept {
    constexpr char idle[]="/data/graphics/ver07/title.ifs";
    return path && std::strncmp(path,idle,sizeof(idle))==0;
}
bool is_sdvx_idle_mount(const char *path,const char *type) noexcept {
    constexpr std::string_view prefix="/mnt/bm2d/ngp";
    constexpr char suffix[]="/title.ifs";
    if(!path || !type || std::strncmp(type,"imagefs",sizeof("imagefs"))!=0 ||
       std::strncmp(path,prefix.data(),prefix.size())!=0) return false;
    size_t i=prefix.size();
    while(i<512 && path[i]>='0' && path[i]<='9') ++i;
    return i>prefix.size() && i<=512-sizeof(suffix) && std::strncmp(path+i,suffix,sizeof(suffix))==0;
}
}
