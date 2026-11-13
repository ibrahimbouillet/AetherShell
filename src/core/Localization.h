// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D70/D74: the interface in ten languages. English is the source and the key: every interface text is written in
// English in the code and translated here, through one catalog (Translations.inc, generated from
// tools/i18n/catalog/*.txt). A text missing from the catalog shows in English; nothing else ever fails.
//
// The ten are the most spoken languages in the world: English, Mandarin Chinese, Hindi, Spanish, French (D70), then
// Arabic, Bengali, Portuguese, Russian and Urdu (D74). "Automatic" follows the Windows display languages, in the order
// the person ranked them, and English when none is one of the ten. Arabic and Urdu read right to left.
//
// Only interface chrome goes through Tr(): never a person's content (application names, file names, the
// welcome's name, window titles), which stays as it is.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace aether::i18n {

enum class Language : std::uint8_t { English, French, Spanish, Chinese, Hindi, Portuguese, Russian, Arabic, Bengali, Urdu };
inline constexpr std::size_t kLanguageCount = 10;

struct LanguageInfo {
    Language language;
    std::string_view code;          // BCP 47: "en", "fr", "es", "zh-Hans", "hi", "pt", "ru", "ar", "bn", "ur"
    std::wstring_view nativeName;   // the language's own name for itself
    std::wstring_view englishName;
    std::string_view savedName;     // how Settings writes the choice in settings.json ("english", "french"...)
    bool rightToLeft = false;       // Arabic and Urdu
};
// In the order Settings lists them.
[[nodiscard]] std::span<const LanguageInfo> Languages() noexcept;
[[nodiscard]] const LanguageInfo& Info(Language language) noexcept;
[[nodiscard]] bool IsRightToLeft(Language language) noexcept;

// The Windows language identifier's language, when it is one of the ten.
[[nodiscard]] std::optional<Language> FromLangId(std::uint16_t langId) noexcept;
// A BCP 47 language name as Windows lists them ("pt-BR", "zh-Hans-CN", "ar-SA"), when its language is one of the ten.
[[nodiscard]] std::optional<Language> FromLocaleName(std::wstring_view name) noexcept;
// The first of the person's Windows display languages (Settings > Time & language, in the order they ranked them)
// that is one of the ten; otherwise the default display language when it is one, otherwise English.
[[nodiscard]] Language SystemLanguage() noexcept;
// What the person chose in Aether Shell's Settings, read from the settings file that every version shares: nullopt when
// the file is missing or unreadable, or the choice is "automatic". For the programs with no Settings of their own.
[[nodiscard]] std::optional<Language> SavedChoice(const std::filesystem::path& settingsFile) noexcept;
// The language of a program that starts before Settings are read: the saved choice, else the Windows languages.
[[nodiscard]] Language StartupLanguage(const std::filesystem::path& settingsFile) noexcept;

// The language the whole process shows. Set at startup from Settings and again when it changes; any thread reads it.
// `chosen`: picked in Settings rather than followed from Windows; dates and times then follow it too.
void SetLanguage(Language language, bool chosen = false) noexcept;
[[nodiscard]] Language CurrentLanguage() noexcept;
// Bumped by every SetLanguage that changed something: surfaces that cache text rebuild it.
[[nodiscard]] std::uint32_t Revision() noexcept;
// The locale dates and times are written in: empty (the person's Windows regional format) unless a language was
// chosen in Settings, then that language's ("fr-FR", "zh-CN"...).
[[nodiscard]] std::wstring_view FormattingLocale() noexcept;

// `english` in the current language, or `english` itself when the catalog has no translation. The result stays valid
// for the life of the process when translated, and as long as `english` otherwise.
[[nodiscard]] std::wstring_view Tr(std::wstring_view english) noexcept;
[[nodiscard]] std::wstring_view Tr(std::wstring_view english, Language language) noexcept;
// Every translation of `english` (the English included), for matching what a person types in any of the languages.
[[nodiscard]] std::array<std::wstring_view, kLanguageCount> AllTranslations(std::wstring_view english) noexcept;
// Whether the catalog has `english` at all.
[[nodiscard]] bool Known(std::wstring_view english) noexcept;
[[nodiscard]] std::size_t CatalogSize() noexcept;

// The translated pattern with its {} filled in, as std::format does. A translation whose placeholders do not match is
// never shown broken: the English pattern is used instead.
std::wstring FormatPattern(std::wstring_view english, std::wformat_args arguments);
template <class... Args>
[[nodiscard]] std::wstring TrFormat(std::wstring_view english, const Args&... arguments) {
    return FormatPattern(english, std::make_wformat_args(arguments...));
}

}  // namespace aether::i18n
