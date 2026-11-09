// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Localization.h"

#include <windows.h>

#include <atomic>
#include <unordered_map>

namespace aether::i18n {
namespace {

// One interface text in each language, in Language's order. An empty translation means "not translated": the
// English is shown.
struct Entry {
    std::array<std::wstring_view, kLanguageCount> text;
};

constexpr Entry kCatalog[] = {
#include "core/Translations.inc"
};

constexpr std::array<LanguageInfo, kLanguageCount> kLanguages{{
    {Language::English, "en", L"English", L"English"},
    {Language::French, "fr", L"Français", L"French"},
    {Language::Spanish, "es", L"Español", L"Spanish"},
    {Language::Chinese, "zh-Hans", L"中文（简体）", L"Chinese (Simplified)"},
    {Language::Hindi, "hi", L"हिन्दी", L"Hindi"},
}};

std::atomic<Language> gLanguage{Language::English};
std::atomic<bool> gChosen{false};
std::atomic<std::uint32_t> gRevision{0};

// English text -> its entry. Built once, on first use; read-only afterwards, so any thread may look up.
const std::unordered_map<std::wstring_view, const Entry*>& Index() {
    static const auto index = [] {
        std::unordered_map<std::wstring_view, const Entry*> map;
        map.reserve(std::size(kCatalog));
        for (const auto& entry : kCatalog) map.emplace(entry.text[0], &entry);
        return map;
    }();
    return index;
}

const Entry* Find(std::wstring_view english) noexcept {
    if (english.empty()) return nullptr;
    try {
        const auto& index = Index();
        const auto found = index.find(english);
        return found == index.end() ? nullptr : found->second;
    } catch (...) {
        return nullptr;
    }
}

}  // namespace

std::span<const LanguageInfo> Languages() noexcept { return kLanguages; }

const LanguageInfo& Info(Language language) noexcept {
    const auto index = static_cast<std::size_t>(language);
    return kLanguages[index < kLanguages.size() ? index : 0];
}

std::optional<Language> FromLangId(std::uint16_t langId) noexcept {
    switch (PRIMARYLANGID(langId)) {
        case LANG_ENGLISH: return Language::English;
        case LANG_FRENCH: return Language::French;
        case LANG_SPANISH: return Language::Spanish;
        case LANG_CHINESE: return Language::Chinese;   // Traditional Chinese readers read the Simplified better than English
        case LANG_HINDI: return Language::Hindi;
        default: return std::nullopt;
    }
}

Language SystemLanguage() noexcept { return FromLangId(::GetUserDefaultUILanguage()).value_or(Language::English); }

void SetLanguage(Language language, bool chosen) noexcept {
    if (static_cast<std::size_t>(language) >= kLanguageCount) language = Language::English;
    const bool before = gChosen.exchange(chosen);
    if (gLanguage.exchange(language) != language || before != chosen) gRevision.fetch_add(1);
}

std::wstring_view FormattingLocale() noexcept {
    if (!gChosen.load()) return {};
    switch (gLanguage.load()) {
        case Language::English: return L"en-US";
        case Language::French: return L"fr-FR";
        case Language::Spanish: return L"es-ES";
        case Language::Chinese: return L"zh-CN";
        case Language::Hindi: return L"hi-IN";
    }
    return {};
}

Language CurrentLanguage() noexcept { return gLanguage.load(); }
std::uint32_t Revision() noexcept { return gRevision.load(); }

std::wstring_view Tr(std::wstring_view english, Language language) noexcept {
    if (language == Language::English) return english;
    const auto* entry = Find(english);
    if (!entry) return english;
    const auto translated = entry->text[static_cast<std::size_t>(language)];
    return translated.empty() ? english : translated;
}

std::wstring_view Tr(std::wstring_view english) noexcept { return Tr(english, CurrentLanguage()); }

std::array<std::wstring_view, kLanguageCount> AllTranslations(std::wstring_view english) noexcept {
    std::array<std::wstring_view, kLanguageCount> all{};
    all[0] = english;
    if (const auto* entry = Find(english))
        for (std::size_t i = 1; i < kLanguageCount; ++i) all[i] = entry->text[i];
    return all;
}

bool Known(std::wstring_view english) noexcept { return Find(english) != nullptr; }
std::size_t CatalogSize() noexcept { return std::size(kCatalog); }

std::wstring FormatPattern(std::wstring_view english, std::wformat_args arguments) {
    const auto pattern = Tr(english);
    try {
        return std::vformat(pattern, arguments);
    } catch (...) {
    }
    try {
        return std::vformat(english, arguments);
    } catch (...) {
        return std::wstring(english);
    }
}

}  // namespace aether::i18n
