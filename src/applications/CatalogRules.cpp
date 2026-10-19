// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/CatalogRules.h"

#include "applications/ApplicationIdentity.h"
#include "core/Strings.h"

#include <windows.h>

#include <algorithm>
#include <initializer_list>
#include <cstddef>
#include <optional>

namespace aether::apps {
namespace {

bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix) noexcept {
    return text.size() >= prefix.size() && text::EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

bool EndsWithIgnoreCase(std::wstring_view text, std::wstring_view suffix) noexcept {
    return text.size() >= suffix.size() && text::EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}

std::wstring_view FileNameOf(std::wstring_view path) noexcept {
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring_view::npos ? path : path.substr(separator + 1);
}

std::wstring_view ExtensionOf(std::wstring_view path) noexcept {
    const auto name = FileNameOf(path);
    const auto dot = name.find_last_of(L'.');
    return dot == std::wstring_view::npos ? std::wstring_view{} : name.substr(dot);
}

std::wstring_view StemOf(std::wstring_view path) noexcept {
    const auto name = FileNameOf(path);
    const auto dot = name.find_last_of(L'.');
    return dot == std::wstring_view::npos ? name : name.substr(0, dot);
}

bool IsOneOf(std::wstring_view value, std::initializer_list<std::wstring_view> candidates) noexcept {
    return std::any_of(candidates.begin(), candidates.end(),
                       [value](std::wstring_view candidate) { return text::EqualsIgnoreCase(value, candidate); });
}

// Documents that are help or release notes rather than applications.
bool IsHelpDocument(std::wstring_view extension) noexcept {
    return IsOneOf(extension, {L".chm", L".hlp", L".htm", L".html", L".mht", L".pdf", L".txt", L".rtf", L".md",
                               L".doc", L".docx", L".xps"});
}

CatalogClassification Skip(CatalogDecision decision) { return CatalogClassification{decision, {}}; }

CatalogClassification Finish(ApplicationInfo app) {
    auto id = MakeApplicationId(app);
    if (!id) return Skip(CatalogDecision::SkipInvalid);
    app.id = std::move(id).value();
    return CatalogClassification{CatalogDecision::Include, std::move(app)};
}

int PreferenceScore(const ApplicationInfo& app) noexcept {
    const bool hasAumid = !app.appUserModelId.empty();
    const bool hasExe = !app.executablePath.empty();
    int score = 0;
    if (hasAumid && hasExe) {
        score = 8;
    } else if (hasAumid || hasExe) {
        score = 4;
    }
    if (!app.parsingName.empty()) score += 2;
    if (!app.shortcutPath.empty()) score += 1;
    return score;
}

void FillMissing(std::wstring& target, const std::wstring& source) {
    if (target.empty() && !source.empty()) target = source;
}

void AppendKeywords(std::wstring& target, const std::wstring& source) {
    if (source.empty()) return;
    if (target.empty()) {
        target = source;
        return;
    }
    // Keywords are a space-separated bag of words; avoid repeating a whole phrase.
    if (text::ToLower(target).find(text::ToLower(source)) == std::wstring::npos) target += L" " + source;
}

void Absorb(ApplicationInfo& primary, const ApplicationInfo& other) {
    FillMissing(primary.displayName, other.displayName);
    FillMissing(primary.parsingName, other.parsingName);
    FillMissing(primary.appUserModelId, other.appUserModelId);
    if (primary.executablePath.empty() && !other.executablePath.empty()) {
        primary.executablePath = other.executablePath;
        // Arguments belong to the executable they were defined with.
        if (primary.arguments.empty()) primary.arguments = other.arguments;
    }
    FillMissing(primary.shortcutPath, other.shortcutPath);
    AppendKeywords(primary.keywords, other.keywords);
    if (other.kind == ApplicationKind::Packaged) primary.kind = ApplicationKind::Packaged;
}

int CompareDisplayNames(const std::wstring& a, const std::wstring& b) noexcept {
    const int result = ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
                                         a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                         static_cast<int>(b.size()), nullptr, nullptr, 0);
    if (result == 0) {
        // Locale comparison unavailable: fall back to an ordinal, case-insensitive comparison.
        const auto la = text::ToLower(a);
        const auto lb = text::ToLower(b);
        return la < lb ? -1 : (la == lb ? 0 : 1);
    }
    return result - CSTR_EQUAL;
}

}  // namespace

