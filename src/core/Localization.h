// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D70/D74/D83: the interface in every language Windows 11 offers for download, plus Hindi, Bengali and Urdu. English
// is the source and the key: every interface text is written in English in the code. English is the only language built
// into the programs; every other one is a pack (AetherShell-language-<code>.json) attached to the version's GitHub
// release, downloaded when a person chooses the language, and kept in <data>/languages. A pack is used only when its
// size and SHA-256 are exactly the ones this version was built with (LanguageTable.inc, tools/i18n/languages.py).
// A text missing from the pack shows in English; nothing else ever fails.
//
// Setup runs before anything can be downloaded: it builds its own texts in every language into itself
// (UseBuiltInTexts with SetupTexts.inc).
//
// "Automatic" follows the Windows display languages, in the order the person ranked them, and English when none is one
// of these. Arabic, Hebrew and Urdu read right to left.
//
// Only interface chrome goes through Tr(): never a person's content (application names, file names, the welcome's
// name, window titles), which stays as it is.
#pragma once

#include "core/Result.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::i18n {

// English (United States) first, then by English name; LanguageTable.inc follows this order.
enum class Language : std::uint8_t {
    English, Arabic, Bengali, Bulgarian, Chinese, ChineseTraditional, Croatian, Czech, Danish, Dutch,
    EnglishUnitedKingdom, Estonian, Finnish, FrenchCanada, French, German, Greek, Hebrew, Hindi, Hungarian,
    Italian, Japanese, Korean, Latvian, Lithuanian, Norwegian, Polish, Portuguese, PortuguesePortugal, Romanian,
    Russian, SerbianLatin, Slovak, Slovenian, Spanish, SpanishMexico, Swedish, Thai, Turkish, Ukrainian, Urdu,
};
inline constexpr std::size_t kLanguageCount = 41;

struct LanguageInfo {
    Language language;
    std::string_view code;              // BCP 47: "en", "fr", "zh-Hans", "pt-BR", "sr-Latn"...
    std::wstring_view nativeName;       // the language's own name for itself
    std::wstring_view englishName;
    std::string_view savedName;         // how Settings writes the choice in settings.json ("english", "french"...)
    bool rightToLeft = false;           // Arabic, Hebrew and Urdu
    std::wstring_view formattingLocale; // dates and times when the language is chosen: "fr-FR", "zh-CN"...
    std::string_view packFile;          // the release asset; empty for English (built in) or a language with no pack
    std::uint64_t packBytes = 0;
    std::string_view packSha256;        // lowercase hexadecimal
};
// In the order Settings lists them.
[[nodiscard]] std::span<const LanguageInfo> Languages() noexcept;
[[nodiscard]] const LanguageInfo& Info(Language language) noexcept;
[[nodiscard]] bool IsRightToLeft(Language language) noexcept;
// A language that can be shown: English, or one with a pack in this version.
[[nodiscard]] bool Available(Language language) noexcept;

// The language of a BCP 47 name as Windows lists them ("de-DE", "pt-PT", "zh-Hant-TW", "es-419"), when it is one of
// these. The region or script picks the variant: British English, Mexican Spanish, Canadian French, European
// Portuguese, Traditional Chinese.
[[nodiscard]] std::optional<Language> FromLocaleName(std::wstring_view name) noexcept;
// The same for a Windows language identifier.
[[nodiscard]] std::optional<Language> FromLangId(std::uint16_t langId) noexcept;
// The first of the person's Windows display languages (Settings > Time & language, in the order they ranked them)
// that is one of these; otherwise the default display language when it is one, otherwise English.
[[nodiscard]] Language SystemLanguage() noexcept;
// D83: every language Windows has that is one of these, each once: the person's display languages in the order they
// ranked them, then the display languages installed on this PC, then the default one. Aether Shell downloads their
// packs, so that it changes language with Windows. English is in the list when Windows has it.
[[nodiscard]] std::vector<Language> WindowsLanguages() noexcept;
// What the person chose in Aether Shell's Settings, read from the settings file that every version shares: nullopt when
// the file is missing or unreadable, or the choice is "automatic". For the programs with no Settings of their own.
[[nodiscard]] std::optional<Language> SavedChoice(const std::filesystem::path& settingsFile) noexcept;
// The language a program wants before Settings are read: the saved choice, else the Windows languages.
[[nodiscard]] Language StartupLanguage(const std::filesystem::path& settingsFile) noexcept;

