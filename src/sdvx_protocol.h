// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
namespace cabinet {
bool valid_sdvx_refid(const std::array<char,32> &) noexcept;
bool is_sdvx_idle_path(const char *) noexcept;
bool is_sdvx_idle_mount(const char *mountpoint,const char *fstype) noexcept;
}
