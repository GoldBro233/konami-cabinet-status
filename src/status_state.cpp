// SPDX-License-Identifier: GPL-3.0-only
#include "status_state.h"
#include "iidx_protocol.h"
#include "sdvx_protocol.h"
namespace cabinet {
const char *phase_name(Phase p) noexcept {
    switch(p) {
    case Phase::Starting:return "starting"; case Phase::Free:return "free";
    case Phase::Occupied:return "occupied"; case Phase::Ending:return "ending";
    case Phase::Unknown:return "unknown"; case Phase::Stopping:return "stopping";
    } return "unknown";
}
const char *reason_name(Reason r) noexcept {
    switch(r) {
    case Reason::Boot:return "boot"; case Reason::GetRequested:return "get_requested";
    case Reason::ProfileLoaded:return "profile_loaded"; case Reason::LogoutRequested:return "logout_requested";
    case Reason::SaveRequested:return "save_requested"; case Reason::IdleLoaded:return "idle_loaded";
    case Reason::OverlappingGet:return "overlapping_get"; case Reason::AbandonedGet:return "abandoned_get";
    case Reason::UnexpectedProfile:return "unexpected_profile"; case Reason::CaptureFault:return "capture_fault";
    case Reason::Stopping:return "stopping";
    } return "capture_fault";
}
void StatusState::publish(Phase p,Reason r) noexcept {
    if(summary_.phase!=p || summary_.reason!=r) { ++summary_.revision; summary_.phase=p; summary_.reason=r; }
    const auto a=(p==Phase::Free)?Availability::Free:
        ((p==Phase::Occupied || p==Phase::Ending)?Availability::Occupied:Availability::Unavailable);
    availability_.store(a,std::memory_order_release);
}
void StatusState::advance() noexcept { pending_open_=false; generation_.fetch_add(1,std::memory_order_release); }
void StatusState::fault(Reason reason) noexcept { advance(); publish(Phase::Unknown,reason); }
StateSummary StatusState::summary() const { std::lock_guard lock(mutex_); return summary_; }
void StatusState::stop() noexcept { std::lock_guard lock(mutex_); advance(); publish(Phase::Stopping,Reason::Stopping); }
void StatusState::apply(const Observation &e) noexcept {
    std::lock_guard lock(mutex_);
    if(summary_.phase==Phase::Stopping || summary_.phase==Phase::Unknown) return;
    using K=Observation::Kind;
    if(e.kind==K::CaptureFault) { fault(Reason::CaptureFault); return; }
    if(e.generation!=generation_.load(std::memory_order_relaxed)) {
        if(e.kind!=K::IdleArchiveOpened && e.kind!=K::IdleArchiveMounted) fault(Reason::CaptureFault);
        return;
    }
    switch(e.kind) {
    case K::GetRequested:
        if(pending_get_) { fault(Reason::OverlappingGet); return; }
        if(e.profile_id[0] && !valid_sdvx_refid(e.profile_id)) { fault(Reason::CaptureFault); return; }
        pending_profile_=e.profile_id; pending_get_=true; advance();
        publish(active_[0]?summary_.phase:Phase::Starting,Reason::GetRequested); return;
    case K::ProfileLoaded: {
        const bool request_identity=pending_profile_[0]!=0;
        if(!e.success || (request_identity ? e.profile_id[0]!=0 : !valid_profile_id(e.profile_id))) {
            fault(Reason::CaptureFault); return;
        }
        const auto &id=request_identity ? pending_profile_ : e.profile_id;
        if(!pending_get_ || (active_[0] && active_!=id)) { fault(Reason::UnexpectedProfile); return; }
        active_=id; pending_profile_.fill(0); pending_get_=false;
        advance(); publish(Phase::Occupied,Reason::ProfileLoaded); return;
    }
    case K::LogoutRequested:
    case K::SaveRequested:
        if(!valid_profile_id(e.profile_id) && !valid_sdvx_refid(e.profile_id)) { fault(Reason::CaptureFault); return; }
        if(active_[0] && active_==e.profile_id) publish(Phase::Ending,e.kind==K::SaveRequested?Reason::SaveRequested:Reason::LogoutRequested);
        return;
    case K::IdleArchiveOpened:
        if(e.success) pending_open_=true;
        return;
    case K::IdleArchiveMounted:
        if(!e.success || !pending_open_) return;
        advance();
        if(pending_get_) { fault(Reason::AbandonedGet); return; }
        active_.fill(0); publish(Phase::Free,Reason::IdleLoaded); return;
    case K::CaptureFault:return;
    }
}
}