bool IsUninstallerName(std::wstring_view displayName) noexcept {
    return StartsWithIgnoreCase(text::Trim(displayName), L"Uninstall");
}

bool IsUninstallerExecutable(std::wstring_view executablePath) noexcept {
    const auto name = FileNameOf(text::Trim(executablePath));
    if (!EndsWithIgnoreCase(name, L".exe")) return false;
    return StartsWithIgnoreCase(name, L"unins") || text::EqualsIgnoreCase(name, L"msiexec.exe");
}

bool IsHelpViewerExecutable(std::wstring_view executablePath) noexcept {
    const auto name = FileNameOf(text::Trim(executablePath));
    return IsOneOf(name, {L"hh.exe", L"winhlp32.exe"});
}

bool IsUrlTarget(std::wstring_view target) noexcept {
    const auto trimmed = text::Trim(target);
    const auto colon = trimmed.find(L':');
    // One letter before ':' is a drive ("C:\..."); a URL scheme has at least two characters.
    if (colon == std::wstring_view::npos || colon < 2) return false;
    const auto scheme = trimmed.substr(0, colon);
    const auto isSchemeChar = [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'+' ||
               c == L'-' || c == L'.';
    };
    const bool letterFirst = (scheme[0] >= L'a' && scheme[0] <= L'z') || (scheme[0] >= L'A' && scheme[0] <= L'Z');
    return letterFirst && std::all_of(scheme.begin(), scheme.end(), isSchemeChar);
}

bool IsShellNamespaceTarget(std::wstring_view target) noexcept {
    return text::Trim(target).starts_with(L"::{");
}

std::optional<std::wstring> AppsFolderChildName(std::wstring_view absoluteParsingName) {
    for (const std::wstring_view prefix : {kAppsFolderParsingPrefix, std::wstring_view(L"shell:AppsFolder\\")}) {
        if (StartsWithIgnoreCase(absoluteParsingName, prefix)) {
            const auto child = absoluteParsingName.substr(prefix.size());
            if (child.empty()) return std::nullopt;
            return std::wstring(child);
        }
    }
    return std::nullopt;
}

CatalogClassification ClassifyDiscoveredItem(const DiscoveredItem& item) {
    const auto displayName = text::Trim(item.displayName);
    if (displayName.empty()) return Skip(CatalogDecision::SkipInvalid);
    if (IsUninstallerName(displayName)) return Skip(CatalogDecision::SkipUninstaller);

    const auto aumid = text::Trim(item.appUserModelId);
    const auto parsingName = text::Trim(item.parsingName);
    const auto target = text::Trim(item.targetPath);
    const bool fromAppsFolder = !parsingName.empty();

    ApplicationInfo app;
    app.displayName = std::wstring(displayName);
    app.parsingName = std::wstring(parsingName);
    if (IsPackagedAumid(aumid) || IsExplicitAumid(aumid)) app.appUserModelId = std::wstring(aumid);
    if (!item.shortcutPath.empty()) {
        if (auto shortcut = NormalizeShortcutPath(item.shortcutPath)) app.shortcutPath = std::move(shortcut).value();
    }

    // Packaged applications are launched through their AUMID; their target (if any) is irrelevant.
    if (IsPackagedAumid(aumid) || (item.hasPackageIdentity && !aumid.empty())) {
        app.kind = ApplicationKind::Packaged;
        app.appUserModelId = std::wstring(aumid);
        if (app.parsingName.empty()) app.parsingName = app.appUserModelId;
        return Finish(std::move(app));
    }

    if (target.empty()) {
        // Apps-folder items without a target are launchable by name only when the shell gave them an
        // explicit identity (otherwise there is nothing to identify or launch).
        if (fromAppsFolder && !app.appUserModelId.empty()) {
            app.kind = ApplicationKind::Other;
            return Finish(std::move(app));
        }
        return Skip(CatalogDecision::SkipInvalid);
    }
    if (IsShellNamespaceTarget(target)) {
        if (!fromAppsFolder) return Skip(CatalogDecision::SkipInvalid);
        app.kind = ApplicationKind::Other;
        return Finish(std::move(app));
    }
    if (IsUrlTarget(target)) return Skip(CatalogDecision::SkipUrl);

    const auto extension = ExtensionOf(target);
    if (text::EqualsIgnoreCase(extension, L".url")) return Skip(CatalogDecision::SkipUrl);
    if (!text::EqualsIgnoreCase(extension, L".exe")) {
        return Skip(IsHelpDocument(extension) ? CatalogDecision::SkipHelp : CatalogDecision::SkipDocument);
    }
    if (IsUninstallerExecutable(target)) return Skip(CatalogDecision::SkipUninstaller);
    if (IsHelpViewerExecutable(target)) return Skip(CatalogDecision::SkipHelp);

    auto executable = NormalizeExecutablePath(target);
    if (!executable) return Skip(CatalogDecision::SkipInvalid);
    app.kind = ApplicationKind::Desktop;
    app.executablePath = std::move(executable).value();
    app.arguments = std::wstring(text::Trim(item.arguments));
    // The executable's file name helps search ("cmd", "msedge", "code").
    const auto stem = StemOf(app.executablePath);
    if (!text::EqualsIgnoreCase(stem, displayName)) app.keywords = std::wstring(stem);
    return Finish(std::move(app));
}

