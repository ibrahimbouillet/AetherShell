// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/Localization.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <string>

using namespace aether;
using i18n::Language;

namespace {
// Restores English after each test: the language is process-wide.
struct EnglishAfter {
    ~EnglishAfter() { i18n::SetLanguage(Language::English); }
};

namespace fs = std::filesystem;
struct TempFolder {
    fs::path path;
    explicit TempFolder(const wchar_t* name)
        : path(fs::temp_directory_path() / (std::wstring(name) + L"-" + std::to_wstring(::GetCurrentProcessId()))) {
        std::error_code ignored;
        fs::remove_all(path, ignored);
        fs::create_directories(path, ignored);
    }
    ~TempFolder() { std::error_code ignored; fs::remove_all(path, ignored); }
    fs::path Write(const wchar_t* name, std::string_view text) const {
        const auto file = path / name;
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file;
    }
};

// A small French pack for these tests (UsePackForTests: no size or SHA-256 check). JSON escapes keep the source ASCII.
constexpr std::string_view kFrenchPack = R"({"aetherLanguagePack":1,"language":"fr","texts":[
    ["Settings","Param\u00e8tres"],
    ["Page {} of {}","Page {} sur {}"],
    ["Received {} of {} ({}%).","Re\u00e7u {} sur {}."]]})";
}  // namespace

AETHER_TEST("localization: the 38 languages of Windows' download, Bengali, Hindi and Urdu, each named in its own language") {
    const auto languages = i18n::Languages();
    REQUIRE_EQ(languages.size(), i18n::kLanguageCount);
    CHECK_EQ(i18n::kLanguageCount, std::size_t{41});
    std::set<std::string_view> codes, savedNames, packs;
    for (const auto& info : languages) {
        CHECK(!info.nativeName.empty());
        CHECK(!info.englishName.empty());
        CHECK(!info.formattingLocale.empty());
        CHECK(codes.insert(info.code).second);
        CHECK(savedNames.insert(info.savedName).second);
        if (info.language == Language::English) {
            CHECK(info.packFile.empty());   // built in
        } else {
            CHECK(packs.insert(info.packFile).second);
            CHECK_EQ(info.packFile, std::format("AetherShell-language-{}.json", info.code));
        }
    }
    // Microsoft's Windows 11 download: Arabic ... Chinese Traditional, English twice and Spanish, French, Portuguese
    // and Chinese twice each.
    for (const auto* code : {"ar", "bg", "cs", "da", "de", "el", "en-GB", "en", "es", "es-MX", "et", "fi", "fr-CA", "fr", "he",
                             "hr", "hu", "it", "ja", "ko", "lt", "lv", "nb", "nl", "pl", "pt-BR", "pt-PT", "ro", "ru", "sk", "sl",
                             "sr-Latn", "sv", "th", "tr", "uk", "zh-Hans", "zh-Hant"})
        CHECK(codes.contains(code));
    for (const auto* code : {"bn", "hi", "ur"}) CHECK(codes.contains(code));
    CHECK(i18n::Info(Language::French).nativeName == std::wstring_view(L"Français (France)"));
    CHECK(i18n::Info(Language::Chinese).code == std::string_view("zh-Hans"));
    CHECK(i18n::Info(Language::ChineseTraditional).code == std::string_view("zh-Hant"));
    CHECK(i18n::Info(Language::German).nativeName == std::wstring_view(L"Deutsch"));
    CHECK(i18n::Info(Language::Japanese).nativeName == std::wstring_view(L"日本語"));
    // The names every earlier version saved stay the same.
    CHECK(i18n::Info(Language::Portuguese).savedName == std::string_view("portuguese"));
    CHECK(i18n::Info(Language::Chinese).savedName == std::string_view("chinese"));
    CHECK(i18n::Info(Language::Spanish).savedName == std::string_view("spanish"));
}

AETHER_TEST("localization: Arabic, Hebrew and Urdu read right to left, the others left to right") {
    for (const auto& info : i18n::Languages())
        CHECK_EQ(i18n::IsRightToLeft(info.language),
                 info.language == Language::Arabic || info.language == Language::Hebrew || info.language == Language::Urdu);
}

