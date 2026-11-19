// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Localization.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Strings.h"

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace aether::i18n {
namespace {

constexpr std::array<LanguageInfo, kLanguageCount> kLanguages{{
#include "core/LanguageTable.inc"
}};

constexpr bool InLanguageOrder() {
    for (std::size_t i = 0; i < kLanguages.size(); ++i)
        if (static_cast<std::size_t>(kLanguages[i].language) != i) return false;
    return true;
}
static_assert(InLanguageOrder(), "LanguageTable.inc must follow the Language enum");

constexpr std::uint64_t kMaxPackBytes = 2u * 1024u * 1024u;
constexpr std::size_t kMaxPackTexts = 8192;

// One language's texts, English -> translation. Never freed once in use, so that Tr's views stay valid.
struct Pack {
    std::deque<std::wstring> storage;
    std::unordered_map<std::wstring_view, std::wstring_view> texts;
};

std::mutex gPacksMutex;
std::vector<std::unique_ptr<const Pack>> gPackStorage;   // every pack ever in use; append only
std::array<std::atomic<const Pack*>, kLanguageCount> gPacks{};

// Built-in rows (setup): English -> row. Set once.
struct BuiltIn {
    std::unordered_map<std::wstring_view, const BuiltInText*> rows;
};
std::atomic<const BuiltIn*> gBuiltIn{nullptr};

std::atomic<Language> gLanguage{Language::English};
std::atomic<bool> gChosen{false};
std::atomic<std::uint32_t> gRevision{0};

std::size_t IndexOf(Language language) noexcept {
    const auto index = static_cast<std::size_t>(language);
    return index < kLanguageCount ? index : 0;
}

std::string LowerHex(const unsigned char* bytes, std::size_t size) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        hex.push_back(kDigits[bytes[i] >> 4]);
        hex.push_back(kDigits[bytes[i] & 0x0F]);
    }
    return hex;
}

std::optional<std::string> Sha256Hex(std::string_view bytes) noexcept {
    try {
        unsigned char digest[32]{};
        if (!BCRYPT_SUCCESS(::BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                         reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                         static_cast<ULONG>(bytes.size()), digest, sizeof(digest))))
            return std::nullopt;
        return LowerHex(digest, sizeof(digest));
    } catch (...) {
        return std::nullopt;
    }
}

// {"aetherLanguagePack":1,"language":"<code>","texts":[["English","translation"],...]}
Result<std::unique_ptr<Pack>> ParsePack(Language language, std::string_view bytes) {
    if (bytes.size() > kMaxPackBytes) return MakeError(ErrorCode::Integrity, "language pack too large");
    if (!text::IsValidUtf8(bytes)) return MakeError(ErrorCode::Validation, "language pack is not UTF-8");
    json::ParseOptions options;
    options.maxInputBytes = kMaxPackBytes;
    options.maxDepth = 8;
    auto parsed = json::Parse(bytes, options);
    if (!parsed) return MakeError(ErrorCode::Validation, "language pack does not read as JSON");
    if (!parsed->IsObject()) return MakeError(ErrorCode::Validation, "language pack is not an object");
    const auto* format = parsed->Find("aetherLanguagePack");
    if (!format || format->AsInt64() != std::optional<std::int64_t>{1}) return MakeError(ErrorCode::Validation, "unknown language pack format");
    const auto* code = parsed->Find("language");
    if (!code || !code->AsString() || *code->AsString() != Info(language).code)
        return MakeError(ErrorCode::Validation, "language pack is another language's");
    const auto* texts = parsed->Find("texts");
    if (!texts || !texts->AsArray()) return MakeError(ErrorCode::Validation, "language pack has no texts");
    const auto& rows = *texts->AsArray();
    if (rows.size() > kMaxPackTexts) return MakeError(ErrorCode::Validation, "language pack has too many texts");
    auto pack = std::make_unique<Pack>();
    pack->texts.reserve(rows.size());
    for (const auto& row : rows) {
        const auto* pair = row.AsArray();
        if (!pair || pair->size() != 2 || !(*pair)[0].AsString() || !(*pair)[1].AsString())
            return MakeError(ErrorCode::Validation, "language pack text is not a pair");
        const auto& english = pack->storage.emplace_back(text::Widen(*(*pair)[0].AsString()));
        const auto& translated = pack->storage.emplace_back(text::Widen(*(*pair)[1].AsString()));
        if (english.empty() || translated.empty()) continue;
        pack->texts.emplace(english, translated);
    }
    return pack;
}

