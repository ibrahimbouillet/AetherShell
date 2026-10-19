// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Scan of synthetic pin-list blobs. Never reads the user's taskbar pin list.
#include "AetherTest.h"

#include "applications/TaskbandScan.h"
#include "core/Strings.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

constexpr wchar_t kPackaged[] = L"Aether.Fixture_123456789abcd!App";

// A synthetic blob built piece by piece; Utf16 returns the byte offset where the text starts.
struct Blob {
    std::vector<std::uint8_t> bytes;

    void Raw(std::initializer_list<std::uint8_t> values) { bytes.insert(bytes.end(), values); }
    void Filler(std::size_t count, std::uint8_t value = 0xA5) { bytes.insert(bytes.end(), count, value); }
    std::size_t Utf16(std::wstring_view text, bool terminate = true) {
        const std::size_t offset = bytes.size();
        for (const wchar_t c : text) {
            bytes.push_back(static_cast<std::uint8_t>(c & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
        }
        if (terminate) Raw({0, 0});
        return offset;
    }
};

std::function<bool(std::wstring_view)> Known(std::set<std::wstring> aumids) {
    return [aumids = std::move(aumids)](std::wstring_view candidate) {
        return aumids.contains(text::ToLower(candidate));
    };
}

const std::function<bool(std::wstring_view)> kAcceptAny = [](std::wstring_view) { return true; };

}  // namespace

AETHER_TEST("taskband scan: shortcut names are found at even and odd offsets, ignoring case") {
    Blob blob;
    blob.Raw({0x14, 0x00, 0x1F});  // odd: the next name starts at byte 3
    const auto gamma = blob.Utf16(L"GAMMA.LNK");
    blob.Filler(7);
    const auto alpha = blob.Utf16(L"alpha.lnk");
    CHECK_EQ(gamma % 2, std::size_t{1});

    const std::vector<std::wstring> names{L"Alpha.lnk", L"Beta.lnk", L"Gamma.lnk"};
    const auto scan = ScanTaskband(blob.bytes, names, nullptr);
    REQUIRE(scan.ok());
    REQUIRE_EQ(scan.value().shortcutOffsets.size(), std::size_t{3});
    REQUIRE(scan.value().shortcutOffsets[0].has_value());
    CHECK_EQ(*scan.value().shortcutOffsets[0], alpha);
    CHECK_FALSE(scan.value().shortcutOffsets[1].has_value());  // not pinned in the list: file-name order later
    REQUIRE(scan.value().shortcutOffsets[2].has_value());
    CHECK_EQ(*scan.value().shortcutOffsets[2], gamma);
    CHECK(*scan.value().shortcutOffsets[2] < *scan.value().shortcutOffsets[0]);  // Windows order
    CHECK_EQ(scan.value().MatchedShortcuts(), std::size_t{2});
    CHECK(scan.value().packaged.empty());
}

AETHER_TEST("taskband scan: duplicates keep their first offset and truncated names do not match") {
    Blob blob;
    const auto first = blob.Utf16(L"Delta.lnk");
    blob.Filler(5);
    blob.Utf16(L"Delta.lnk");
    blob.Filler(2);
    blob.Utf16(L"Epsilon.ln", false);  // cut off at the end of the blob
    const auto scan = ScanTaskband(blob.bytes, {L"Delta.lnk", L"Epsilon.lnk"}, nullptr);
    REQUIRE(scan.ok());
    REQUIRE(scan.value().shortcutOffsets[0].has_value());
    CHECK_EQ(*scan.value().shortcutOffsets[0], first);
    CHECK_FALSE(scan.value().shortcutOffsets[1].has_value());
}

AETHER_TEST("taskband scan: a short name inside a longer pinned name is not a match") {
    Blob blob;
    blob.Filler(3);
    const auto data = blob.Utf16(L"Data.lnk");
    const auto scan = ScanTaskband(blob.bytes, {L"a.lnk", L"Data.lnk"}, nullptr);
    REQUIRE(scan.ok());
    CHECK_FALSE(scan.value().shortcutOffsets[0].has_value());
    REQUIRE(scan.value().shortcutOffsets[1].has_value());
    CHECK_EQ(*scan.value().shortcutOffsets[1], data);

    // Once the short name occurs on its own, it matches there.
    const auto own = blob.Utf16(L"A.LNK");
    const auto again = ScanTaskband(blob.bytes, {L"a.lnk", L"Data.lnk"}, nullptr);
    REQUIRE(again.ok());
    REQUIRE(again.value().shortcutOffsets[0].has_value());
    CHECK_EQ(*again.value().shortcutOffsets[0], own);
}

AETHER_TEST("taskband scan: empty, oversized and garbage blobs") {
    const std::vector<std::wstring> names{L"Alpha.lnk", L"Beta.lnk"};
    const auto empty = ScanTaskband({}, names, kAcceptAny);
    REQUIRE(empty.ok());
    CHECK_EQ(empty.value().MatchedShortcuts(), std::size_t{0});
    CHECK(empty.value().packaged.empty());
    CHECK_EQ(empty.value().shortcutOffsets.size(), std::size_t{2});

    std::vector<std::uint8_t> large(65, 0x41);
    const auto oversized = ScanTaskband(large, names, kAcceptAny, 64);
    REQUIRE_FALSE(oversized.ok());
    CHECK_EQ(oversized.error().code, ErrorCode::Unsupported);
    CHECK(ScanTaskband(large, names, kAcceptAny, 65).ok());

    // Deterministic pseudo-random bytes: no order and no packaged pin is invented.
    std::vector<std::uint8_t> garbage(64 * 1024);
    std::uint32_t state = 0x2545F491u;
    for (auto& byte : garbage) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24);
    }
    const auto noise = ScanTaskband(garbage, names, Known({text::ToLower(kPackaged)}));
    REQUIRE(noise.ok());
    CHECK_EQ(noise.value().MatchedShortcuts(), std::size_t{0});
    CHECK(noise.value().packaged.empty());
}

