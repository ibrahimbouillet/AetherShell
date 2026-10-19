// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/RunningPresentation.h"

#include "applications/ApplicationIdentity.h"
#include "applications/IconResource.h"
#include "core/Strings.h"

#include <array>
#include <cstdint>
#include <optional>

namespace aether::apps {
namespace {

// Lexical key of an executable: validated, normalized and lower-cased; never touches the disk.
// UNC paths are accepted for comparison only (the tracker reports them for network executables).
std::optional<std::wstring> ExecutableKey(std::wstring_view path) {
    if (text::Trim(path).empty()) return std::nullopt;
    PathPolicy policy;
    policy.allowUnc = true;
    auto normalized = NormalizeExecutablePath(path, policy);
    if (!normalized) return std::nullopt;
    return text::ToLower(normalized.value());
}

std::wstring_view UsableAumid(std::wstring_view aumid) noexcept {
    const auto trimmed = text::Trim(aumid);
    if (trimmed.empty()) return {};
    return IsPackagedAumid(trimmed) || IsExplicitAumid(trimmed) ? trimmed : std::wstring_view{};
}

ApplicationKind KindFromRunningFacts(std::wstring_view aumid, const std::wstring& executablePath) noexcept {
    if (IsPackagedAumid(aumid)) return ApplicationKind::Packaged;
    return executablePath.empty() ? ApplicationKind::Other : ApplicationKind::Desktop;
}

void Hash(std::uint64_t& state, std::wstring_view value) {
    const auto folded = text::ToLower(text::Trim(value));
    for (const wchar_t unit : folded) {
        state ^= static_cast<std::uint64_t>(unit);
        state *= 1099511628211ull;
    }
    // Field separator: a non-character, so ("ab", "c") and ("a", "bc") differ.
    state ^= 0xFFFFu;
    state *= 1099511628211ull;
}

}  // namespace

ApplicationIndex::ApplicationIndex(std::vector<ApplicationInfo> catalog) : entries_(std::move(catalog)) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        if (const auto aumid = UsableAumid(entry.appUserModelId); !aumid.empty()) {
            byAumid_.try_emplace(text::ToLower(aumid), i);
        }
        if (auto key = ExecutableKey(entry.executablePath)) byExecutable_[std::move(*key)].push_back(i);
    }
}

const ApplicationInfo* ApplicationIndex::FindByAumid(std::wstring_view aumid) const {
    const auto usable = UsableAumid(aumid);
    if (usable.empty()) return nullptr;
    const auto found = byAumid_.find(text::ToLower(usable));
    return found == byAumid_.end() ? nullptr : &entries_[found->second];
}

const ApplicationInfo* ApplicationIndex::FindByExecutable(std::wstring_view executablePath) const {
    const auto key = ExecutableKey(executablePath);
    if (!key) return nullptr;
    const auto found = byExecutable_.find(*key);
    if (found == byExecutable_.end()) return nullptr;
    const ApplicationInfo* match = nullptr;
    for (const auto index : found->second) {
        const auto& entry = entries_[index];
        if (!text::Trim(entry.arguments).empty()) continue;
        if (match) return nullptr;  // ambiguous: never guess
        match = &entry;
    }
    return match;
}

bool ApplicationIndex::HasExecutableWithOtherAumid(std::wstring_view executablePath, std::wstring_view aumid) const {
    const auto key = ExecutableKey(executablePath);
    if (!key) return false;
    const auto found = byExecutable_.find(*key);
    if (found == byExecutable_.end()) return false;
    const auto own = text::Trim(aumid);
    for (const auto index : found->second) {
        const auto other = text::Trim(entries_[index].appUserModelId);
        if (!other.empty() && !text::EqualsIgnoreCase(other, own)) return true;
    }
    return false;
}

ApplicationInfo ResolveRunningPresentation(const RunningApplication& running, const ApplicationIndex& index) {
    ApplicationInfo result;
    result.id = running.id;
    const auto aumid = UsableAumid(running.appUserModelId);
    const ApplicationInfo* match = nullptr;
    if (!aumid.empty()) {
        match = index.FindByAumid(aumid);                         // rule (a)
    } else if (!running.executablePath.empty()) {
        match = index.FindByExecutable(running.executablePath);  // rule (b)
    }

    if (match) {
        result.kind = match->kind;
        result.parsingName = match->parsingName;
        result.appUserModelId = match->appUserModelId;
        result.shortcutPath = match->shortcutPath;
        result.arguments = match->arguments;
        result.keywords = match->keywords;
        result.iconResource = match->iconResource;
        // Arguments belong to the executable they were defined with.
        if (!match->executablePath.empty()) {
            result.executablePath = match->executablePath;
        } else if (text::Trim(match->arguments).empty()) {
            result.executablePath = running.executablePath;
        }
    } else {
        // Rule (c): running facts only.
        result.appUserModelId = std::wstring(aumid);
        result.executablePath = running.executablePath;
        result.kind = KindFromRunningFacts(aumid, result.executablePath);
        if (IsPackagedAumid(aumid)) result.parsingName = std::wstring(aumid);
        if (!aumid.empty() && !running.relaunchIconResource.empty() &&
            ParseIconResource(running.relaunchIconResource)) {
            result.iconResource = running.relaunchIconResource;
        }
        // Rule (d): another profile of the same executable must never be started by a bare launch.
        if (!aumid.empty() && !result.executablePath.empty() &&
            index.HasExecutableWithOtherAumid(result.executablePath, aumid)) {
            result.executablePath.clear();
            if (result.kind == ApplicationKind::Desktop) result.kind = ApplicationKind::Other;
        }
    }

    result.displayName = !text::Trim(running.displayName).empty() ? running.displayName
                         : match                                 ? match->displayName
                                                                 : std::wstring{};
    return result;
}

bool HasLaunchRoute(const ApplicationInfo& app) noexcept {
    try {
        if (!text::Trim(app.parsingName).empty() && ValidateParsingName(app.parsingName)) return true;
        if (IsPackagedAumid(text::Trim(app.appUserModelId))) return true;
        if (!app.shortcutPath.empty() && NormalizeShortcutPath(app.shortcutPath)) return true;
        return !app.executablePath.empty() && text::Trim(app.arguments).empty() &&
               NormalizeExecutablePath(app.executablePath).ok();
    } catch (...) {
        return false;
    }
}

std::wstring IconSourceFingerprint(const ApplicationInfo& app) {
    std::uint64_t state = 14695981039346656037ull;  // FNV-1a 64 offset basis
    Hash(state, std::to_wstring(static_cast<int>(app.kind)));
    Hash(state, app.parsingName);
    Hash(state, app.appUserModelId);
    Hash(state, app.shortcutPath);
    Hash(state, app.executablePath);
    Hash(state, app.iconResource);
    constexpr std::array<wchar_t, 16> digits{L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7',
                                             L'8', L'9', L'a', L'b', L'c', L'd', L'e', L'f'};
    std::wstring result(16, L'0');
    for (std::size_t i = 0; i < 16; ++i) {
        result[15 - i] = digits[static_cast<std::size_t>(state & 0xFu)];
        state >>= 4;
    }
    return result;
}

}  // namespace aether::apps