// --- packs (D83) ---
// Where `language`'s pack lives in `languagesDir` (<data>/languages); empty for English.
[[nodiscard]] std::filesystem::path PackPath(Language language, const std::filesystem::path& languagesDir);
// Whether the pack file is there with this version's size (its content is checked when it is used).
[[nodiscard]] bool PackInstalled(Language language, const std::filesystem::path& languagesDir) noexcept;
// Checks `bytes` against this version's size and SHA-256 for `language`, then makes its texts the language's.
// Integrity when they differ, Validation when it does not read as a pack of `language`.
[[nodiscard]] Result<void> UsePack(Language language, std::string_view bytes);
// Reads the installed pack from `languagesDir` and uses it (UsePack). NotFound when it is not there.
[[nodiscard]] Result<void> LoadInstalledPack(Language language, const std::filesystem::path& languagesDir);
// Tests only: a pack whose bytes are not this version's (no size or SHA-256 check).
[[nodiscard]] Result<void> UsePackForTests(Language language, std::string_view bytes);
// Whether `language` has its texts here: English, a pack in use, or built-in texts (setup).
[[nodiscard]] bool HasTexts(Language language) noexcept;

// Texts built into a program in every language (setup's, SetupTexts.inc): one row per text, Language order.
struct BuiltInText {
    std::array<std::wstring_view, kLanguageCount> text;
};
// Registers rows that live for the whole process (a static table). Called once at startup.
void UseBuiltInTexts(std::span<const BuiltInText> rows) noexcept;

// The language a program starts in: StartupLanguage, its pack read from `languagesDir` when it is installed, and English
// shown until the wanted language has its texts. `shown` is what SetLanguage was given; `wanted` may need a download.
struct StartLanguage {
    Language wanted = Language::English;
    Language shown = Language::English;
    bool chosen = false;   // picked in Settings rather than followed from Windows
};
StartLanguage StartIn(const std::filesystem::path& settingsFile, const std::filesystem::path& languagesDir) noexcept;

// The language the whole process shows. Set at startup from Settings and again when it changes; any thread reads it.
// `chosen`: picked in Settings rather than followed from Windows; dates and times then follow it too.
void SetLanguage(Language language, bool chosen = false) noexcept;
[[nodiscard]] Language CurrentLanguage() noexcept;
// Bumped by every SetLanguage that changed something, and by a pack coming into use: surfaces that cache text rebuild it.
[[nodiscard]] std::uint32_t Revision() noexcept;
// The locale dates and times are written in: empty (the person's Windows regional format) unless a language was
// chosen in Settings, then that language's ("fr-FR", "zh-CN"...).
[[nodiscard]] std::wstring_view FormattingLocale() noexcept;

// `english` in the current language, or `english` itself when that language has no translation of it. The result stays
// valid for the life of the process when translated (packs are never freed), and as long as `english` otherwise.
[[nodiscard]] std::wstring_view Tr(std::wstring_view english) noexcept;
[[nodiscard]] std::wstring_view Tr(std::wstring_view english, Language language) noexcept;
// `english` in every language that has its texts here (the English included; empty for the others), for matching what a
// person types in any of them.
[[nodiscard]] std::array<std::wstring_view, kLanguageCount> AllTranslations(std::wstring_view english) noexcept;

// The translated pattern with its {} filled in, as std::format does. A translation whose placeholders do not match is
// never shown broken: the English pattern is used instead.
std::wstring FormatPattern(std::wstring_view english, std::wformat_args arguments);
template <class... Args>
[[nodiscard]] std::wstring TrFormat(std::wstring_view english, const Args&... arguments) {
    return FormatPattern(english, std::make_wformat_args(arguments...));
}

}  // namespace aether::i18n