AETHER_TEST("taskband scan: packaged AUMIDs are returned only when the caller knows them") {
    Blob blob;
    blob.Filler(9);
    const auto known = blob.Utf16(kPackaged);
    blob.Filler(4);
    blob.Utf16(L"Aether.Unknown_123456789abcd!App");  // valid syntax, not installed
    blob.Filler(1);
    const auto shortcut = blob.Utf16(L"Pinned.lnk");
    const auto scan = ScanTaskband(blob.bytes, {L"Pinned.lnk"}, Known({text::ToLower(kPackaged)}));
    REQUIRE(scan.ok());
    REQUIRE_EQ(scan.value().packaged.size(), std::size_t{1});
    CHECK_EQ(scan.value().packaged[0].first, known);
    CHECK_EQ(scan.value().packaged[0].second, std::wstring(kPackaged));
    REQUIRE(scan.value().shortcutOffsets[0].has_value());
    CHECK_EQ(*scan.value().shortcutOffsets[0], shortcut);
    CHECK(scan.value().packaged[0].first < shortcut);

    // A null predicate confirms nothing.
    const auto none = ScanTaskband(blob.bytes, {L"Pinned.lnk"}, nullptr);
    REQUIRE(none.ok());
    CHECK(none.value().packaged.empty());
}

AETHER_TEST("taskband scan: malformed packaged identities are rejected even when known") {
    const std::wstring wrongPublisher = L"Aether.Fixture_123456789abc!App";  // 12-character publisher id
    const std::wstring noUnderscore = L"Aether.Fixture123456789abcd!App";
    const std::wstring longAppId = L"Aether.Fixture_123456789abcd!" + std::wstring(65, L'A');
    const std::wstring shortName = L"Ae_123456789abcd!App";
    Blob blob;
    for (const auto& value : {wrongPublisher, noUnderscore, longAppId, shortName}) {
        blob.Filler(3, 0x00);
        blob.Utf16(value);
    }
    const auto scan = ScanTaskband(blob.bytes, {}, Known({text::ToLower(wrongPublisher), text::ToLower(noUnderscore),
                                                          text::ToLower(longAppId), text::ToLower(shortName)}));
    REQUIRE(scan.ok());
    CHECK(scan.value().packaged.empty());
}

AETHER_TEST("taskband scan: packaged AUMIDs next to alphanumeric bytes are found at bounded starts") {
    Blob blob;
    blob.Raw({0x07});  // odd parity
    const auto start = blob.Utf16(L"xyz9", false);
    blob.Utf16(kPackaged, false);
    blob.Utf16(L"42Q", false);
    blob.Raw({0x00, 0x00});
    // The same AUMID again, differently cased: reported once, at its first position.
    blob.Filler(6);
    blob.Utf16(L"AETHER.FIXTURE_123456789ABCD!APP");
    const auto scan = ScanTaskband(blob.bytes, {}, Known({text::ToLower(kPackaged)}));
    REQUIRE(scan.ok());
    REQUIRE_EQ(scan.value().packaged.size(), std::size_t{1});
    CHECK_EQ(scan.value().packaged[0].first, start + 8);  // after "xyz9" (4 code units)
    CHECK_EQ(scan.value().packaged[0].second, std::wstring(kPackaged));
    CHECK_EQ(scan.value().packaged[0].first % 2, std::size_t{1});

    // The longest confirmed AppId wins.
    Blob longer;
    longer.Utf16(L"Aether.Fixture_123456789abcd!App2");
    const auto both = ScanTaskband(longer.bytes, {},
                                   Known({text::ToLower(kPackaged), L"aether.fixture_123456789abcd!app2"}));
    REQUIRE(both.ok());
    REQUIRE_EQ(both.value().packaged.size(), std::size_t{1});
    CHECK_EQ(both.value().packaged[0].second, std::wstring(L"Aether.Fixture_123456789abcd!App2"));
}

AETHER_TEST("taskband scan: packaged pins are ordered by position") {
    Blob blob;
    const auto second = blob.Utf16(L"Aether.Second_123456789abcd!App");
    blob.Filler(2);
    const auto first = blob.Utf16(L"Aether.First_123456789abcd!App");
    const auto scan = ScanTaskband(blob.bytes, {}, kAcceptAny);
    REQUIRE(scan.ok());
    REQUIRE_EQ(scan.value().packaged.size(), std::size_t{2});
    CHECK_EQ(scan.value().packaged[0].first, second);
    CHECK_EQ(scan.value().packaged[1].first, first);
}
