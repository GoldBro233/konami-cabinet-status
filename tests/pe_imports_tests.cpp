// SPDX-License-Identifier: GPL-3.0-only
#include "pe_imports.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>

using namespace cabinet;
namespace {
constexpr size_t nt = 0x80;
constexpr size_t optional = nt + 24;
constexpr size_t import_directory = 0x900;
constexpr size_t lookup_table = 0xb00;
constexpr size_t iat_table = 0x1000;
constexpr uint64_t ordinal_flag = uint64_t{1} << 63;
const std::array<ImportTarget, 3> targets{{
    {"destroy", 17, 0x180001000},
    {"open", 29, 0x180002000},
    {"mount", 43, 0x180003000}
}};

struct Fixture {
    std::vector<std::byte> bytes = std::vector<std::byte>(0x6000);
    Fixture() {
        u16(0, 0x5a4d);
        u32(0x3c, nt);
        u32(nt, 0x00004550);
        u16(nt + 4, 0x8664);
        u32(nt + 8, 0x12345678);
        u16(nt + 20, 240);
        u16(optional, 0x20b);
        u32(optional + 16, 0x2340);
        u32(optional + 56, static_cast<uint32_t>(bytes.size()));
        u32(optional + 60, 0x200);
        u32(optional + 108, 16);
    }
    void u16(size_t at, uint16_t value) {
        assert(at + 2 <= bytes.size());
        bytes[at] = std::byte(value & 255);
        bytes[at + 1] = std::byte(value >> 8);
    }
    void u32(size_t at, uint32_t value) {
        u16(at, static_cast<uint16_t>(value));
        u16(at + 2, static_cast<uint16_t>(value >> 16));
    }
    void u64(size_t at, uint64_t value) {
        u32(at, static_cast<uint32_t>(value));
        u32(at + 4, static_cast<uint32_t>(value >> 32));
    }
    void text(size_t at, std::string_view value) {
        assert(at + value.size() < bytes.size());
        for (size_t i = 0; i < value.size(); ++i) bytes[at + i] = std::byte(value[i]);
        bytes[at + value.size()] = std::byte{0};
    }
    void directory(size_t index, uint32_t rva, uint32_t size) {
        u32(optional + 112 + index * 8, rva);
        u32(optional + 116 + index * 8, size);
    }
    void imports(std::initializer_list<size_t> apis, bool named = true, bool oft = true) {
        directory(1, import_directory, 40);
        u32(import_directory, oft ? lookup_table : 0);
        u32(import_directory + 12, 0xa00);
        u32(import_directory + 16, iat_table);
        text(0xa00, "AvS2-CoRe.DlL");
        size_t index = 0;
        for (size_t api : apis) {
            const size_t name = 0x1800 + index * 32;
            u16(name, 0);
            text(name + 2, targets[api].name);
            if (oft) u64(lookup_table + index * 8,
                         named ? name : ordinal_flag | targets[api].ordinal);
            u64(iat_table + index * 8, targets[api].direct_address);
            ++index;
        }
    }
    void exports() {
        directory(0, 0x400, 0x100);
        u32(0x40c, 0x780);
        text(0x780, "avs2-core.dll");
        u32(0x410, 11); // Actual ordinal = base + table index, not name index.
        u32(0x414, 5);
        u32(0x418, 3);
        u32(0x41c, 0x500);
        u32(0x420, 0x580);
        u32(0x424, 0x600);
        for (uint32_t i = 0; i < 5; ++i) u32(0x500 + i * 4, 0x2000 + i * 16);
        text(0x700, "destroy");
        text(0x720, "mount");
        text(0x740, "open");
        u32(0x580, 0x700); u16(0x600, 4);
        u32(0x584, 0x720); u16(0x602, 0);
        u32(0x588, 0x740); u16(0x604, 2);
    }
};

bool imports(const Fixture &fixture, std::vector<ImportSlot> &slots, std::string &error) {
    return pe_import_slots(fixture.bytes, "avs2-core.dll", targets, slots, error);
}
void rejects(const Fixture &fixture) {
    std::vector<ImportSlot> slots{{AvsApi::Mount, 8, 123}};
    std::string error;
    assert(!imports(fixture, slots, error));
    assert(slots.empty());
    assert(!error.empty());
}
void rejects_export(const Fixture &fixture) {
    uint16_t ordinal = 0;
    std::string error;
    assert(!pe_export_ordinal(fixture.bytes, "destroy", ordinal, error));
    assert(!error.empty());
}

void identity_tests() {
    Fixture fixture;
    PeIdentity identity;
    std::string error;
    assert(pe_identity(fixture.bytes, identity, error));
    assert(error.empty());
    assert(identity.machine == 0x8664 && identity.timestamp == 0x12345678);
    assert(identity.entry_point == 0x2340 && identity.image_size == fixture.bytes.size());
    for (size_t size : {size_t{0}, size_t{63}, size_t{0x90}, size_t{0x100}, size_t{0x5fff}}) {
        assert(!pe_identity(std::span<const std::byte>(fixture.bytes).first(size), identity, error));
        assert(!error.empty());
    }
    auto bad = fixture; bad.u16(0, 0); rejects(bad);
    bad = fixture; bad.u32(0x3c, 0xfffffff0); rejects(bad);
    bad = fixture; bad.u32(0x3c, 0); rejects(bad);
    bad = fixture; bad.u32(nt, 0); rejects(bad);
    bad = fixture; bad.u16(nt + 4, 0x14c); rejects(bad);
    bad = fixture; bad.u16(optional, 0x10b); rejects(bad);
    bad = fixture; bad.u16(nt + 20, 111); rejects(bad);
    bad = fixture; bad.u16(nt + 20, 0xffff); rejects(bad);
    bad = fixture; bad.u32(optional + 56, 0); rejects(bad);
    bad = fixture; bad.u32(optional + 56, 0x100); rejects(bad);
    bad = fixture; bad.u32(optional + 56, 0x6001); rejects(bad);
    bad = fixture; bad.u32(optional + 16, 0x6000); rejects(bad);
    bad = fixture; bad.u32(optional + 60, 0x100); rejects(bad);
    bad = fixture; bad.u32(optional + 108, 17); rejects(bad);
    bad = fixture; bad.u16(nt + 6, 0xffff); rejects(bad);
    // The supplied span can include extra readable bytes; RVAs still stop at SizeOfImage.
    fixture.bytes.resize(0x7000, std::byte{0});
    assert(pe_identity(fixture.bytes, identity, error));
    assert(identity.image_size == 0x6000);
    fixture.directory(1, 0x6500, 40);
    rejects(fixture);
}

void import_tests() {
    std::vector<ImportSlot> slots;
    std::string error;
    Fixture fixture;
    assert(imports(fixture, slots, error) && slots.empty());
    fixture.imports({0, 1, 2, 0});
    const uint64_t wrapped = 0x7ffa12345678;
    fixture.u64(iat_table, wrapped);
    assert(imports(fixture, slots, error));
    assert(error.empty() && slots.size() == 4);
    assert(slots[0].api == AvsApi::Destroy && slots[0].rva == iat_table);
    assert(slots[0].predecessor == wrapped);
    assert(slots[1].api == AvsApi::Open && slots[1].predecessor == targets[1].direct_address);
    assert(slots[2].api == AvsApi::Mount && slots[2].rva == iat_table + 16);
    assert(slots[3].api == AvsApi::Destroy && slots[3].rva == iat_table + 24);
    fixture = Fixture{}; fixture.imports({2, 0, 1}, false);
    fixture.u64(iat_table + 8, wrapped);
    assert(imports(fixture, slots, error) && slots.size() == 3);
    assert(slots[0].api == AvsApi::Mount && slots[1].api == AvsApi::Destroy);
    assert(slots[1].predecessor == wrapped);
    fixture = Fixture{}; fixture.imports({0, 1, 2}, true, false);
    assert(imports(fixture, slots, error) && slots.size() == 3);
    fixture.u64(iat_table + 8, wrapped); rejects(fixture);
    fixture = Fixture{}; fixture.imports({0}, true, false);
    fixture.u64(iat_table, 0x123); rejects(fixture);
    fixture = Fixture{}; fixture.imports({0});
    fixture.text(0x1802, "unrelated");
    assert(imports(fixture, slots, error) && slots.empty());
    fixture = Fixture{}; fixture.imports({0}, false);
    fixture.u64(lookup_table, ordinal_flag | 1234);
    assert(imports(fixture, slots, error) && slots.empty());
    fixture = Fixture{}; fixture.imports({0});
    fixture.text(0xa00, "another.dll");
    assert(imports(fixture, slots, error) && slots.empty());
    // Distinct import descriptors contribute slots and keep their own predecessors.
    fixture = Fixture{}; fixture.imports({0});
    fixture.directory(1, import_directory, 60);
    fixture.u32(import_directory + 20, 0xc00);
    fixture.u32(import_directory + 32, 0xa00);
    fixture.u32(import_directory + 36, 0x1200);
    fixture.u64(0xc00, ordinal_flag | targets[0].ordinal);
    fixture.u64(0x1200, wrapped);
    assert(imports(fixture, slots, error) && slots.size() == 2);
    assert(slots[0].predecessor != slots[1].predecessor);
    assert(slots[1].rva == 0x1200);
    auto ambiguous = targets;
    ambiguous[1].name = targets[0].name;
    assert(!pe_import_slots(fixture.bytes, "avs2-core.dll", ambiguous, slots, error));
    assert(slots.empty());
}

void import_corruption_tests() {
    Fixture fixture; fixture.imports({0});
    auto bad = fixture; bad.directory(1, 0x5ff0, 40); rejects(bad);
    bad = fixture; bad.directory(1, 0, 40); rejects(bad);
    bad = fixture; bad.directory(1, import_directory, 0); rejects(bad);
    bad = fixture; bad.directory(1, import_directory, 19); rejects(bad);
    bad = fixture; bad.directory(1, import_directory, 20); rejects(bad);
    bad = fixture; bad.u32(import_directory + 12, 0x6000); rejects(bad);
    bad = fixture; bad.u32(import_directory + 12, 0x5fff);
    bad.bytes.back() = std::byte{'x'}; rejects(bad);
    bad = fixture; bad.u32(import_directory + 12, 0); rejects(bad);
    bad = fixture; bad.u32(import_directory + 16, iat_table + 1); rejects(bad);
    bad = fixture; bad.u32(import_directory + 16, 0x6000); rejects(bad);
    bad = fixture; bad.u32(import_directory + 16, 0); rejects(bad);
    bad = fixture; bad.u32(import_directory, lookup_table + 1); rejects(bad);
    bad = fixture; bad.u32(import_directory, 0x6000); rejects(bad);
    bad = fixture; bad.u64(lookup_table, 0x100000000); rejects(bad);
    bad = fixture; bad.u64(lookup_table, 0x6000); rejects(bad);
    bad = fixture; bad.u64(lookup_table, 0x5ffe); rejects(bad);
    bad = fixture; bad.u64(lookup_table, 0x5ffd);
    bad.bytes.back() = std::byte{'x'}; rejects(bad);
    bad = fixture; bad.text(0x1802, ""); rejects(bad);
    bad = fixture; bad.u64(lookup_table, ordinal_flag | 0x10000 | targets[0].ordinal); rejects(bad);
    bad = fixture; bad.u64(iat_table, 0); rejects(bad);
    bad = fixture; bad.u64(iat_table + 8, targets[1].direct_address); rejects(bad);
    // A valid slot must never escape when a later descriptor fails validation.
    bad = fixture; bad.directory(1, import_directory, 60);
    bad.u32(import_directory + 32, 0x6000); rejects(bad);
    // INT runs to the last image byte with no terminator.
    bad = fixture; bad.u32(import_directory, 0x5ff8);
    bad.u64(0x5ff8, ordinal_flag | targets[0].ordinal); rejects(bad);
    // IAT ends before an otherwise valid INT terminator.
    bad = fixture; bad.u32(import_directory + 16, 0x5ff8);
    bad.u64(0x5ff8, targets[0].direct_address); rejects(bad);
    // Missing OFT also needs an in-bounds zero terminator.
    bad = fixture; bad.u32(import_directory, 0);
    bad.u32(import_directory + 16, 0x5ff8);
    bad.u64(0x5ff8, targets[0].direct_address); rejects(bad);
}

void capacity_tests() {
    Fixture fixture; fixture.imports({});
    std::vector<ImportSlot> slots;
    std::string error;
    for (size_t i = 0; i < 64; ++i) {
        fixture.u64(lookup_table + i * 8, ordinal_flag | targets[0].ordinal);
        fixture.u64(iat_table + i * 8, targets[0].direct_address);
    }
    assert(imports(fixture, slots, error) && slots.size() == 64);
    fixture.u64(lookup_table + 64 * 8, ordinal_flag | targets[0].ordinal);
    fixture.u64(iat_table + 64 * 8, targets[0].direct_address);
    rejects(fixture);
    // Capacity is per API: more than 64 total slots is permitted.
    fixture = Fixture{}; fixture.imports({});
    for (size_t i = 0; i < 192; ++i) {
        const size_t api = i % 3;
        fixture.u64(lookup_table + i * 8, ordinal_flag | targets[api].ordinal);
        fixture.u64(0x3000 + i * 8, targets[api].direct_address);
    }
    fixture.u32(import_directory + 16, 0x3000);
    assert(imports(fixture, slots, error) && slots.size() == 192);
}

void export_tests() {
    Fixture fixture; fixture.exports();
    uint16_t ordinal{};
    std::string error;
    assert(pe_export_ordinal(fixture.bytes, "destroy", ordinal, error) && ordinal == 15);
    assert(pe_export_ordinal(fixture.bytes, "mount", ordinal, error) && ordinal == 11);
    assert(pe_export_ordinal(fixture.bytes, "open", ordinal, error) && ordinal == 13);
    assert(error.empty());
    assert(!pe_export_ordinal(fixture.bytes, "Destroy", ordinal, error));
    assert(!pe_export_ordinal(fixture.bytes, "missing", ordinal, error));
    auto bad = fixture; bad.directory(0, 0x5ff0, 40); rejects_export(bad);
    bad = fixture; bad.directory(0, 0x400, 39); rejects_export(bad);
    bad = fixture; bad.u32(0x41c, 0x5ff0); rejects_export(bad);
    bad = fixture; bad.u32(0x420, 0x5ff8); rejects_export(bad);
    bad = fixture; bad.u32(0x424, 0x5ffc); rejects_export(bad);
    bad = fixture; bad.u32(0x41c, 0); rejects_export(bad);
    bad = fixture; bad.u32(0x414, std::numeric_limits<uint32_t>::max()); rejects_export(bad);
    bad = fixture; bad.u32(0x418, std::numeric_limits<uint32_t>::max()); rejects_export(bad);
    bad = fixture; bad.u16(0x600, 5); rejects_export(bad);
    bad = fixture; bad.u32(0x580, 0x6000); rejects_export(bad);
    bad = fixture; bad.u32(0x580, 0x5fff); bad.bytes.back() = std::byte{'x'}; rejects_export(bad);
    bad = fixture; bad.text(0x700, ""); rejects_export(bad);
    bad = fixture; bad.u32(0x40c, 0x6000); rejects_export(bad);
    bad = fixture; bad.u32(0x40c, 0x5fff); bad.bytes.back() = std::byte{'x'}; rejects_export(bad);
    bad = fixture; bad.u32(0x510, 0); rejects_export(bad);
    bad = fixture; bad.u32(0x504, 0x6000); rejects_export(bad);
    bad = fixture; bad.u32(0x410, 65532); rejects_export(bad);
    // Every named ordinal is checked, including a malformed unrelated export.
    bad = fixture; bad.u16(0x604, 5); rejects_export(bad);
    // A duplicate target name has no unique ordinal.
    bad = fixture; bad.u32(0x588, 0x700); rejects_export(bad);
    fixture.u32(0x510, 0x480); fixture.text(0x480, "other.function");
    assert(pe_export_ordinal(fixture.bytes, "destroy", ordinal, error) && ordinal == 15);
    bad = fixture; bad.u32(0x510, 0x4ff); bad.bytes[0x4ff] = std::byte{'x'}; rejects_export(bad);
}
} // namespace

int main() {
    identity_tests();
    import_tests();
    import_corruption_tests();
    capacity_tests();
    export_tests();
    std::puts("PE64 identity, export ordinals and bounded import identity tests passed");
}
