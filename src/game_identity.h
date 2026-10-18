// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "avs_capture.h"
#include "pe_imports.h"
#include "spicesdk.h"
#include <windows.h>
#include <span>
#include <string>
namespace cabinet {
struct GameIdentity { HMODULE game{},avs{}; AvsFunctions functions{}; std::array<ImportTarget,3> targets{}; };
bool loaded_image(HMODULE,std::span<const std::byte> &,std::string &error);
bool identify_game(const SPICE_SDK_V0 &,GameIdentity &,std::string &diagnostic,std::string &error);
}
