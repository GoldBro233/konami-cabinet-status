// SPDX-License-Identifier: GPL-3.0-only
#include "game_support.h"
namespace cabinet {
GameKind detect_game(std::string_view model) noexcept {
    if(model=="LDJ") return GameKind::Iidx;
    if(model=="KFC") return GameKind::Sdvx;
    return GameKind::Unknown;
}
const char *game_name(GameKind game) noexcept {
    switch(game) {
    case GameKind::Iidx:return "iidx";
    case GameKind::Sdvx:return "sdvx";
    case GameKind::Unknown:return "unknown";
    } return "unknown";
}
AvsAbi compatible_avs_abi(const AvsVersion &version) noexcept {
    // spice2x 26-09-12 core.cpp has the same required exports for 2.17.0.0
    // and 2.17.3.0. The provided Windows logs verify that family on 2.17.4.0.
    if(version==AvsVersion{2,17,0,0} || version==AvsVersion{2,17,3,0} ||
       version==AvsVersion{2,17,4,0}) return AvsAbi::Xcgsqzn217;
    return AvsAbi::Unsupported;
}
const char *avs_abi_name(AvsAbi abi) noexcept {
    switch(abi) {
    case AvsAbi::Xcgsqzn217:return "XCgsqzn/2.17";
    case AvsAbi::Unsupported:return "unsupported";
    } return "unsupported";
}
}
