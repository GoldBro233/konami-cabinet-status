// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace cabinet {
enum class GameKind { Unknown, Iidx, Sdvx };
enum class AvsAbi { Unsupported, Xcgsqzn217 };
using AvsVersion=std::array<uint16_t,4>;
GameKind detect_game(std::string_view model) noexcept;
const char *game_name(GameKind) noexcept;
AvsAbi compatible_avs_abi(const AvsVersion &) noexcept;
const char *avs_abi_name(AvsAbi) noexcept;
}