AETHER_TEST("localization: Windows' display language chooses one of these, its region the variant") {
    CHECK(i18n::FromLangId(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH_CANADIAN)) == Language::FrenchCanada);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH)) == Language::French);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MEXICAN)) == Language::SpanishMexico);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH_MODERN)) == Language::Spanish);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) == Language::ChineseTraditional);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) == Language::Chinese);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK)) == Language::EnglishUnitedKingdom);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) == Language::English);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_PORTUGUESE, SUBLANG_PORTUGUESE_BRAZILIAN)) == Language::Portuguese);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_PORTUGUESE, SUBLANG_PORTUGUESE)) == Language::PortuguesePortugal);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_SWEDISH, SUBLANG_DEFAULT)) == Language::Swedish);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_HEBREW, SUBLANG_DEFAULT)) == Language::Hebrew);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_HINDI, SUBLANG_DEFAULT)) == Language::Hindi);
    CHECK(i18n::FromLangId(MAKELANGID(LANG_ARABIC, SUBLANG_ARABIC_EGYPT)) == Language::Arabic);
    CHECK_FALSE(i18n::FromLangId(MAKELANGID(LANG_ICELANDIC, SUBLANG_DEFAULT)).has_value());
}

AETHER_TEST("localization: the language names Windows lists are understood, whatever the region or script") {
    CHECK(i18n::FromLocaleName(L"pt-BR") == Language::Portuguese);
    CHECK(i18n::FromLocaleName(L"pt") == Language::Portuguese);
    CHECK(i18n::FromLocaleName(L"pt-PT") == Language::PortuguesePortugal);
    CHECK(i18n::FromLocaleName(L"pt-AO") == Language::PortuguesePortugal);
    CHECK(i18n::FromLocaleName(L"zh-Hans-CN") == Language::Chinese);
    CHECK(i18n::FromLocaleName(L"zh-CN") == Language::Chinese);
    CHECK(i18n::FromLocaleName(L"zh-Hant-TW") == Language::ChineseTraditional);
    CHECK(i18n::FromLocaleName(L"zh-HK") == Language::ChineseTraditional);
    CHECK(i18n::FromLocaleName(L"AR-sa") == Language::Arabic);
    CHECK(i18n::FromLocaleName(L"fr_CA") == Language::FrenchCanada);
    CHECK(i18n::FromLocaleName(L"fr-BE") == Language::French);
    CHECK(i18n::FromLocaleName(L"es-ES") == Language::Spanish);
    CHECK(i18n::FromLocaleName(L"es") == Language::Spanish);
    CHECK(i18n::FromLocaleName(L"es-419") == Language::SpanishMexico);
    CHECK(i18n::FromLocaleName(L"es-MX") == Language::SpanishMexico);
    CHECK(i18n::FromLocaleName(L"en-US") == Language::English);
    CHECK(i18n::FromLocaleName(L"en") == Language::English);
    CHECK(i18n::FromLocaleName(L"en-GB") == Language::EnglishUnitedKingdom);
    CHECK(i18n::FromLocaleName(L"en-AU") == Language::EnglishUnitedKingdom);
    CHECK(i18n::FromLocaleName(L"nb-NO") == Language::Norwegian);
    CHECK(i18n::FromLocaleName(L"nn-NO") == Language::Norwegian);
    CHECK(i18n::FromLocaleName(L"sr-Latn-RS") == Language::SerbianLatin);
    CHECK(i18n::FromLocaleName(L"sv-SE") == Language::Swedish);
    CHECK(i18n::FromLocaleName(L"he-IL") == Language::Hebrew);
    CHECK(i18n::FromLocaleName(L"ru") == Language::Russian);
    CHECK(i18n::FromLocaleName(L"ur-PK") == Language::Urdu);
    CHECK_FALSE(i18n::FromLocaleName(L"is-IS").has_value());
    CHECK_FALSE(i18n::FromLocaleName(L"").has_value());
    CHECK_FALSE(i18n::FromLocaleName(L"x").has_value());
    CHECK_FALSE(i18n::FromLocaleName(L"english").has_value());
}

AETHER_TEST("localization: Windows' languages are listed once each, the one followed among them") {
    const auto languages = i18n::WindowsLanguages();
    std::set<Language> seen(languages.begin(), languages.end());
    CHECK_EQ(seen.size(), languages.size());
    const auto system = i18n::SystemLanguage();
    CHECK(system == Language::English || seen.contains(system));
    TEST_NOTE(std::format("{} of Windows' languages are among these", languages.size()));
}

