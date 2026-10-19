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
void guest_state_cases() {
    StatusState s; idle(s);
    event(s,K::IdleArchiveOpened);
    event(s,K::PlayStarted);
    assert(s.snapshot()==Availability::Occupied && s.summary().reason==Reason::PlayStarted);
    event(s,K::IdleArchiveMounted); assert(s.snapshot()==Availability::Occupied);
    event(s,K::PlayStarted);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveOpened,"",false); event(s,K::IdleArchiveMounted);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveOpened); event(s,K::IdleArchiveMounted,"",false);
    assert(s.snapshot()==Availability::Occupied);
    idle(s); assert(s.snapshot()==Availability::Free);
    // A second guest session must invalidate resources captured during the first.
    event(s,K::PlayStarted);
    Observation old{K::IdleArchiveOpened,s.generation(),{},true};
    idle(s); event(s,K::PlayStarted); s.apply(old);
    old.kind=K::IdleArchiveMounted; s.apply(old);
    assert(s.snapshot()==Availability::Occupied);
    idle(s); assert(s.snapshot()==Availability::Free);
    // Guest play can begin before a card response and must preserve its pending get.
    event(s,K::GetRequested); event(s,K::PlayStarted);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::ProfileLoaded,"10000001");
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::SaveRequested,"10000001");
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::PlayStarted); assert(s.snapshot()==Availability::Occupied);
    idle(s); assert(s.snapshot()==Availability::Free);
    // Card login followed by playstart also retains the card identity.
    login(s); event(s,K::PlayStarted); event(s,K::LogoutRequested,"10000001");
    assert(s.snapshot()==Availability::Occupied && s.summary().phase==Phase::Ending);
    idle(s); assert(s.snapshot()==Availability::Free);
    // Playstart cannot erase an unanswered card load at the idle boundary.
    event(s,K::GetRequested); event(s,K::PlayStarted); idle(s);
    assert(s.snapshot()==Availability::Unavailable);
    event(s,K::PlayStarted); idle(s); assert(s.snapshot()==Availability::Unavailable);
}
void two_profile_state_cases() {
    constexpr const char *ids[]={"10000001","10000002"};
    for(bool interleaved:{false,true}) {
        const auto *first=ids[0];
        const auto *second=ids[1];
        StatusState s; idle(s);
        event(s,K::GetRequested);
        if(interleaved) event(s,K::GetRequested);
        event(s,K::ProfileLoaded,first);
        assert(s.snapshot()==Availability::Occupied);
        if(!interleaved) event(s,K::GetRequested);
        assert(s.snapshot()==Availability::Occupied);
        event(s,K::ProfileLoaded,second);
        assert(s.snapshot()==Availability::Occupied && s.summary().phase==Phase::Occupied);
        event(s,K::SaveRequested,"10000003");
        assert(s.summary().phase==Phase::Occupied);
        event(s,K::SaveRequested,first); event(s,K::SaveRequested,first);
        assert(s.snapshot()==Availability::Occupied && s.summary().phase==Phase::Occupied);
        event(s,K::IdleArchiveMounted); assert(s.snapshot()==Availability::Occupied);
        event(s,K::LogoutRequested,second);
        assert(s.snapshot()==Availability::Occupied && s.summary().phase==Phase::Ending);
        event(s,K::IdleArchiveOpened); assert(s.snapshot()==Availability::Occupied);
        event(s,K::IdleArchiveMounted); assert(s.snapshot()==Availability::Free);
        // Clearing the cabinet must clear both identities and ending markers.
        login(s,second); event(s,K::LogoutRequested,first);
        assert(s.summary().phase==Phase::Occupied);
        event(s,K::SaveRequested,second); assert(s.summary().phase==Phase::Ending);
        idle(s); assert(s.snapshot()==Availability::Free);
    }
    // A requested refresh uses its existing slot, even with both slots occupied.
    StatusState refresh; idle(refresh); login(refresh,ids[0]); login(refresh,ids[1]);
    event(refresh,K::SaveRequested,ids[0]); event(refresh,K::LogoutRequested,ids[1]);
    assert(refresh.summary().phase==Phase::Ending);
    event(refresh,K::GetRequested); event(refresh,K::ProfileLoaded,ids[0]);
    assert(refresh.snapshot()==Availability::Occupied && refresh.summary().phase==Phase::Occupied);
    event(refresh,K::SaveRequested,ids[0]); assert(refresh.summary().phase==Phase::Ending);
    idle(refresh); assert(refresh.snapshot()==Availability::Free);
    // Two pending replies can refresh one identity and introduce the other in either order.
    for(bool reverse:{false,true}) {
        StatusState s; idle(s); login(s,ids[0]);
        event(s,K::GetRequested); event(s,K::GetRequested); event(s,K::PlayStarted);
        event(s,K::ProfileLoaded,ids[reverse?1:0]);
        event(s,K::ProfileLoaded,ids[reverse?0:1]);
        assert(s.snapshot()==Availability::Occupied);
        event(s,K::LogoutRequested,ids[0]); assert(s.summary().phase==Phase::Occupied);
        event(s,K::SaveRequested,ids[1]); assert(s.summary().phase==Phase::Ending);
        idle(s); assert(s.snapshot()==Availability::Free);
    }
}
void iidx_rejection_cases() {
    // An unsolicited response is invalid even when it repeats an active identity.
    for(const auto *id:{"10000001","10000002"}) {
        StatusState s; idle(s); login(s); event(s,K::ProfileLoaded,id);
        assert(s.snapshot()==Availability::Unavailable);
        event(s,K::PlayStarted); idle(s); assert(s.snapshot()==Availability::Unavailable);
    }
    StatusState unsolicited; idle(unsolicited); event(unsolicited,K::ProfileLoaded,"10000001");
    assert(unsolicited.snapshot()==Availability::Unavailable);
    StatusState overflow; idle(overflow);
    event(overflow,K::GetRequested); event(overflow,K::GetRequested); event(overflow,K::GetRequested);
    assert(overflow.snapshot()==Availability::Unavailable);
    event(overflow,K::ProfileLoaded,"10000001"); idle(overflow);
    assert(overflow.snapshot()==Availability::Unavailable);
    StatusState third; idle(third); login(third); login(third,"10000002");
    event(third,K::SaveRequested,"10000001");
    event(third,K::GetRequested); event(third,K::ProfileLoaded,"10000003");
    assert(third.snapshot()==Availability::Unavailable);
    idle(third); event(third,K::PlayStarted); assert(third.snapshot()==Availability::Unavailable);
    StatusState excess_reply; idle(excess_reply);
    event(excess_reply,K::GetRequested); event(excess_reply,K::GetRequested);
    event(excess_reply,K::ProfileLoaded,"10000001"); event(excess_reply,K::ProfileLoaded,"10000001");
    assert(excess_reply.snapshot()==Availability::Occupied);
    event(excess_reply,K::ProfileLoaded,"10000002");
    assert(excess_reply.snapshot()==Availability::Unavailable);
    // One answered request cannot hide a second abandoned request, even after playstart.
    StatusState abandoned; idle(abandoned);
    event(abandoned,K::GetRequested); event(abandoned,K::GetRequested);
    event(abandoned,K::ProfileLoaded,"10000001"); event(abandoned,K::PlayStarted); idle(abandoned);
    assert(abandoned.snapshot()==Availability::Unavailable);
    StatusState failed; idle(failed); event(failed,K::GetRequested);
    event(failed,K::PlayStarted); event(failed,K::ProfileLoaded,"10000001",false); idle(failed);
    assert(failed.snapshot()==Availability::Unavailable);
    for(const auto *id:{"","1000X001","a1B2c3D4e5F60718"}) {
        StatusState invalid; idle(invalid); event(invalid,K::GetRequested);
        Observation response{K::ProfileLoaded,invalid.generation(),{},true};
        const std::string_view value(id);
        std::copy(value.begin(),value.end(),response.profile_id.begin()); invalid.apply(response);
        assert(invalid.snapshot()==Availability::Unavailable);
        event(invalid,K::PlayStarted); idle(invalid); assert(invalid.snapshot()==Availability::Unavailable);
    }
}
void two_profile_resource_cases() {
    StatusState s; idle(s); login(s);
    event(s,K::IdleArchiveOpened);
    Observation stale_mount{K::IdleArchiveMounted,s.generation(),{},true};
    event(s,K::GetRequested); event(s,K::ProfileLoaded,"10000002");
    s.apply(stale_mount); event(s,K::IdleArchiveMounted);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::SaveRequested,"10000001"); event(s,K::LogoutRequested,"10000002");
    event(s,K::IdleArchiveOpened,"",false); event(s,K::IdleArchiveMounted);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::IdleArchiveOpened); event(s,K::IdleArchiveMounted,"",false);
    assert(s.snapshot()==Availability::Occupied);
    idle(s); assert(s.snapshot()==Availability::Free);
    login(s,"10000002"); login(s,"10000001"); s.apply(stale_mount);
    assert(s.snapshot()==Availability::Occupied);
    event(s,K::CaptureFault); idle(s); event(s,K::PlayStarted);
    assert(s.snapshot()==Availability::Unavailable);
}
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
    // SDVX retains one request identity and must reject overlapping or replacement loads.
    StatusState overlap; idle(overlap);
    event(overlap,K::GetRequested,refid); event(overlap,K::GetRequested,other);
    assert(overlap.snapshot()==Availability::Unavailable);
    event(overlap,K::ProfileLoaded); idle(overlap); assert(overlap.snapshot()==Availability::Unavailable);
    StatusState replacement; idle(replacement);
    event(replacement,K::GetRequested,refid); event(replacement,K::ProfileLoaded);
    event(replacement,K::GetRequested,other); event(replacement,K::ProfileLoaded);
    assert(replacement.snapshot()==Availability::Unavailable);
    idle(replacement); assert(replacement.snapshot()==Availability::Unavailable);
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
    guest_state_cases(); two_profile_state_cases(); iidx_rejection_cases(); two_profile_resource_cases();
    StatusState abandoned; idle(abandoned); event(abandoned,K::GetRequested); idle(abandoned);
    assert(abandoned.snapshot()==Availability::Unavailable); login(abandoned); idle(abandoned);
    assert(abandoned.snapshot()==Availability::Unavailable);
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