std::vector<ApplicationInfo> MergeDuplicateApplications(std::vector<ApplicationInfo> entries) {
    std::vector<ApplicationInfo> merged;
    merged.reserve(entries.size());
    std::vector<std::vector<std::size_t>> groups;  // indices into `entries`, per merged entry
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto existing = std::find_if(merged.begin(), merged.end(), [&](const ApplicationInfo& candidate) {
            return text::EqualsIgnoreCase(candidate.id, entries[i].id);
        });
        if (existing == merged.end()) {
            merged.push_back(ApplicationInfo{entries[i].id});
            groups.push_back({i});
        } else {
            groups[static_cast<std::size_t>(existing - merged.begin())].push_back(i);
        }
    }
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const auto& members = groups[g];
        std::size_t primary = members.front();
        for (const auto index : members) {
            if (PreferenceScore(entries[index]) > PreferenceScore(entries[primary])) primary = index;
        }
        ApplicationInfo result = std::move(entries[primary]);
        for (const auto index : members) {
            if (index != primary) Absorb(result, entries[index]);
        }
        merged[g] = std::move(result);
    }
    return merged;
}

std::vector<ApplicationInfo> CombineCatalog(std::vector<ApplicationInfo> appsFolderEntries,
                                            std::vector<ApplicationInfo> shortcutEntries) {
    auto combined = MergeDuplicateApplications(std::move(appsFolderEntries));
    const std::size_t appsFolderCount = combined.size();

    const auto enrich = [](ApplicationInfo& target, const ApplicationInfo& shortcut) {
        FillMissing(target.shortcutPath, shortcut.shortcutPath);
        FillMissing(target.appUserModelId, shortcut.appUserModelId);
        if (target.executablePath.empty() && !shortcut.executablePath.empty()) {
            target.executablePath = shortcut.executablePath;
            if (target.arguments.empty()) target.arguments = shortcut.arguments;
        }
        AppendKeywords(target.keywords, shortcut.keywords);
    };

    for (auto& shortcut : shortcutEntries) {
        std::optional<std::size_t> match;
        if (!shortcut.appUserModelId.empty()) {
            for (std::size_t i = 0; i < appsFolderCount && !match; ++i) {
                if (text::EqualsIgnoreCase(combined[i].appUserModelId, shortcut.appUserModelId)) match = i;
            }
        } else if (!shortcut.executablePath.empty()) {
            std::vector<std::size_t> candidates;
            for (std::size_t i = 0; i < appsFolderCount; ++i) {
                const auto& entry = combined[i];
                if (text::EqualsIgnoreCase(entry.executablePath, shortcut.executablePath) &&
                    text::Trim(entry.arguments) == text::Trim(shortcut.arguments)) {
                    candidates.push_back(i);
                }
            }
            for (const auto index : candidates) {
                if (text::EqualsIgnoreCase(combined[index].displayName, shortcut.displayName)) {
                    match = index;
                    break;
                }
            }
            if (!match) {
                for (const auto index : candidates) {
                    if (combined[index].shortcutPath.empty()) {
                        match = index;
                        break;
                    }
                }
            }
        }
        if (match) {
            enrich(combined[*match], shortcut);
        } else {
            combined.push_back(std::move(shortcut));
        }
    }

    auto result = MergeDuplicateApplications(std::move(combined));
    std::stable_sort(result.begin(), result.end(), [](const ApplicationInfo& a, const ApplicationInfo& b) {
        const int order = CompareDisplayNames(a.displayName, b.displayName);
        if (order != 0) return order < 0;
        return a.id < b.id;
    });
    return result;
}

}  // namespace aether::apps
