# Dependency sources and notices

The project is distributed under GPL-3.0-only; see LICENSE. Original game logs are private inputs and must never be included in source or binary distributions.

## CivetWeb

Upstream: https://github.com/civetweb/civetweb
Version: v1.16, commit d7ba35bbb649209c66e582d5a0244ba988a15159.
The vendored C implementation, referenced .inl files, public C header, and full LICENSE.md are retained under third_party/civetweb. License: MIT (including the upstream third-party notices in LICENSE.md).

Local modification: in src/civetweb.c, set_ports_option's Windows SO_EXCLUSIVEADDRUSE failure branch retains the diagnostic, closes the socket, sets INVALID_SOCKET, and continues. The existing portsOk != portsTotal check then rejects incomplete startup. No other vendor changes are intended.

## Spice SDK and AVS ABI

Upstream: https://github.com/spice2x/spice2x.github.io
Version: 26-09-12, commit 2b6e76730b3898a54d5126c0cb5bf67a4a43aefa.
third_party/spice-sdk/spicesdk.h is the complete, unchanged src/spice2x/sdk/include/spicesdk.h at that commit (table ends at hook_library). GPLv3 notice retained in this source distribution.

AVS function declarations, enum values and 2.17 export names are referenced from src/spice2x/avs/core.h and core.cpp at that commit. src/spice2x/avs/automap.cpp supplies the precedent for observing property objects immediately before destruction. No internal AVS object layout, binary patch, or node mutation is used.

The required accessors share the same named exports in core.cpp's IMPORT_AVS21700 (2.17.0.0) and IMPORT_AVS21730 (2.17.3.0) tables. The local Windows IIDX evidence verifies the same family on file version 2.17.4.0. These three file versions map to the XCgsqzn/2.17 ABI; other versions are not inferred from export-name presence alone. The linked SDK header remains a pinned source dependency, not an exact runtime spice2x release requirement.
