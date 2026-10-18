// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "status_state.h"
#include <string_view>
namespace cabinet {
bool valid_profile_id(const std::array<char,32> &) noexcept;
bool copy_profile_id(std::string_view, std::array<char,32> &) noexcept;
bool is_idle_path(const char *) noexcept;
bool is_idle_mount(const char *mountpoint, const char *fstype) noexcept;
}
