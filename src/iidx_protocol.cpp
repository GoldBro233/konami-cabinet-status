// SPDX-License-Identifier: GPL-3.0-only
#include "iidx_protocol.h"
#include <algorithm>
#include <cstring>
namespace cabinet {
bool valid_profile_id(const std::array<char,32> &id) noexcept {
    size_t i=0;
    for (; i<id.size() && id[i]; ++i) if (id[i]<'0' || id[i]>'9') return false;
    return i>0 && i<id.size();
}
bool copy_profile_id(std::string_view value, std::array<char,32> &out) noexcept {
    out.fill(0);
    if(value.empty() || value.size()>=out.size()) return false;
    std::copy(value.begin(),value.end(),out.begin());
    if (!valid_profile_id(out)) { out.fill(0); return false; }
    return true;
}
// Compare components without allocating; observed IIDX idle archives use 0 or 1.
bool is_idle_path(const char *path) noexcept {
    if(!path || *path!='/') return false;
    constexpr std::string_view parts[]={"data","graphic","1","sub_demo.ifs"};
    size_t used=0;
    for(size_t component=0;component<std::size(parts);++component) {
        if(path[used]!='/') return false;
        while(used<512 && path[used]=='/') ++used;
        if(component==2) {
            if(used>=512 || (path[used]!='0' && path[used]!='1')) return false;
            ++used;
        } else {
            for(char c:parts[component]) { if(used>=512 || path[used]!=c) return false; ++used; }
        }
    }
    return used<512 && path[used]=='\0';
}
bool is_idle_mount(const char *path,const char *type) noexcept {
    if(!path || !type || std::strcmp(type,"imagefs")!=0 || std::strncmp(path,"/afp",4)!=0) return false;
    size_t i=4;
    while(i<64 && path[i]>='0' && path[i]<='9') ++i;
    return i>4 && i<64 && is_idle_path(path+i);
}
}
