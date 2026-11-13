// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Localization.h"

#include "core/FileSystem.h"
#include "core/Json.h"

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
    {Language::English, "en", L"English", L"English", "english", false},
    {Language::French, "fr", L"Français", L"French", "french", false},
    {Language::Spanish, "es", L"Español", L"Spanish", "spanish", false},
    {Language::Chinese, "zh-Hans", L"中文（简体）", L"Chinese (Simplified)", "chinese", false},
    {Language::Hindi, "hi", L"हिन्दी", L"Hindi", "hindi", false},
    {Language::Portuguese, "pt", L"Português", L"Portuguese", "portuguese", false},
    {Language::Russian, "ru", L"Русский", L"Russian", "russian", false},
    {Language::Arabic, "ar", L"العربية", L"Arabic", "arabic", true},
    {Language::Bengali, "bn", L"বাংলা", L"Bengali", "bengali", false},
    {Language::Urdu, "ur", L"اردو", L"Urdu", "urdu", true},
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
        case LANG_PORTUGUESE: return Language::Portuguese;
        case LANG_RUSSIAN: return Language::Russian;
        case LANG_ARABIC: return Language::Arabic;
        case LANG_BENGALI: return Language::Bengali;
        case LANG_URDU: return Language::Urdu;
        default: return std::nullopt;
    }
}

bool IsRightToLeft(Language language) noexcept { return Info(language).rightToLeft; }

std::optional<Language> FromLocaleName(std::wstring_view name) noexcept {
    const auto end = name.find_first_of(L"-_");
    const auto primary = name.substr(0, end);
    if (primary.size() < 2 || primary.size() > 3) return std::nullopt;
    wchar_t lower[4]{};
    for (std::size_t i = 0; i < primary.size(); ++i) {
        const wchar_t c = primary[i];
        lower[i] = (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
    }
    const std::wstring_view code(lower, primary.size());
    // The primary language subtag decides (Traditional Chinese readers read the Simplified better than English).
    if (code == L"en") return Language::English;
    if (code == L"fr") return Language::French;
    if (code == L"es") return Language::Spanish;
    if (code == L"zh") return Language::Chinese;
    if (code == L"hi") return Language::Hindi;
    if (code == L"pt") return Language::Portuguese;
    if (code == L"ru") return Language::Russian;
    if (code == L"ar") return Language::Arabic;
    if (code == L"bn") return Language::Bengali;
    if (code == L"ur") return Language::Urdu;
    return std::nullopt;
}

Language SystemLanguage() noexcept {
    try {
        // Windows lets a person rank several display languages. The first of them that is one of the ten wins, so a
        // person whose first language is not translated still gets their second one, not English.
        ULONG count = 0, size = 0;
        if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 1 && size < 4096) {
            std::wstring names(size, L'\0');
            if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &size)) {
                for (const wchar_t* name = names.c_str(); *name; name += std::wstring_view(name).size() + 1) {
                    if (const auto language = FromLocaleName(name)) return *language;
                }
            }
        }
    } catch (...) {
    }
    return FromLangId(::GetUserDefaultUILanguage()).value_or(Language::English);
}

std::optional<Language> SavedChoice(const std::filesystem::path& settingsFile) noexcept {
    try {
        constexpr std::uint64_t kLimit = 1024 * 1024;
        auto text = fs::ReadFile(settingsFile, kLimit);
        if (!text) return std::nullopt;
        json::ParseOptions options;
        options.maxInputBytes = kLimit;
        options.maxDepth = 16;
        auto parsed = json::Parse(*text, options);
        if (!parsed || !parsed->IsObject()) return std::nullopt;
        const auto* general = parsed->Find("general");
        if (!general || !general->IsObject()) return std::nullopt;
        const auto* saved = general->Find("language");
        if (!saved || !saved->AsString()) return std::nullopt;
        for (const auto& info : kLanguages)
            if (info.savedName == *saved->AsString()) return info.language;
        return std::nullopt;   // "automatic", or a name written by a newer version
    } catch (...) {
        return std::nullopt;
    }
}

Language StartupLanguage(const std::filesystem::path& settingsFile) noexcept {
    return SavedChoice(settingsFile).value_or(SystemLanguage());
}

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
        case Language::Portuguese: return L"pt-BR";
        case Language::Russian: return L"ru-RU";
        case Language::Arabic: return L"ar-SA";
        case Language::Bengali: return L"bn-BD";
        case Language::Urdu: return L"ur-PK";
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
