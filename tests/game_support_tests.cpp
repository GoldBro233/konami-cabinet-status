// SPDX-License-Identifier: GPL-3.0-only
#include "game_support.h"
#include <cassert>
#include <cstdio>
using namespace cabinet;
int main() {
    assert(detect_game("LDJ")==GameKind::Iidx);
    assert(detect_game("KFC")==GameKind::Sdvx);
    assert(detect_game("MDX")==GameKind::Unknown);
    assert(detect_game("")==GameKind::Unknown);
    assert(detect_game("LDJ:J:D:X:2026081900")==GameKind::Unknown);
    for(const auto &version:{AvsVersion{2,17,0,0},AvsVersion{2,17,3,0},AvsVersion{2,17,4,0}})
        assert(compatible_avs_abi(version)==AvsAbi::Xcgsqzn217);
    for(const auto &version:{AvsVersion{2,16,8,1},AvsVersion{2,18,0,0},AvsVersion{2,17,4,1},
                             AvsVersion{2,17,30,0},AvsVersion{0,0,0,0}})
        assert(compatible_avs_abi(version)==AvsAbi::Unsupported);
    std::puts("IIDX/SDVX model selection and supported AVS ABI boundaries passed");
}
