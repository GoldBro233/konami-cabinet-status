// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace cabinet {
enum class AvsApi : size_t { Destroy, Open, Mount };
struct PeIdentity { uint16_t machine{}; uint32_t timestamp{},entry_point{},image_size{}; };
struct ImportTarget { std::string_view name; uint16_t ordinal{}; uint64_t direct_address{}; };
struct ImportSlot { AvsApi api{}; uint32_t rva{}; uint64_t predecessor{}; };
// Inputs are a loaded PE64 image span, never an on-disk PE file.
bool pe_identity(std::span<const std::byte>,PeIdentity &,std::string &error);
bool pe_export_ordinal(std::span<const std::byte>,std::string_view,uint16_t &,std::string &error);
bool pe_import_slots(std::span<const std::byte>,std::string_view library,
                     const std::array<ImportTarget,3> &,std::vector<ImportSlot> &,std::string &error);
}
