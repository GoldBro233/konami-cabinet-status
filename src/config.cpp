// SPDX-License-Identifier: GPL-3.0-only
#include "config.h"
#include <array>
namespace cabinet {
namespace {
std::string_view trim(std::string_view s) {
    constexpr std::string_view ws=" \t\r\n";
    auto a=s.find_first_not_of(ws); if(a==s.npos) return {};
    return s.substr(a,s.find_last_not_of(ws)-a+1);
}
bool equal(std::string_view a,std::string_view b) {
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i) {
        char c=a[i]; if(c>='A'&&c<='Z') c+=('a'-'A'); if(c!=b[i]) return false;
    } return true;
}
bool decimal(std::string_view s,unsigned limit,unsigned &v) {
    if(s.empty()) return false; v=0;
    for(char c:s) { if(c<'0'||c>'9'||v>(limit-static_cast<unsigned>(c-'0'))/10) return false; v=v*10+(c-'0'); }
    return v<=limit;
}
bool ipv4(std::string_view s,std::string &out) {
    std::array<unsigned,4> numbers{};
    for(size_t i=0;i<4;++i) {
        auto dot=s.find('.'); auto part=s.substr(0,dot);
        if(!decimal(part,255,numbers[i])) return false;
        if(i<3) { if(dot==s.npos) return false; s.remove_prefix(dot+1); }
        else if(dot!=s.npos) return false;
    }
    out=std::to_string(numbers[0])+"."+std::to_string(numbers[1])+"."+std::to_string(numbers[2])+"."+std::to_string(numbers[3]);
    return true;
}
}
bool parse_config(std::string_view text,Config &out,std::string &error) {
    error.clear(); Config parsed;
    auto fail=[&](const char *why) { error=why; return false; };
    if(text.size()>65536) return fail("configuration exceeds 64 KiB");
    if(text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    bool server=false,unknown=false; unsigned seen=0;
    while(!text.empty()) {
        auto nl=text.find('\n'); auto line=trim(text.substr(0,nl));
        if(nl==text.npos) text={}; else text.remove_prefix(nl+1);
        if(line.empty()||line[0]==';'||line[0]=='#') continue;
        if(line[0]=='[') {
            if(line.back()!=']') return fail("malformed section");
            server=equal(trim(line.substr(1,line.size()-2)),"server"); unknown|=!server; continue;
        }
        auto eq=line.find('='); if(eq==line.npos) return fail("expected key=value");
        auto key=trim(line.substr(0,eq)),value=trim(line.substr(eq+1));
        unsigned bit=0;
        if(server) {
            if(equal(key,"enabled")) bit=1;
            else if(equal(key,"port")) bit=2;
            else if(equal(key,"bindaddress")) bit=4;
        }
        if(!bit) { unknown=true; continue; }
        if(seen&bit) return fail("duplicate Server key"); seen|=bit;
        if(value.empty()) return fail("empty Server value");
        if(bit==1) { if(value!="0" && value!="1") return fail("Enabled must be 0 or 1"); parsed.enabled=value=="1"; }
        if(bit==2) { unsigned port; if(!decimal(value,65535,port)||!port) return fail("Port must be decimal 1..65535"); parsed.port=static_cast<uint16_t>(port); }
        if(bit==4 && !ipv4(value,parsed.bind_address)) return fail("BindAddress must be a dotted decimal IPv4 address");
    }
    out=std::move(parsed);
    if(unknown) error="unknown configuration sections or keys ignored";
    return true;
}
}
