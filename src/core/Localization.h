// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D70: the interface in five languages. English is the source and the key: every interface text is written in
// English in the code and translated here, through one catalog (Translations.inc, generated from
// tools/i18n/catalog/*.txt). A text missing from the catalog shows in English; nothing else ever fails.
//
// The five are the most spoken languages in the world (English, Mandarin Chinese, Hindi, Spanish, French).
// "Automatic" follows the Windows display language, and English for any other.
//
// Only interface chrome goes through Tr(): never a person's content (application names, file names, the
// welcome's name, window titles), which stays as it is.
#pragma once

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace aether::i18n {

enum class Language : std::uint8_t { English, French, Spanish, Chinese, Hindi };
inline constexpr std::size_t kLanguageCount = 5;

struct LanguageInfo {
    Language language;
    std::string_view code;          // BCP 47: "en", "fr", "es", "zh-Hans", "hi"
    std::wstring_view nativeName;   // the language's own name for itself
    std::wstring_view englishName;
};
// In the order Settings lists them.
[[nodiscard]] std::span<const LanguageInfo> Languages() noexcept;
[[nodiscard]] const LanguageInfo& Info(Language language) noexcept;

// The Windows language identifier's language, when it is one of the five.
[[nodiscard]] std::optional<Language> FromLangId(std::uint16_t langId) noexcept;
// The Windows display language when it is one of the five, otherwise English.
[[nodiscard]] Language SystemLanguage() noexcept;

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
