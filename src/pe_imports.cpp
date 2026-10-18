// SPDX-License-Identifier: GPL-3.0-only
#include "pe_imports.h"
#include <cstring>
#include <limits>

namespace cabinet {
namespace {
constexpr uint64_t ordinal_flag = uint64_t{1} << 63;
constexpr size_t max_slots_per_api = 64;

bool fail(std::string &error, const char *message) {
    error = message;
    return false;
}

struct Image {
    std::span<const std::byte> bytes;
    size_t optional{};
    uint32_t directories{};

    bool contains(size_t offset, size_t count) const noexcept {
        return offset <= bytes.size() && count <= bytes.size() - offset;
    }
    uint16_t u16(size_t offset) const noexcept {
        return uint16_t(std::to_integer<uint8_t>(bytes[offset])) |
               uint16_t(uint16_t(std::to_integer<uint8_t>(bytes[offset + 1])) << 8);
    }
    uint32_t u32(size_t offset) const noexcept {
        return uint32_t(u16(offset)) | (uint32_t(u16(offset + 2)) << 16);
    }
    uint64_t u64(size_t offset) const noexcept {
        return uint64_t(u32(offset)) | (uint64_t(u32(offset + 4)) << 32);
    }
    bool array(uint32_t rva, uint32_t count, size_t width) const noexcept {
        return count == 0 || (rva != 0 && rva <= bytes.size() &&
                              count <= (bytes.size() - rva) / width);
    }
    bool string(size_t rva, std::string_view &value) const noexcept {
        if (rva == 0 || rva >= bytes.size()) return false;
        const char *start = reinterpret_cast<const char *>(bytes.data() + rva);
        const void *end = std::memchr(start, 0, bytes.size() - rva);
        if (!end) return false;
        value = {start, static_cast<size_t>(static_cast<const char *>(end) - start)};
        return true;
    }
    bool directory(uint32_t index, uint32_t &rva, uint32_t &size) const noexcept {
        rva = size = 0;
        if (index >= directories) return true;
        rva = u32(optional + 112 + size_t(index) * 8);
        size = u32(optional + 116 + size_t(index) * 8);
        return (rva == 0 && size == 0) || (rva != 0 && size != 0 && contains(rva, size));
    }
};

bool load_image(std::span<const std::byte> bytes, Image &image,
                PeIdentity &identity, std::string &error) {
    Image input{bytes};
    if (!input.contains(0, 64) || input.u16(0) != 0x5a4d)
        return fail(error, "invalid DOS header");
    const size_t nt = input.u32(0x3c);
    if (nt < 64 || !input.contains(nt, 24) || input.u32(nt) != 0x00004550)
        return fail(error, "invalid NT header");
    if (input.u16(nt + 4) != 0x8664)
        return fail(error, "unsupported PE machine");
    const size_t optional = nt + 24;
    const size_t optional_size = input.u16(nt + 20);
    if (optional_size < 112 || !input.contains(optional, optional_size) ||
        input.u16(optional) != 0x20b)
        return fail(error, "invalid PE32+ header");
    const uint32_t size = input.u32(optional + 56);
    const uint32_t headers = input.u32(optional + 60);
    const uint32_t entry = input.u32(optional + 16);
    const uint32_t directories = input.u32(optional + 108);
    const size_t section_table = optional + optional_size;
    const size_t section_size = size_t(input.u16(nt + 6)) * 40;
    if (size == 0 || size > bytes.size() || headers > size ||
        section_table > headers || section_size > headers - section_table ||
        entry >= size || directories > (optional_size - 112) / 8)
        return fail(error, "invalid PE image bounds");
    image = {bytes.first(size), optional, directories};
    identity = {0x8664, input.u32(nt + 8), entry, size};
    return true;
}

char ascii_lower(char ch) noexcept {
    return ch >= 'A' && ch <= 'Z' ? char(ch + ('a' - 'A')) : ch;
}
bool same_library(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    return true;
}
} // namespace

bool pe_identity(std::span<const std::byte> bytes, PeIdentity &out, std::string &error) {
    error.clear();
    Image image;
    PeIdentity identity;
    if (!load_image(bytes, image, identity, error)) return false;
    out = identity;
    return true;
}

bool pe_export_ordinal(std::span<const std::byte> bytes, std::string_view name,
                       uint16_t &out, std::string &error) {
    error.clear();
    Image image;
    PeIdentity identity;
    if (!load_image(bytes, image, identity, error)) return false;
    uint32_t rva{}, size{};
    if (!image.directory(0, rva, size) || size < 40)
        return fail(error, "invalid export directory");
    const uint32_t library_name_rva = image.u32(size_t(rva) + 12);
    std::string_view library_name;
    if (library_name_rva != 0 &&
        (!image.string(library_name_rva, library_name) || library_name.empty()))
        return fail(error, "invalid export library name");
    const uint32_t base = image.u32(size_t(rva) + 16);
    const uint32_t function_count = image.u32(size_t(rva) + 20);
    const uint32_t name_count = image.u32(size_t(rva) + 24);
    const uint32_t functions = image.u32(size_t(rva) + 28);
    const uint32_t names = image.u32(size_t(rva) + 32);
    const uint32_t ordinals = image.u32(size_t(rva) + 36);
    if (!image.array(functions, function_count, 4) ||
        !image.array(names, name_count, 4) || !image.array(ordinals, name_count, 2))
        return fail(error, "invalid export arrays");
    for (uint32_t i = 0; i < function_count; ++i) {
        const uint32_t function = image.u32(size_t(functions) + size_t(i) * 4);
        if (function != 0 && !image.contains(function, 1))
            return fail(error, "invalid export function RVA");
        if (function >= rva && uint64_t(function) < uint64_t(rva) + size) {
            std::string_view forwarder;
            if (!image.string(function, forwarder) || forwarder.empty() ||
                forwarder.size() >= uint64_t(rva) + size - function)
                return fail(error, "invalid export forwarder");
        }
    }
    bool found = false;
    uint16_t result{};
    for (uint32_t i = 0; i < name_count; ++i) {
        std::string_view candidate;
        const uint32_t name_rva = image.u32(size_t(names) + size_t(i) * 4);
        const uint16_t index = image.u16(size_t(ordinals) + size_t(i) * 2);
        if (!image.string(name_rva, candidate) || candidate.empty() ||
            index >= function_count ||
            image.u32(size_t(functions) + size_t(index) * 4) == 0)
            return fail(error, "invalid named export");
        if (candidate == name) {
            const uint64_t ordinal = uint64_t(base) + index;
            if (found || ordinal > std::numeric_limits<uint16_t>::max())
                return fail(error, "invalid export ordinal");
            found = true;
            result = static_cast<uint16_t>(ordinal);
        }
    }
    if (!found) return fail(error, "export not found");
    out = result;
    return true;
}

bool pe_import_slots(std::span<const std::byte> bytes, std::string_view library,
                     const std::array<ImportTarget, 3> &targets,
                     std::vector<ImportSlot> &out, std::string &error) {
    out.clear();
    error.clear();
    Image image;
    PeIdentity identity;
    if (!load_image(bytes, image, identity, error)) return false;
    uint32_t directory{}, size{};
    if (!image.directory(1, directory, size))
        return fail(error, "invalid import directory");
    if (size == 0) return true;
    std::array<ImportSlot, max_slots_per_api * 3> slots{};
    std::array<size_t, 3> counts{};
    size_t slot_count = 0;
    bool terminated = false;
    for (size_t offset = 0; offset <= size && size - offset >= 20; offset += 20) {
        const size_t descriptor = size_t(directory) + offset;
        const uint32_t original = image.u32(descriptor);
        const uint32_t stamp = image.u32(descriptor + 4);
        const uint32_t chain = image.u32(descriptor + 8);
        const uint32_t name_rva = image.u32(descriptor + 12);
        const uint32_t first = image.u32(descriptor + 16);
        if ((original | stamp | chain | name_rva | first) == 0) {
            terminated = true;
            break;
        }
        std::string_view imported_library;
        if (!image.string(name_rva, imported_library) || imported_library.empty())
            return fail(error, "invalid import library name");
        if (!same_library(imported_library, library)) continue;
        if (first == 0 || first % 8 != 0 || !image.contains(first, 8) ||
            (original != 0 && (original % 8 != 0 || !image.contains(original, 8))))
            return fail(error, "invalid import thunk RVA");
        const size_t lookup = original != 0 ? original : first;
        const size_t limit = (image.bytes.size() - lookup) / 8;
        bool thunk_terminated = false;
        for (size_t i = 0; i < limit; ++i) {
            const size_t iat = size_t(first) + i * 8;
            if (!image.contains(iat, 8)) return fail(error, "unterminated import IAT");
            const uint64_t value = image.u64(lookup + i * 8);
            const uint64_t predecessor = image.u64(iat);
            if (value == 0) {
                if (predecessor != 0) return fail(error, "mismatched import terminator");
                thunk_terminated = true;
                break;
            }
            if (predecessor == 0) return fail(error, "null import predecessor");
            size_t matched = targets.size();
            std::string_view function_name;
            uint16_t ordinal{};
            const bool by_ordinal = original != 0 && (value & ordinal_flag) != 0;
            if (original != 0) {
                if (by_ordinal) {
                    if ((value & ~(ordinal_flag | uint64_t{0xffff})) != 0)
                        return fail(error, "invalid import ordinal");
                    ordinal = static_cast<uint16_t>(value);
                } else {
                    if (value > std::numeric_limits<uint32_t>::max() ||
                        !image.contains(static_cast<size_t>(value), 3) ||
                        !image.string(static_cast<size_t>(value) + 2, function_name) ||
                        function_name.empty())
                        return fail(error, "invalid import function name");
                }
            }
            for (size_t api = 0; api < targets.size(); ++api) {
                const bool match = original == 0
                    ? targets[api].direct_address != 0 && predecessor == targets[api].direct_address
                    : by_ordinal ? ordinal == targets[api].ordinal : function_name == targets[api].name;
                if (!match) continue;
                if (matched != targets.size()) return fail(error, "ambiguous import identity");
                matched = api;
            }
            if (matched == targets.size()) {
                if (original == 0) return fail(error, "unverifiable import without OFT");
                continue;
            }
            if (counts[matched] == max_slots_per_api)
                return fail(error, "too many import slots");
            ++counts[matched];
            slots[slot_count++] = {static_cast<AvsApi>(matched), static_cast<uint32_t>(iat), predecessor};
        }
        if (!thunk_terminated) return fail(error, "unterminated import thunks");
    }
    if (!terminated) return fail(error, "unterminated import descriptors");
    out.assign(slots.begin(), slots.begin() + slot_count);
    return true;
}
} // namespace cabinet
