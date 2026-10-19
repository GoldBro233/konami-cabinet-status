// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace cabinet {
enum class Phase { Starting, Free, Occupied, Ending, Unknown, Stopping };
enum class Availability { Unavailable, Free, Occupied };
enum class Reason { Boot, GetRequested, ProfileLoaded, LogoutRequested, SaveRequested, IdleLoaded,
                    OverlappingGet, AbandonedGet, UnexpectedProfile, CaptureFault, Stopping };
struct Observation {
    enum class Kind { GetRequested, ProfileLoaded, LogoutRequested, SaveRequested,
                      IdleArchiveOpened, IdleArchiveMounted, CaptureFault };
    Kind kind{};
    uint64_t generation{};
    // A request may supply the session identity; an empty loaded ID then uses it.
    std::array<char,32> profile_id{};
    bool success{};
};
struct StateSummary { uint64_t revision; Phase phase; Reason reason; };
const char *phase_name(Phase) noexcept;
const char *reason_name(Reason) noexcept;
class StatusState {
public:
    void apply(const Observation &) noexcept;
    Availability snapshot() const noexcept { return availability_.load(std::memory_order_acquire); }
    uint64_t generation() const noexcept { return generation_.load(std::memory_order_acquire); }
    StateSummary summary() const;
    void stop() noexcept;
private:
    void publish(Phase, Reason) noexcept;
    void advance() noexcept;
    void fault(Reason) noexcept;
    mutable std::mutex mutex_;
    std::atomic<Availability> availability_{Availability::Unavailable};
    std::atomic<uint64_t> generation_{0};
    StateSummary summary_{0, Phase::Starting, Reason::Boot};
    std::array<char,32> active_{};
    std::array<char,32> pending_profile_{};
    bool pending_get_{};
    bool pending_open_{};
};
}
