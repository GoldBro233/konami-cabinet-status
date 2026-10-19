// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game_identity.h"
#include <atomic>
#include <array>
#include <mutex>
#include <vector>
#include <utility>
namespace cabinet {
class IatHooks {
public:
    explicit IatHooks(StatusState &state):state_(state) {}
    bool install(const GameIdentity &,HMODULE self,std::vector<std::string> &diagnostics,std::string &error);
    bool check() noexcept;
    bool first_decode_fault(DecodeDiagnostic &) const noexcept;
    void disable() noexcept { observing_.store(false,std::memory_order_release); }
private:
    struct Slot { void *volatile *address{}; void *predecessor{}; void *wrapper{}; };
    template<size_t I> static int32_t destroy_thunk(Property);
    template<size_t I> static uint32_t open_thunk(const char *,uint16_t,int);
    template<size_t I> static int mount_thunk(const char *,const char *,const char *,void *);
    template<size_t... I> static auto destroy_wrappers(std::index_sequence<I...>);
    template<size_t... I> static auto open_wrappers(std::index_sequence<I...>);
    template<size_t... I> static auto mount_wrappers(std::index_sequence<I...>);
    void rollback() noexcept;
    void record_decode_fault(const DecodeDiagnostic &) noexcept;
    StatusState &state_;
    // Keep decoding and publication in one order across game/network threads.
    std::mutex observation_mutex_;
    AvsFunctions functions_{};
    GameKind game_=GameKind::Unknown;
    std::array<std::array<Slot,64>,3> slots_{};
    std::array<size_t,3> counts_{};
    std::atomic<bool> observing_{false};
    // 0=empty, 1=writer owns payload, 2=immutable payload published.
    std::atomic<unsigned char> decode_fault_state_{0};
    DecodeDiagnostic decode_fault_{};
    static std::atomic<IatHooks *> live_;
};
}
