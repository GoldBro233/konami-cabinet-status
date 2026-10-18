// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace cabinet {
struct Config { bool enabled=true; uint16_t port=18080; std::string bind_address="127.0.0.1"; };
// On success error is empty, except for a single unknown-key/section warning.
bool parse_config(std::string_view text,Config &out,std::string &error);
}
