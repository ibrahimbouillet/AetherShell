// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/Localization.h"

#include <windows.h>

#include <filesystem>
#include <format>
#include <fstream>
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

AETHER_TEST("localization: the ten languages, each named in its own language") {
    const auto languages = i18n::Languages();
    REQUIRE_EQ(languages.size(), i18n::kLanguageCount);
    CHECK_EQ(i18n::kLanguageCount, std::size_t{10});
    std::set<std::string_view> codes, savedNames;
    for (const auto& info : languages) {
        CHECK(!info.nativeName.empty());
        CHECK(codes.insert(info.code).second);
        CHECK(savedNames.insert(info.savedName).second);
    }
    CHECK(i18n::Info(Language::French).nativeName == std::wstring_view(L"Français"));
    CHECK(i18n::Info(Language::Chinese).code == std::string_view("zh-Hans"));
    CHECK(i18n::Info(Language::Portuguese).nativeName == std::wstring_view(L"Português"));
    CHECK(i18n::Info(Language::Russian).nativeName == std::wstring_view(L"Русский"));
    CHECK(i18n::Info(Language::Arabic).code == std::string_view("ar"));
    CHECK(i18n::Info(Language::Bengali).code == std::string_view("bn"));
    CHECK(i18n::Info(Language::Urdu).code == std::string_view("ur"));
}

AETHER_TEST("localization: Arabic and Urdu read right to left, the others left to right") {
    for (const auto& info : i18n::Languages())
        CHECK_EQ(i18n::IsRightToLeft(info.language), info.language == Language::Arabic || info.language == Language::Urdu);
}

AETHER_TEST("localization: Windows' display language chooses one of the ten, English otherwise") {
    CHECK(i18n::FromLangId(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH_CANADIAN)) == Language::French);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MEXICAN)) == Language::Spanish);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) == Language::Chinese);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_HINDI, SUBLANG_DEFAULT)) == Language::Hindi);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK)) == Language::English);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_PORTUGUESE, SUBLANG_PORTUGUESE_BRAZILIAN)) == Language::Portuguese);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT)) == Language::Russian);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_ARABIC, SUBLANG_ARABIC_EGYPT)) == Language::Arabic);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_BENGALI, SUBLANG_DEFAULT)) == Language::Bengali);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_URDU, SUBLANG_DEFAULT)) == Language::Urdu);
    CHECK_FALSE(i18n::FromLangId(MAKELANGID(LANG_SWEDISH, SUBLANG_DEFAULT)).has_value());
}

AETHER_TEST("localization: the language names Windows lists are understood, whatever the region or script") {
    CHECK(i18n::FromLocaleName(L"pt-BR") == Language::Portuguese);
    CHECK(i18n::FromLocaleName(L"pt-PT") == Language::Portuguese);
    CHECK(i18n::FromLocaleName(L"zh-Hans-CN") == Language::Chinese);
    CHECK(i18n::FromLocaleName(L"zh-Hant-TW") == Language::Chinese);
    CHECK(i18n::FromLocaleName(L"AR-sa") == Language::Arabic);
    CHECK(i18n::FromLocaleName(L"fr_CA") == Language::French);
    CHECK(i18n::FromLocaleName(L"ru") == Language::Russian);
    CHECK(i18n::FromLocaleName(L"bn-BD") == Language::Bengali);
    CHECK(i18n::FromLocaleName(L"ur-PK") == Language::Urdu);
    CHECK(i18n::FromLocaleName(L"en-US") == Language::English);
    CHECK_FALSE(i18n::FromLocaleName(L"sv-SE").has_value());
    CHECK_FALSE(i18n::FromLocaleName(L"").has_value());
    CHECK_FALSE(i18n::FromLocaleName(L"x").has_value());
    CHECK_FALSE(i18n::FromLocaleName(L"english").has_value());
}

AETHER_TEST("localization: the language chosen in Settings is read back from the file the versions share") {
    namespace fs = std::filesystem;
    const auto folder = fs::temp_directory_path() / (L"aether-localization-test-" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ignored;
    fs::create_directories(folder, ignored);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{folder};
    const auto write = [&](const char* text) {
        const auto file = folder / L"settings.json";
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
        return file;
    };
    for (const auto& info : i18n::Languages()) {
        const auto file = write(("{\"schemaVersion\":1,\"general\":{\"language\":\"" + std::string(info.savedName) + "\"}}").c_str());
        CHECK(i18n::SavedChoice(file) == info.language);
        CHECK(i18n::StartupLanguage(file) == info.language);
    }
    // Automatic, a name from a newer version, no language, damaged or missing files: no saved choice, Windows decides.
    CHECK_FALSE(i18n::SavedChoice(write("{\"general\":{\"language\":\"automatic\"}}")).has_value());
    CHECK_FALSE(i18n::SavedChoice(write("{\"general\":{\"language\":\"klingon\"}}")).has_value());
    CHECK_FALSE(i18n::SavedChoice(write("{\"general\":{}}")).has_value());
    CHECK_FALSE(i18n::SavedChoice(write("{\"general\":")).has_value());
    CHECK_FALSE(i18n::SavedChoice(write("[]")).has_value());
    CHECK_FALSE(i18n::SavedChoice(folder / L"missing.json").has_value());
    CHECK(i18n::StartupLanguage(folder / L"missing.json") == i18n::SystemLanguage());
}

AETHER_TEST("localization: a text is translated in each of the new languages, and dates follow the chosen one") {
    EnglishAfter restore;
    CHECK(i18n::Tr(L"Settings", Language::Portuguese) == std::wstring_view(L"Configurações"));
    CHECK(i18n::Tr(L"Settings", Language::Russian) == std::wstring_view(L"Параметры"));
    CHECK(i18n::Tr(L"Settings", Language::Arabic) == std::wstring_view(L"الإعدادات"));
    CHECK(i18n::Tr(L"Settings", Language::Bengali) == std::wstring_view(L"সেটিংস"));
    CHECK(i18n::Tr(L"Settings", Language::Urdu) == std::wstring_view(L"ترتیبات"));
    const std::pair<Language, const wchar_t*> locales[] = {{Language::Portuguese, L"pt-BR"}, {Language::Russian, L"ru-RU"},
        {Language::Arabic, L"ar-SA"}, {Language::Bengali, L"bn-BD"}, {Language::Urdu, L"ur-PK"}};
    for (const auto& [language, locale] : locales) {
        i18n::SetLanguage(language, true);
        CHECK(i18n::FormattingLocale() == std::wstring_view(locale));
    }
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

AETHER_TEST("localization: every text of the catalog is in all ten languages with the same placeholders") {
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
