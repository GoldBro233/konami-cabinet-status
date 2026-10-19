// SPDX-License-Identifier: GPL-3.0-only
// ABI: spice2x 26-09-12, commit 2b6e76730b3898a54d5126c0cb5bf67a4a43aefa, avs/core.h.
#pragma once
#include "status_state.h"
#include "game_support.h"
#include <cstdint>
namespace cabinet {
using Property=void *;
using Node=void *;
struct AvsFunctions {
    int32_t (*destroy)(Property){};
    Node (*search)(Property,Node,const char *){};
    Node (*traverse)(Node,int){};
    int32_t (*name)(Node,char *,uint32_t){};
    int (*node_type)(Node){};
    int (*read)(Node,int,void *,uint32_t){};
    int (*refer)(Property,Node,const char *,int,void *,uint32_t){};
    uint32_t (*get_error)(Property){};
    Property (*clear_error)(Property){};
    uint32_t (*fs_open)(const char *,uint16_t,int){};
    int (*fs_mount)(const char *,const char *,const char *,void *){};
};
enum class DecodeKind { Ignore, Event, Fault };
enum class DecodePoint { None, Accessors, RootSearch, RootName, Services, RequestAttributes,
                         RequestMethod, GetCid, GetRid, SaveId, LogoutChildren, LogoutId,
                         ResponseChildren, ResponseAttributes, ResponseStatus, ResponseFault,
                         ProfileAttributes, ProfileId, RequestChildren, RequestRefId, ResponseResult };
enum class DecodeIssue { None, Missing, Duplicate, WrongType, AccessorFailed, Unterminated,
                         Empty, InvalidProfile, TraversalLimit, AvsError, ObserverException };
struct DecodeDiagnostic {
    DecodePoint point=DecodePoint::None;
    DecodeIssue issue=DecodeIssue::None;
    int observed_type=-1;
    int accessor_result=0;
    uint32_t avs_error=0;
    bool network_root=false;
};
const char *decode_point_name(DecodePoint) noexcept;
const char *decode_issue_name(DecodeIssue) noexcept;
struct DecodeOutcome {
    DecodeKind kind=DecodeKind::Ignore;
    Observation event{};
    DecodeDiagnostic diagnostic{};
};
DecodeOutcome decode_property(Property,const AvsFunctions &,GameKind) noexcept;
}
