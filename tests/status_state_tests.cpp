// SPDX-License-Identifier: GPL-3.0-only
#include "status_state.h"
#include "iidx_protocol.h"
#include <cassert>
#include <cstdio>
using namespace cabinet;
using K=Observation::Kind;
void event(StatusState &s,K k,const char *id="",bool success=true) {
    Observation e{k,s.generation(),{},success};
    if(*id) assert(copy_profile_id(id,e.profile_id));
    s.apply(e);
}
void idle(StatusState &s) { event(s,K::IdleArchiveOpened); event(s,K::IdleArchiveMounted); }
void login(StatusState &s,const char *id="10000001") { event(s,K::GetRequested); event(s,K::ProfileLoaded,id); }
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
    for(int i=0;i<1000;++i) assert(s.snapshot()==Availability::Occupied);
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
    std::puts("state trajectories and resource boundaries passed");
}
