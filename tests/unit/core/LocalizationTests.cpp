// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/Localization.h"

#include <windows.h>

#include <format>
#include <regex>
#include <set>
#include <string>

using namespace aether;
using i18n::Language;

namespace {
// Restores English after each test: the language is process-wide.
struct EnglishAfter {
    ~EnglishAfter() { i18n::SetLanguage(Language::English); }
};
}  // namespace

AETHER_TEST("localization: the five languages, each named in its own language") {
    const auto languages = i18n::Languages();
    REQUIRE_EQ(languages.size(), i18n::kLanguageCount);
    std::set<std::string_view> codes;
    for (const auto& info : languages) {
        CHECK(!info.nativeName.empty());
        CHECK(codes.insert(info.code).second);
    }
    CHECK(i18n::Info(Language::French).nativeName == std::wstring_view(L"Français"));
    CHECK(i18n::Info(Language::Chinese).code == std::string_view("zh-Hans"));
}

AETHER_TEST("localization: Windows' display language chooses one of the five, English otherwise") {
    CHECK(i18n::FromLangId(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH_CANADIAN)) == Language::French);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MEXICAN)) == Language::Spanish);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) == Language::Chinese);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_HINDI, SUBLANG_DEFAULT)) == Language::Hindi);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK)) == Language::English);
    CHECK_FALSE(i18n::FromLangId(MAKELANGID(LANG_SWEDISH, SUBLANG_DEFAULT)).has_value());
}

AETHER_TEST("localization: a known text is translated, an unknown one shows as it is") {
    EnglishAfter restore;
    CHECK(i18n::Tr(L"Settings") == std::wstring_view(L"Settings"));
    i18n::SetLanguage(Language::French);
    CHECK(i18n::Tr(L"Settings") == std::wstring_view(L"Paramètres"));
    CHECK(i18n::Tr(L"Google Chrome") == std::wstring_view(L"Google Chrome"));   // a name, never translated
    CHECK(i18n::Tr(L"") == std::wstring_view(L""));
    CHECK(i18n::Tr(L"Settings", Language::Spanish) == std::wstring_view(L"Configuración"));
    const auto all = i18n::AllTranslations(L"Settings");
    CHECK(all[0] == std::wstring_view(L"Settings"));
    CHECK(all[static_cast<std::size_t>(Language::Chinese)] == std::wstring_view(L"设置"));
}

AETHER_TEST("localization: a change of language is counted, the same one is not") {
    EnglishAfter restore;
    i18n::SetLanguage(Language::English);
    const auto before = i18n::Revision();
    i18n::SetLanguage(Language::English);
    CHECK_EQ(i18n::Revision(), before);
    i18n::SetLanguage(Language::Hindi);
    CHECK(i18n::Revision() != before);
    CHECK(i18n::CurrentLanguage() == Language::Hindi);
}

AETHER_TEST("localization: dates follow a chosen language, and the person's own format otherwise") {
    EnglishAfter restore;
    i18n::SetLanguage(Language::French, false);
    CHECK(i18n::FormattingLocale().empty());
    i18n::SetLanguage(Language::French, true);
    CHECK(i18n::FormattingLocale() == std::wstring_view(L"fr-FR"));
}

AETHER_TEST("localization: patterns are filled in each language") {
    EnglishAfter restore;
    CHECK_EQ(i18n::TrFormat(L"Version {} is available", std::wstring(L"4.0")), std::wstring(L"Version 4.0 is available"));
    i18n::SetLanguage(Language::French);
    CHECK_EQ(i18n::TrFormat(L"Page {} of {}", 1, 2), std::wstring(L"Page 1 sur 2"));
    // A pattern that is not in the catalog still formats.
    CHECK_EQ(i18n::TrFormat(L"{} + {}", 1, 2), std::wstring(L"1 + 2"));
}

AETHER_TEST("localization: every text of the catalog is in all five languages with the same placeholders") {
    // The catalog is checked when it is generated (tools/i18n/make_catalog.py); this guards the built table itself.
    REQUIRE(i18n::CatalogSize() > 500);
    const std::wregex placeholder(L"\\{[^{}]*\\}");
    const auto placeholders = [&](std::wstring_view text) {
        std::multiset<std::wstring> found;
        const std::wstring value(text);
        for (std::wsregex_iterator it(value.begin(), value.end(), placeholder), end; it != end; ++it) found.insert(it->str());
        return found;
    };
    const wchar_t* samples[] = {L"Received {} of {} ({}%).", L"Step {} of {}: {}", L"Battery {} percent, {}", L"Estimated remaining: {} h {} min"};
    for (const auto* english : samples) {
        REQUIRE(i18n::Known(english));
        const auto all = i18n::AllTranslations(english);
        for (const auto text : all) {
            CHECK(!text.empty());
            CHECK(placeholders(text) == placeholders(english));
        }
    }
}
