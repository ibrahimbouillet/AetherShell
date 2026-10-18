// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "status/KeyboardLayouts.h"
#include "status/SnapshotUtilities.h"

#include <cstdint>

using namespace aether;
using namespace aether::status;

AETHER_TEST("keyboard: input language ids map to locale, label and display name") {
    auto french = DescribeInputLanguage(0x040C);
    REQUIRE(french.ok());
    CHECK_EQ(french.value().localeName, std::wstring(L"fr-FR"));
    CHECK_EQ(french.value().shortLabel, std::wstring(L"FR"));
    CHECK_FALSE(french.value().languageName.empty());  // localized in the Windows display language

    auto english = DescribeInputLanguage(0x0409);
    REQUIRE(english.ok());
    CHECK_EQ(english.value().localeName, std::wstring(L"en-US"));
    CHECK_EQ(english.value().shortLabel, std::wstring(L"EN"));

    auto german = DescribeInputLanguage(0x0407);
    REQUIRE(german.ok());
    CHECK_EQ(german.value().localeName, std::wstring(L"de-DE"));
    CHECK_EQ(german.value().shortLabel, std::wstring(L"DE"));

    auto british = DescribeInputLanguage(0x0809);
    REQUIRE(british.ok());
    CHECK_EQ(british.value().localeName, std::wstring(L"en-GB"));
    CHECK_EQ(british.value().shortLabel, std::wstring(L"EN"));

    CHECK_FALSE(DescribeInputLanguage(0).ok());
}

AETHER_TEST("keyboard: short labels use ISO 639-1, else the three-letter code") {
    CHECK_EQ(ShortLanguageLabel(L"fr-FR"), std::wstring(L"FR"));
    CHECK_EQ(ShortLanguageLabel(L"zh-CN"), std::wstring(L"ZH"));
    CHECK_EQ(ShortLanguageLabel(L"sr-Latn-RS"), std::wstring(L"SR"));
    CHECK_EQ(ShortLanguageLabel(L"haw-US"), std::wstring(L"HAW"));  // Hawaiian has no two-letter code
    CHECK(ShortLanguageLabel(L"").empty());
}

AETHER_TEST("keyboard: KLID derivation from HKL values") {
    int resolverCalls = 0;
    const LayoutIdResolver resolver = [&](std::uint16_t layoutId) -> std::optional<std::wstring> {
        ++resolverCalls;
        if (layoutId == 0x0002) return L"00010409";  // United States-Dvorak
        if (layoutId == 0x0003) return L"not-a-klid";
        return std::nullopt;
    };

    // Primary layouts: the device handle is the layout's language.
    CHECK_EQ(KeyboardLayoutIdFromHkl(0x04090409, resolver), std::optional<std::wstring>(L"00000409"));
    CHECK_EQ(KeyboardLayoutIdFromHkl(0x040C040C, resolver), std::optional<std::wstring>(L"0000040C"));
    // English input language with a French layout.
    CHECK_EQ(KeyboardLayoutIdFromHkl(0x040C0409, resolver), std::optional<std::wstring>(L"0000040C"));
    CHECK_EQ(resolverCalls, 0);

    // Layout variants: 0xFxxx, resolved through the Layout Id; x64 handles may be sign-extended.
    CHECK_EQ(KeyboardLayoutIdFromHkl(0xF0020409u, resolver), std::optional<std::wstring>(L"00010409"));
    CHECK_EQ(KeyboardLayoutIdFromHkl(static_cast<std::uintptr_t>(static_cast<std::intptr_t>(
                                         static_cast<std::int32_t>(0xF0020409u))),
                                     resolver),
             std::optional<std::wstring>(L"00010409"));
    CHECK_FALSE(KeyboardLayoutIdFromHkl(0xF0990409u, resolver).has_value());  // unknown Layout Id
    CHECK_FALSE(KeyboardLayoutIdFromHkl(0xF0030409u, resolver).has_value());  // malformed resolver answer
    CHECK_FALSE(KeyboardLayoutIdFromHkl(0xF0020409u, LayoutIdResolver{}).has_value());

    // Legacy IME: the KLID is the whole HKL.
    CHECK_EQ(KeyboardLayoutIdFromHkl(0xE0010411u, resolver), std::optional<std::wstring>(L"E0010411"));
    // No device handle: nothing to derive.
    CHECK_FALSE(KeyboardLayoutIdFromHkl(0x00000409u, resolver).has_value());
}

AETHER_TEST("keyboard: registry lookups for well-known layouts") {
    // Stable registrations shipped with every Windows installation.
    CHECK_EQ(FindKeyboardLayoutIdByLayoutId(0x0002), std::optional<std::wstring>(L"00010409"));  // US-Dvorak
    CHECK_EQ(FindKeyboardLayoutIdByLayoutId(0x0001), std::optional<std::wstring>(L"00020409"));  // US-International
    CHECK_FALSE(FindKeyboardLayoutIdByLayoutId(0x0FFF).has_value());

    auto us = KeyboardLayoutDisplayName(L"00000409");
    REQUIRE(us.has_value());
    CHECK_FALSE(us->empty());
    TEST_NOTE("00000409 -> " + test::NarrowForDisplay(*us));
    auto lowercase = KeyboardLayoutDisplayName(L"0000040c");
    CHECK(lowercase.has_value());

    CHECK_FALSE(KeyboardLayoutDisplayName(L"FFFFFFFF").has_value());   // not registered
    CHECK_FALSE(KeyboardLayoutDisplayName(L"..\\..\\x").has_value());  // not a KLID: never used as a path
    CHECK_FALSE(KeyboardLayoutDisplayName(L"").has_value());
}

AETHER_TEST("keyboard: full description of an HKL") {
    const KeyboardSnapshot french = DescribeKeyboardLayout(0x040C040C);
    CHECK_EQ(french.state, ProviderState::Available);
    CHECK_EQ(french.localeName, std::wstring(L"fr-FR"));
    CHECK_EQ(french.shortLabel, std::wstring(L"FR"));
    CHECK_FALSE(french.languageName.empty());
    CHECK_FALSE(french.layoutName.empty());
    CHECK_FALSE(french.fromForegroundWindow);
    TEST_NOTE(Describe(french));

    // English input with the Dvorak variant (sign-extended handle as returned on x64).
    const auto dvorak = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(static_cast<std::int32_t>(0xF0020409u)));
    const KeyboardSnapshot variant = DescribeKeyboardLayout(dvorak);
    CHECK_EQ(variant.state, ProviderState::Available);
    CHECK_EQ(variant.shortLabel, std::wstring(L"EN"));
    CHECK_FALSE(variant.layoutName.empty());
    TEST_NOTE(Describe(variant));

    const KeyboardSnapshot invalid = DescribeKeyboardLayout(0);
    CHECK_EQ(invalid.state, ProviderState::Error);
    CHECK_FALSE(invalid.detail.empty());
    CHECK(invalid.shortLabel.empty());
}
