// SPDX-License-Identifier: GPL-3.0-only
#include "iat_hooks.h"
#include "iidx_protocol.h"
#include "iat_slot.h"
#include <psapi.h>
#include <cstring>
#include <utility>
namespace cabinet {
std::atomic<IatHooks *> IatHooks::live_{nullptr};
namespace {
thread_local unsigned destroy_depth=0,open_depth=0,mount_depth=0;
struct Depth {
    unsigned &value; bool outer;
    explicit Depth(unsigned &v):value(v),outer(v++==0) {}
    ~Depth() { --value; }
};
// The predecessor is immutable and prepared before any wrapper is published.
bool exchange_pointer(void *volatile *slot,void *expected,void *desired,std::string *error) noexcept {
    DWORD protection=0,unused=0;
    if(!VirtualProtect(const_cast<void **>(slot),sizeof(void *),PAGE_READWRITE,&protection)) {
        if(error) *error="IAT VirtualProtect failed"; return false;
    }
    void *previous=InterlockedCompareExchangePointer(slot,desired,expected);
    bool restored=VirtualProtect(const_cast<void **>(slot),sizeof(void *),protection,&unused)!=0;
    if(!restored) {
        // A second protection restoration does not modify slot ownership.
        (void)VirtualProtect(const_cast<void **>(slot),sizeof(void *),protection,&unused);
        if(error) *error="IAT page protection restoration failed"; return false;
    }
    if(previous!=expected) { if(error) *error="IAT changed during installation"; return false; }
    return true;
}
}
template<size_t I> int32_t IatHooks::destroy_thunk(Property property) {
    IatHooks &h=*live_.load(std::memory_order_acquire);
    const auto predecessor=reinterpret_cast<decltype(AvsFunctions::destroy)>(h.slots_[0][I].predecessor);
    Depth depth(destroy_depth);
    if(depth.outer && h.observing_.load(std::memory_order_acquire)) {
        const auto generation=h.state_.generation();
        auto outcome=decode_property(property,h.functions_);
        if(h.observing_.load(std::memory_order_acquire)) {
            if(outcome.kind==DecodeKind::Fault) {
                h.record_decode_fault(outcome.diagnostic);
                h.state_.apply({Observation::Kind::CaptureFault,generation,{},false});
            }
            else if(outcome.kind==DecodeKind::Event) { outcome.event.generation=generation; h.state_.apply(outcome.event); }
        }
    }
    return predecessor(property);
}
template<size_t I> uint32_t IatHooks::open_thunk(const char *path,uint16_t mode,int flags) {
    IatHooks &h=*live_.load(std::memory_order_acquire);
    auto predecessor=reinterpret_cast<decltype(AvsFunctions::fs_open)>(h.slots_[1][I].predecessor);
    Depth depth(open_depth);
    const bool capture=depth.outer && h.observing_.load(std::memory_order_acquire);
    const auto generation=capture?h.state_.generation():0;
    const bool matched=capture && mode==1 && is_idle_path(path);
    const auto result=predecessor(path,mode,flags);
    if(matched && h.observing_.load(std::memory_order_acquire))
        h.state_.apply({Observation::Kind::IdleArchiveOpened,generation,{},(result&0x80000000u)==0});
    return result;
}
template<size_t I> int IatHooks::mount_thunk(const char *mountpoint,const char *fsroot,const char *fstype,void *data) {
    IatHooks &h=*live_.load(std::memory_order_acquire);
    auto predecessor=reinterpret_cast<decltype(AvsFunctions::fs_mount)>(h.slots_[2][I].predecessor);
    Depth depth(mount_depth);
    const bool capture=depth.outer && h.observing_.load(std::memory_order_acquire);
    const auto generation=capture?h.state_.generation():0;
    const bool matched=capture && is_idle_mount(mountpoint,fstype);
    const auto result=predecessor(mountpoint,fsroot,fstype,data);
    if(matched && h.observing_.load(std::memory_order_acquire))
        h.state_.apply({Observation::Kind::IdleArchiveMounted,generation,{},result>0});
    return result;
}
template<size_t... I> auto IatHooks::destroy_wrappers(std::index_sequence<I...>) { return std::array{&destroy_thunk<I>...}; }
template<size_t... I> auto IatHooks::open_wrappers(std::index_sequence<I...>) { return std::array{&open_thunk<I>...}; }
template<size_t... I> auto IatHooks::mount_wrappers(std::index_sequence<I...>) { return std::array{&mount_thunk<I>...}; }
void IatHooks::rollback() noexcept {
    disable();
    for(size_t api=0;api<3;++api) for(size_t i=0;i<counts_[api];++i) {
        auto &s=slots_[api][i];
        if(load_iat_slot(s.address)==s.wrapper) (void)exchange_pointer(s.address,s.wrapper,s.predecessor,nullptr);
    }
}
bool IatHooks::install(const GameIdentity &identity,HMODULE self,std::vector<std::string> &diagnostics,std::string &error) {
    // Fetch the entire module snapshot before inspecting or modifying any slot.
    std::vector<HMODULE> modules(128); DWORD needed=0;
    for(;;) {
        if(!EnumProcessModules(GetCurrentProcess(),modules.data(),static_cast<DWORD>(modules.size()*sizeof(HMODULE)),&needed)) {
            error="EnumProcessModules failed"; return false;
        }
        if(needed<=modules.size()*sizeof(HMODULE)) { modules.resize(needed/sizeof(HMODULE)); break; }
        modules.resize(needed/sizeof(HMODULE)+16);
    }
    functions_=identity.functions;
    const auto destroy_table=destroy_wrappers(std::make_index_sequence<64>{});
    const auto open_table=open_wrappers(std::make_index_sequence<64>{});
    const auto mount_table=mount_wrappers(std::make_index_sequence<64>{});
    bool profile_caller=false;
    for(HMODULE module:modules) {
        if(module==identity.avs || module==self) continue;
        HMODULE held=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(module),&held)) {
            error="module snapshot changed during preparation"; return false;
        }
        struct Lease { HMODULE module; ~Lease(){ FreeLibrary(module); } } lease{held};
        std::span<const std::byte> image;
        if(!loaded_image(held,image,error)) return false;
        std::vector<ImportSlot> found;
        if(!pe_import_slots(image,"avs2-core.dll",identity.targets,found,error)) return false;
        if(found.empty()) continue;
        char name[MAX_PATH]{};
        if(!GetModuleBaseNameA(GetCurrentProcess(),held,name,MAX_PATH)) { error="cannot name IAT caller"; return false; }
        std::array<size_t,3> caller_counts{};
        for(const auto &import:found) {
            const size_t api=static_cast<size_t>(import.api);
            if(counts_[api]>=64) { error="IAT wrapper capacity exceeded"; return false; }
            auto &slot=slots_[api][counts_[api]];
            slot.address=reinterpret_cast<void *volatile *>(reinterpret_cast<uintptr_t>(held)+import.rva);
            slot.predecessor=reinterpret_cast<void *>(import.predecessor);
            slot.wrapper=api==0?reinterpret_cast<void *>(destroy_table[counts_[api]]):
                         api==1?reinterpret_cast<void *>(open_table[counts_[api]]):reinterpret_cast<void *>(mount_table[counts_[api]]);
            if(!slot.predecessor) { error="null AVS IAT predecessor"; return false; }
            ++counts_[api]; ++caller_counts[api];
        }
        if(caller_counts[0] && (!_stricmp(name,"bm2dx.dll") || !_stricmp(name,"avs2-ea3.dll"))) profile_caller=true;
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(held),&pinned)) {
            error="cannot pin IAT caller"; return false;
        }
        diagnostics.emplace_back(std::string("caller=")+name+" destroy="+std::to_string(caller_counts[0])+" open="+std::to_string(caller_counts[1])+" mount="+std::to_string(caller_counts[2]));
    }
    if(!counts_[0] || !counts_[1] || !counts_[2] || !profile_caller) { error="required AVS static import coverage missing"; return false; }
    HMODULE pinned_self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(self),&pinned_self)) {
        error="cannot pin cabinet-status DLL"; return false;
    }
    IatHooks *expected=nullptr;
    if(!live_.compare_exchange_strong(expected,this,std::memory_order_release,std::memory_order_relaxed)) { error="IAT runtime already published"; return false; }
    for(size_t api=0;api<3;++api) for(size_t i=0;i<counts_[api];++i) {
        auto &s=slots_[api][i];
        if(!exchange_pointer(s.address,s.predecessor,s.wrapper,&error)) { rollback(); return false; }
    }
    // Capture becomes active only when all prepared slots still belong to us.
    for(size_t api=0;api<3;++api) for(size_t i=0;i<counts_[api];++i)
        if(load_iat_slot(slots_[api][i].address)!=slots_[api][i].wrapper) { error="iat_replaced during installation"; rollback(); return false; }
    observing_.store(true,std::memory_order_release);
    diagnostics.emplace_back("capture installed modules/APIs: destroy="+std::to_string(counts_[0])+" open="+std::to_string(counts_[1])+" mount="+std::to_string(counts_[2]));
    return true;
}
bool IatHooks::check() noexcept {
    if(!observing_.load(std::memory_order_acquire)) return true;
    for(size_t api=0;api<3;++api) for(size_t i=0;i<counts_[api];++i)
        if(load_iat_slot(slots_[api][i].address)!=slots_[api][i].wrapper) {
            disable(); state_.apply({Observation::Kind::CaptureFault,state_.generation(),{},false}); return false;
        }
    return true;
}
void IatHooks::record_decode_fault(const DecodeDiagnostic &diagnostic) noexcept {
    unsigned char expected=0;
    if(!decode_fault_state_.compare_exchange_strong(expected,1,std::memory_order_acq_rel)) return;
    decode_fault_=diagnostic;
    decode_fault_state_.store(2,std::memory_order_release);
}
bool IatHooks::first_decode_fault(DecodeDiagnostic &diagnostic) const noexcept {
    if(decode_fault_state_.load(std::memory_order_acquire)!=2) return false;
    diagnostic=decode_fault_;
    return true;
}
}