AETHER_TEST("localization: the language chosen in Settings is read back from the file the versions share") {
    const TempFolder folder(L"aether-localization-test");
    for (const auto& info : i18n::Languages()) {
        const auto file = folder.Write(L"settings.json", "{\"schemaVersion\":1,\"general\":{\"language\":\"" + std::string(info.savedName) + "\"}}");
        CHECK(i18n::SavedChoice(file) == info.language);
        CHECK(i18n::StartupLanguage(file) == info.language);
    }
    // Automatic, a name from a newer version, no language, damaged or missing files: no saved choice, Windows decides.
    CHECK_FALSE(i18n::SavedChoice(folder.Write(L"settings.json", "{\"general\":{\"language\":\"automatic\"}}")).has_value());
    CHECK_FALSE(i18n::SavedChoice(folder.Write(L"settings.json", "{\"general\":{\"language\":\"klingon\"}}")).has_value());
    CHECK_FALSE(i18n::SavedChoice(folder.Write(L"settings.json", "{\"general\":{}}")).has_value());
    CHECK_FALSE(i18n::SavedChoice(folder.Write(L"settings.json", "{\"general\":")).has_value());
    CHECK_FALSE(i18n::SavedChoice(folder.Write(L"settings.json", "[]")).has_value());
    CHECK_FALSE(i18n::SavedChoice(folder.path / L"missing.json").has_value());
    CHECK(i18n::StartupLanguage(folder.path / L"missing.json") == i18n::SystemLanguage());
}

AETHER_TEST("localization: English is built in, every other language is a pack of this version's release") {
    CHECK(i18n::Available(Language::English));
    CHECK(i18n::HasTexts(Language::English));
    CHECK(i18n::PackPath(Language::English, L"C:\\data\\languages").empty());
    CHECK(i18n::PackPath(Language::German, L"C:\\data\\languages") == fs::path(L"C:\\data\\languages\\AetherShell-language-de.json"));
    for (const auto& info : i18n::Languages()) {
        if (info.language == Language::English) continue;
        // Every pack is published with this version (languages.py build): a size and a SHA-256 for each.
        CHECK(info.packBytes > 0);
        CHECK_EQ(info.packSha256.size(), std::size_t{64});
        CHECK(i18n::Available(info.language));
    }
}

AETHER_TEST("localization: a pack is used only when it is exactly this version's") {
    const TempFolder folder(L"aether-language-packs-test");
    // Wrong size: refused before anything is read.
    auto refused = i18n::UsePack(Language::German, "{}");
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().code == ErrorCode::Integrity);
    // The right size, other bytes: refused by the SHA-256.
    const std::string forged(static_cast<std::size_t>(i18n::Info(Language::German).packBytes), ' ');
    refused = i18n::UsePack(Language::German, forged);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().code == ErrorCode::Integrity);
    CHECK_FALSE(i18n::HasTexts(Language::German));
    // On disk: the size is what PackInstalled looks at; the content is checked when it is loaded.
    CHECK_FALSE(i18n::PackInstalled(Language::German, folder.path));
    CHECK(i18n::LoadInstalledPack(Language::German, folder.path).error().code == ErrorCode::NotFound);
    folder.Write(L"AetherShell-language-de.json", forged);
    CHECK(i18n::PackInstalled(Language::German, folder.path));
    CHECK(i18n::LoadInstalledPack(Language::German, folder.path).error().code == ErrorCode::Integrity);
    CHECK_FALSE(i18n::HasTexts(Language::German));
    // English has no pack.
    CHECK(i18n::UsePack(Language::English, "{}").error().code == ErrorCode::Unavailable);
}

AETHER_TEST("localization: a pack that is not one of this language is refused") {
    CHECK(i18n::UsePackForTests(Language::Italian, "not json").error().code == ErrorCode::Validation);
    CHECK(i18n::UsePackForTests(Language::Italian, R"({"aetherLanguagePack":2,"language":"it","texts":[]})").error().code == ErrorCode::Validation);
    CHECK(i18n::UsePackForTests(Language::Italian, R"({"aetherLanguagePack":1,"language":"fr","texts":[]})").error().code == ErrorCode::Validation);
    CHECK(i18n::UsePackForTests(Language::Italian, R"({"aetherLanguagePack":1,"language":"it","texts":[["a"]]})").error().code == ErrorCode::Validation);
    CHECK(i18n::UsePackForTests(Language::Italian, "{\"aetherLanguagePack\":1,\"language\":\"it\",\"texts\":[[\"a\",\"\xC3\"]]}").error().code == ErrorCode::Validation);
    CHECK_FALSE(i18n::HasTexts(Language::Italian));
}

