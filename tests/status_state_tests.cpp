// SPDX-License-Identifier: GPL-3.0-only
#include "status_state.h"
#include "iidx_protocol.h"
#include "sdvx_protocol.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string_view>
using namespace cabinet;
using K=Observation::Kind;
void event(StatusState &s,K k,const char *id="",bool success=true) {
    Observation e{k,s.generation(),{},success};
    if(*id) {
        const std::string_view value(id); assert(value.size()<e.profile_id.size());
        std::copy(value.begin(),value.end(),e.profile_id.begin());
        assert(valid_profile_id(e.profile_id) || valid_sdvx_refid(e.profile_id));
    }
    s.apply(e);
}
void idle(StatusState &s) { event(s,K::IdleArchiveOpened); event(s,K::IdleArchiveMounted); }
void login(StatusState &s,const char *id="10000001") { event(s,K::GetRequested); event(s,K::ProfileLoaded,id); }
void sdvx_state_cases() {
    constexpr auto refid="a1B2c3D4e5F60718";
    constexpr auto other="ABCDEF0123456789";
    StatusState s; idle(s);
    // An open before login belongs to an old generation and cannot release this session.
    event(s,K::IdleArchiveOpened);
    event(s,K::GetRequested,refid); event(s,K::ProfileLoaded);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::SaveRequested,other); assert(s.summary().phase==Phase::Occupied);
    event(s,K::SaveRequested,refid); assert(s.summary().phase==Phase::Ending);
    event(s,K::IdleArchiveMounted); assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveOpened,"",false); event(s,K::IdleArchiveMounted);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveOpened); event(s,K::IdleArchiveMounted,"",false);
    assert(s.snapshot()==Availability::Occupied); idle(s); assert(s.snapshot()==Availability::Free);
    // The next load must replace the completed session's request identity.
    event(s,K::GetRequested,other); event(s,K::ProfileLoaded);
    event(s,K::SaveRequested,refid); assert(s.summary().phase==Phase::Occupied);
    event(s,K::SaveRequested,other); assert(s.summary().phase==Phase::Ending);
    idle(s); assert(s.snapshot()==Availability::Free);
    StatusState returned_code;
    event(returned_code,K::GetRequested,refid); event(returned_code,K::ProfileLoaded,"20000001");
    assert(returned_code.snapshot()==Availability::Unavailable);
    StatusState absent_refid;
    event(absent_refid,K::GetRequested); event(absent_refid,K::ProfileLoaded);
    assert(absent_refid.snapshot()==Availability::Unavailable);
    StatusState failed;
    event(failed,K::GetRequested,refid); event(failed,K::ProfileLoaded,"",false); idle(failed);
    assert(failed.snapshot()==Availability::Unavailable);
}
void sdvx_resource_boundaries() {
    assert(is_sdvx_idle_path("/data/graphics/ver07/title.ifs"));
    constexpr const char *bad_paths[]={nullptr,"/data/graphics//ver07/title.ifs","/data/graphics/ver07/title.ifs/",
                                       "/data/graphics/ver06/title.ifs","/data/graphics/../ver07/title.ifs"};
    for(const auto *path:bad_paths) assert(!is_sdvx_idle_path(path));
    assert(is_sdvx_idle_mount("/mnt/bm2d/ngp0/title.ifs","imagefs"));
    assert(is_sdvx_idle_mount("/mnt/bm2d/ngp123/title.ifs","imagefs"));
    constexpr const char *bad_mounts[]={nullptr,"/mnt/bm2d/ngp/title.ifs","/mnt/bm2d/ngp1x/title.ifs",
                                        "/mnt/bm2d/ngp1//title.ifs","/mnt/bm2d/ngp1/title.ifs/",
                                        "/mnt/bm2d/ngp1/other.ifs","/mnt/bm2d/ngp1/../title.ifs"};
    for(const auto *path:bad_mounts) assert(!is_sdvx_idle_mount(path,"imagefs"));
    assert(!is_sdvx_idle_mount("/mnt/bm2d/ngp1/title.ifs","other"));
    assert(!is_sdvx_idle_mount("/mnt/bm2d/ngp1/title.ifs",nullptr));
    std::array<char,600> unterminated; unterminated.fill('1');
    constexpr std::string_view prefix="/mnt/bm2d/ngp";
    std::copy(prefix.begin(),prefix.end(),unterminated.begin());
    assert(!is_sdvx_idle_mount(unterminated.data(),"imagefs"));
    std::array<char,32> hex{};
    constexpr std::string_view refid="a1B2c3D4e5F60718";
    std::copy(refid.begin(),refid.end(),hex.begin());
    assert(valid_sdvx_refid(hex) && !valid_profile_id(hex));
    hex[16]='0'; assert(!valid_sdvx_refid(hex));
}
int main() {
    StatusState s; assert(s.snapshot()==Availability::Unavailable);
    idle(s); assert(s.snapshot()==Availability::Free);
    // PIN-only cancel has no target observation; repeated idle remains free.
    idle(s); assert(s.snapshot()==Availability::Free);
    event(s,K::GetRequested); assert(s.snapshot()==Availability::Unavailable);
    event(s,K::ProfileLoaded,"10000001"); assert(s.snapshot()==Availability::Occupied);
    event(s,K::LogoutRequested,"10000002"); assert(s.summary().phase==Phase::Occupied);
    event(s,K::LogoutRequested,"10000001"); event(s,K::LogoutRequested,"10000001");
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveOpened); assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveMounted); assert(s.snapshot()==Availability::Free);
    // Full play / upload: end intent never releases without a resource boundary.
    login(s); event(s,K::SaveRequested,"10000001");
    event(s,K::IdleArchiveMounted); assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveOpened,"",false); event(s,K::IdleArchiveMounted);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveOpened); event(s,K::IdleArchiveMounted,"",false);
    assert(s.snapshot()==Availability::Occupied); idle(s); assert(s.snapshot()==Availability::Free);
    // Same semantics when a single player used P2: no position field exists.
    login(s); idle(s); login(s); idle(s); assert(s.snapshot()==Availability::Free);
    Observation old{K::IdleArchiveOpened,s.generation(),{},true};
    login(s); s.apply(old); old.kind=K::IdleArchiveMounted; s.apply(old);
    assert(s.snapshot()==Availability::Occupied);
    StatusState overlap; idle(overlap); event(overlap,K::GetRequested); event(overlap,K::GetRequested);
    assert(overlap.snapshot()==Availability::Unavailable); idle(overlap); login(overlap);
    assert(overlap.snapshot()==Availability::Unavailable);
    StatusState abandoned; idle(abandoned); event(abandoned,K::GetRequested); idle(abandoned);
    assert(abandoned.snapshot()==Availability::Unavailable); login(abandoned); idle(abandoned);
    assert(abandoned.snapshot()==Availability::Unavailable);
    StatusState duplicate; login(duplicate); event(duplicate,K::ProfileLoaded,"10000001");
    assert(duplicate.snapshot()==Availability::Unavailable);
    StatusState conflict; login(conflict); event(conflict,K::GetRequested);
    assert(conflict.snapshot()==Availability::Occupied); event(conflict,K::ProfileLoaded,"10000002");
    assert(conflict.snapshot()==Availability::Unavailable);
    StatusState stale; Observation late{K::GetRequested,0,{},true}; idle(stale); stale.apply(late);
    assert(stale.snapshot()==Availability::Unavailable);
    StatusState fault; idle(fault); event(fault,K::CaptureFault); idle(fault);
    assert(fault.snapshot()==Availability::Unavailable); fault.stop(); assert(fault.snapshot()==Availability::Unavailable);
    assert(is_idle_path("/data/graphic//1/sub_demo.ifs"));
    assert(!is_idle_path("/data/graphic/../1/sub_demo.ifs"));
    assert(!is_idle_path("/data/graphic/1/sub_demo.ifs/"));
    assert(is_idle_mount("/afp123/data/graphic//1/sub_demo.ifs","imagefs"));
    assert(!is_idle_mount("/afp/data/graphic/1/sub_demo.ifs","imagefs"));
    assert(!is_idle_mount("/afp1/data/graphic/1/sub_demo.ifs","other"));
    sdvx_state_cases(); sdvx_resource_boundaries();
    std::puts("state trajectories and resource boundaries passed");
}