void Install(Language language, std::unique_ptr<Pack> pack) {
    const Pack* raw = pack.get();
    {
        std::lock_guard lock(gPacksMutex);
        gPackStorage.push_back(std::move(pack));
    }
    gPacks[IndexOf(language)].store(raw, std::memory_order_release);
    gRevision.fetch_add(1);
}

std::optional<std::wstring_view> FromPack(std::wstring_view english, Language language) noexcept {
    const Pack* pack = gPacks[IndexOf(language)].load(std::memory_order_acquire);
    if (!pack) return std::nullopt;
    try {
        const auto found = pack->texts.find(english);
        if (found != pack->texts.end()) return found->second;
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<std::wstring_view> FromBuiltIn(std::wstring_view english, Language language) noexcept {
    const BuiltIn* builtIn = gBuiltIn.load(std::memory_order_acquire);
    if (!builtIn) return std::nullopt;
    try {
        const auto found = builtIn->rows.find(english);
        if (found == builtIn->rows.end()) return std::nullopt;
        const auto translated = found->second->text[IndexOf(language)];
        if (!translated.empty()) return translated;
    } catch (...) {
    }
    return std::nullopt;
}

std::wstring Lower(std::wstring_view value) {
    std::wstring lower(value);
    for (auto& c : lower)
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + (L'a' - L'A'));
    return lower;
}

}  // namespace

std::span<const LanguageInfo> Languages() noexcept { return kLanguages; }
const LanguageInfo& Info(Language language) noexcept { return kLanguages[IndexOf(language)]; }
bool IsRightToLeft(Language language) noexcept { return Info(language).rightToLeft; }
bool Available(Language language) noexcept { return language == Language::English || Info(language).packBytes > 0; }