AETHER_TEST("localization: a program starts in its saved language once its pack is here, in English until then") {
    EnglishAfter restore;
    const TempFolder folder(L"aether-language-start-test");
    const auto settings = folder.Write(L"settings.json", "{\"general\":{\"language\":\"dutch\"}}");
    const auto start = i18n::StartIn(settings, folder.path / L"languages");
    CHECK(start.wanted == Language::Dutch);
    CHECK(start.chosen);
    CHECK(start.shown == Language::English);   // no Dutch pack in this folder, none in use
    CHECK(i18n::CurrentLanguage() == Language::English);
    CHECK(i18n::FormattingLocale().empty());   // dates keep the person's format until Dutch is shown
}

AETHER_TEST("localization: a pack's text is shown, an unknown one shows as it is") {
    EnglishAfter restore;
    REQUIRE(i18n::UsePackForTests(Language::French, kFrenchPack).ok());
    CHECK(i18n::HasTexts(Language::French));
    CHECK(i18n::Tr(L"Settings") == std::wstring_view(L"Settings"));
    i18n::SetLanguage(Language::French);
    CHECK(i18n::Tr(L"Settings") == std::wstring_view(L"Paramètres"));
    CHECK(i18n::Tr(L"Google Chrome") == std::wstring_view(L"Google Chrome"));   // a name, never translated
    CHECK(i18n::Tr(L"") == std::wstring_view(L""));
    CHECK(i18n::Tr(L"Settings", Language::English) == std::wstring_view(L"Settings"));
    const auto all = i18n::AllTranslations(L"Settings");
    CHECK(all[0] == std::wstring_view(L"Settings"));
    CHECK(all[static_cast<std::size_t>(Language::French)] == std::wstring_view(L"Paramètres"));
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
    const std::pair<Language, const wchar_t*> locales[] = {{Language::Portuguese, L"pt-BR"}, {Language::PortuguesePortugal, L"pt-PT"},
        {Language::Russian, L"ru-RU"}, {Language::Arabic, L"ar-SA"}, {Language::EnglishUnitedKingdom, L"en-GB"},
        {Language::SerbianLatin, L"sr-Latn-RS"}, {Language::Japanese, L"ja-JP"}};
    for (const auto& [language, locale] : locales) {
        i18n::SetLanguage(language, true);
        CHECK(i18n::FormattingLocale() == std::wstring_view(locale));
    }
}

AETHER_TEST("localization: patterns are filled in each language, a broken one in English") {
    EnglishAfter restore;
    REQUIRE(i18n::UsePackForTests(Language::French, kFrenchPack).ok());
    CHECK_EQ(i18n::TrFormat(L"Version {} is available", std::wstring(L"4.0")), std::wstring(L"Version 4.0 is available"));
    i18n::SetLanguage(Language::French);
    CHECK_EQ(i18n::TrFormat(L"Page {} of {}", 1, 2), std::wstring(L"Page 1 sur 2"));
    // The pack's pattern lost a placeholder: the English one is shown, never a broken text.
    CHECK_EQ(i18n::TrFormat(L"Received {} of {} ({}%).", 1, 2, 50), std::wstring(L"Received 1 of 2 (50%)."));
    // A pattern that is not in the pack still formats.
    CHECK_EQ(i18n::TrFormat(L"{} + {}", 1, 2), std::wstring(L"1 + 2"));
}

AETHER_TEST("localization: built-in texts (setup's) are shown in their language without a pack") {
    EnglishAfter restore;
    static constexpr i18n::BuiltInText rows[] = {
        {{L"Aether Shell Setup", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"Aether Shell-Setup"}},
    };
    static_assert(static_cast<std::size_t>(Language::German) == 15);
    i18n::UseBuiltInTexts(rows);
    CHECK(i18n::HasTexts(Language::German));
    CHECK(i18n::Tr(L"Aether Shell Setup", Language::German) == std::wstring_view(L"Aether Shell-Setup"));
    CHECK(i18n::Tr(L"Aether Shell Setup", Language::Greek) == std::wstring_view(L"Aether Shell Setup"));   // an empty column: English
    i18n::UseBuiltInTexts({});
    CHECK_FALSE(i18n::HasTexts(Language::German));
}