std::optional<Language> FromLocaleName(std::wstring_view name) noexcept {
    try {
        // language[-script][-region], '_' accepted as Windows sometimes writes it.
        std::wstring tags[3];
        std::size_t count = 0;
        for (std::size_t start = 0; start <= name.size() && count < 3;) {
            const auto end = name.find_first_of(L"-_", start);
            tags[count++] = Lower(name.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start));
            if (end == std::wstring_view::npos) break;
            start = end + 1;
        }
        const std::wstring& primary = tags[0];
        if (primary.size() < 2 || primary.size() > 3) return std::nullopt;
        std::wstring script, region;
        for (std::size_t i = 1; i < count; ++i) {
            if (tags[i].size() == 4) script = tags[i];
            else if (tags[i].size() == 2 || tags[i].size() == 3) region = tags[i];
        }
        if (primary == L"en") {
            // English (United States) for the Americas' and the Philippines' English; British spelling elsewhere.
            if (region.empty() || region == L"us" || region == L"ca" || region == L"ph" || region == L"um" || region == L"vi" ||
                region == L"as" || region == L"gu" || region == L"pr")
                return Language::English;
            return Language::EnglishUnitedKingdom;
        }
        if (primary == L"es") return region.empty() || region == L"es" ? Language::Spanish : Language::SpanishMexico;
        if (primary == L"fr") return region == L"ca" ? Language::FrenchCanada : Language::French;
        if (primary == L"pt") return region.empty() || region == L"br" ? Language::Portuguese : Language::PortuguesePortugal;
        if (primary == L"zh") {
            const bool traditional = script == L"hant" || region == L"tw" || region == L"hk" || region == L"mo";
            return traditional ? Language::ChineseTraditional : Language::Chinese;
        }
        if (primary == L"nb" || primary == L"no" || primary == L"nn") return Language::Norwegian;
        if (primary == L"sr") return Language::SerbianLatin;   // the Latin script is the one Windows downloads
        for (const auto& info : kLanguages) {
            const std::wstring code = Lower(text::Widen(info.code));
            if (code == primary) return info.language;
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Language> FromLangId(std::uint16_t langId) noexcept {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (::LCIDToLocaleName(MAKELCID(langId, SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) <= 0) return std::nullopt;
    return FromLocaleName(name);
}

Language SystemLanguage() noexcept {
    try {
        // Windows lets a person rank several display languages. The first of them that is one of these wins, so a
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

std::vector<Language> WindowsLanguages() noexcept {
    try {
        std::vector<Language> languages;
        const auto add = [&languages](const wchar_t* name) {
            if (const auto language = FromLocaleName(name); language && std::find(languages.begin(), languages.end(), *language) == languages.end())
                languages.push_back(*language);
        };
        ULONG count = 0, size = 0;
        if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 1 && size < 4096) {
            std::wstring names(size, L'\0');
            if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &size))
                for (const wchar_t* name = names.c_str(); *name; name += std::wstring_view(name).size() + 1) add(name);
        }
        // The display languages installed on this PC: a person switches Windows to one of them in Settings.
        std::vector<std::wstring> installed;
        (void)::EnumUILanguagesW([](LPWSTR name, LONG_PTR context) -> BOOL {
            try {
                auto& names = *reinterpret_cast<std::vector<std::wstring>*>(context);
                if (name && names.size() < 256) names.emplace_back(name);
                return TRUE;
            } catch (...) {
                return FALSE;
            }
        }, MUI_LANGUAGE_NAME, reinterpret_cast<LONG_PTR>(&installed));
        for (const auto& name : installed) add(name.c_str());
        if (const auto system = FromLangId(::GetUserDefaultUILanguage());
            system && std::find(languages.begin(), languages.end(), *system) == languages.end())
            languages.push_back(*system);
        return languages;
    } catch (...) {
        return {};
    }
}

std::optional<Language> SavedChoice(const std::filesystem::path& settingsFile) noexcept {
    try {
        constexpr std::uint64_t kLimit = 1024 * 1024;
        auto content = fs::ReadFile(settingsFile, kLimit);
        if (!content) return std::nullopt;
        json::ParseOptions options;
        options.maxInputBytes = kLimit;
        options.maxDepth = 16;
        auto parsed = json::Parse(*content, options);
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

std::filesystem::path PackPath(Language language, const std::filesystem::path& languagesDir) {
    const auto& info = Info(language);
    if (info.packFile.empty()) return {};
    return languagesDir / text::Widen(info.packFile);
}

bool PackInstalled(Language language, const std::filesystem::path& languagesDir) noexcept {
    try {
        const auto path = PackPath(language, languagesDir);
        if (path.empty()) return false;
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        return !error && size == Info(language).packBytes;
    } catch (...) {
        return false;
    }
}

Result<void> UsePack(Language language, std::string_view bytes) {
    const auto& info = Info(language);
    if (!Available(language) || language == Language::English) return MakeError(ErrorCode::Unavailable, "no language pack in this version");
    if (bytes.size() != info.packBytes) return MakeError(ErrorCode::Integrity, "language pack size differs from this version's");
    const auto digest = Sha256Hex(bytes);
    if (!digest) return MakeError(ErrorCode::Platform, "SHA-256 unavailable");
    if (*digest != info.packSha256) return MakeError(ErrorCode::Integrity, "language pack differs from this version's");
    auto pack = ParsePack(language, bytes);
    if (!pack) return pack.error();
    Install(language, std::move(*pack));
    return Ok();
}

Result<void> LoadInstalledPack(Language language, const std::filesystem::path& languagesDir) {
    const auto path = PackPath(language, languagesDir);
    if (path.empty()) return MakeError(ErrorCode::Unavailable, "no language pack in this version");
    auto bytes = fs::ReadFile(path, kMaxPackBytes);
    if (!bytes) return bytes.error();
    return UsePack(language, *bytes);
}

Result<void> UsePackForTests(Language language, std::string_view bytes) {
    if (language == Language::English) return MakeError(ErrorCode::InvalidArgument, "English is built in");
    auto pack = ParsePack(language, bytes);
    if (!pack) return pack.error();
    Install(language, std::move(*pack));
    return Ok();
}

bool HasTexts(Language language) noexcept {
    if (language == Language::English) return true;
    if (gPacks[IndexOf(language)].load(std::memory_order_acquire)) return true;
    const BuiltIn* builtIn = gBuiltIn.load(std::memory_order_acquire);
    if (!builtIn || builtIn->rows.empty()) return false;
    return !builtIn->rows.begin()->second->text[IndexOf(language)].empty();
}

void UseBuiltInTexts(std::span<const BuiltInText> rows) noexcept {
    try {
        auto builtIn = std::make_unique<BuiltIn>();
        builtIn->rows.reserve(rows.size());
        for (const auto& row : rows)
            if (!row.text[0].empty()) builtIn->rows.emplace(row.text[0], &row);
        static std::unique_ptr<BuiltIn> kept;   // one table for the process
        std::lock_guard lock(gPacksMutex);
        kept = std::move(builtIn);
        gBuiltIn.store(kept.get(), std::memory_order_release);
        gRevision.fetch_add(1);
    } catch (...) {
    }
}

StartLanguage StartIn(const std::filesystem::path& settingsFile, const std::filesystem::path& languagesDir) noexcept {
    StartLanguage start;
    const auto saved = SavedChoice(settingsFile);
    start.chosen = saved.has_value();
    start.wanted = saved.value_or(SystemLanguage());
    if (start.wanted != Language::English && !HasTexts(start.wanted) && !languagesDir.empty() && PackInstalled(start.wanted, languagesDir))
        (void)LoadInstalledPack(start.wanted, languagesDir);
    start.shown = HasTexts(start.wanted) ? start.wanted : Language::English;
    SetLanguage(start.shown, start.chosen && start.shown == start.wanted);
    return start;
}

void SetLanguage(Language language, bool chosen) noexcept {
    if (static_cast<std::size_t>(language) >= kLanguageCount) language = Language::English;
    const bool before = gChosen.exchange(chosen);
    if (gLanguage.exchange(language) != language || before != chosen) gRevision.fetch_add(1);
}

std::wstring_view FormattingLocale() noexcept {
    if (!gChosen.load()) return {};
    return Info(gLanguage.load()).formattingLocale;
}

Language CurrentLanguage() noexcept { return gLanguage.load(); }
std::uint32_t Revision() noexcept { return gRevision.load(); }

std::wstring_view Tr(std::wstring_view english, Language language) noexcept {
    if (language == Language::English || english.empty()) return english;
    if (const auto translated = FromPack(english, language)) return *translated;
    if (const auto translated = FromBuiltIn(english, language)) return *translated;
    return english;
}

std::wstring_view Tr(std::wstring_view english) noexcept { return Tr(english, CurrentLanguage()); }

std::array<std::wstring_view, kLanguageCount> AllTranslations(std::wstring_view english) noexcept {
    std::array<std::wstring_view, kLanguageCount> all{};
    all[0] = english;
    for (std::size_t i = 1; i < kLanguageCount; ++i) {
        const auto language = static_cast<Language>(i);
        if (const auto translated = FromPack(english, language)) all[i] = *translated;
        else if (const auto builtIn = FromBuiltIn(english, language)) all[i] = *builtIn;
    }
    return all;
}

namespace {
// The replacement fields of a std::format pattern ("{{" and "}}" are literal braces).
std::size_t ReplacementFields(std::wstring_view pattern) noexcept {
    std::size_t fields = 0;
    for (std::size_t at = 0; at < pattern.size(); ++at) {
        if (pattern[at] != L'{') continue;
        if (at + 1 < pattern.size() && pattern[at + 1] == L'{') { ++at; continue; }
        ++fields;
        const auto close = pattern.find(L'}', at);
        if (close == std::wstring_view::npos) break;
        at = close;
    }
    return fields;
}
}  // namespace

std::wstring FormatPattern(std::wstring_view english, std::wformat_args arguments) {
    const auto pattern = Tr(english);
    // A translation that lost or gained a field would drop or refuse a value: the English pattern is shown instead.
    if (pattern.data() != english.data() && ReplacementFields(pattern) == ReplacementFields(english)) {
        try {
            return std::vformat(pattern, arguments);
        } catch (...) {
        }
    }
    try {
        return std::vformat(english, arguments);
    } catch (...) {
        return std::wstring(english);
    }
}

}  // namespace aether::i18n
