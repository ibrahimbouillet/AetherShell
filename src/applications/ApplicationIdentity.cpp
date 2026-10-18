// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/ApplicationIdentity.h"

#include "core/Strings.h"

#include <windows.h>

#include <algorithm>

namespace aether::apps {
namespace {

constexpr std::size_t kMaxPathLength = 32767;  // longest Win32 path (with the \\?\ prefix)
constexpr std::size_t kMaxNameLength = 1024;   // AUMIDs, parsing names and class names
constexpr std::size_t kMaxExplicitAumidLength = 128;

bool HasControlCharacters(std::wstring_view text) noexcept {
    return std::any_of(text.begin(), text.end(), [](wchar_t c) { return c < 0x20 || c == 0x7F; });
}

bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix) noexcept {
    return text.size() >= prefix.size() && text::EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

bool EndsWithIgnoreCase(std::wstring_view text, std::wstring_view suffix) noexcept {
    return text.size() >= suffix.size() && text::EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
}

bool IsAsciiLetter(wchar_t c) noexcept { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool IsAsciiDigit(wchar_t c) noexcept { return c >= L'0' && c <= L'9'; }
bool IsAsciiAlphanumeric(wchar_t c) noexcept { return IsAsciiLetter(c) || IsAsciiDigit(c); }

bool IsDriveAbsolute(std::wstring_view path) noexcept {
    return path.size() >= 3 && IsAsciiLetter(path[0]) && path[1] == L':' && path[2] == L'\\';
}

// "\\server\share\rest": both the server and the share must be non-empty.
bool IsWellFormedUnc(std::wstring_view path) noexcept {
    if (path.size() < 5 || path[0] != L'\\' || path[1] != L'\\') return false;
    const auto serverEnd = path.find(L'\\', 2);
    if (serverEnd == std::wstring_view::npos || serverEnd == 2) return false;
    const auto shareEnd = path.find(L'\\', serverEnd + 1);
    return shareEnd != std::wstring_view::npos && shareEnd > serverEnd + 1 && shareEnd + 1 < path.size();
}

Error InvalidPath(std::string message) { return MakeError(ErrorCode::InvalidArgument, std::move(message)); }

Result<std::wstring> ValidateName(std::wstring_view value, const char* what) {
    const auto trimmed = text::Trim(value);
    if (trimmed.empty()) return MakeError(ErrorCode::InvalidArgument, std::string(what) + " is empty");
    if (trimmed.size() > kMaxNameLength) {
        return MakeError(ErrorCode::InvalidArgument, std::string(what) + " is too long");
    }
    if (HasControlCharacters(trimmed)) {
        return MakeError(ErrorCode::InvalidArgument, std::string(what) + " contains control characters");
    }
    return std::wstring(trimmed);
}

Result<std::wstring> MakePrefixedId(std::wstring_view prefix, std::wstring_view value, const char* what) {
    auto valid = ValidateName(value, what);
    if (!valid) return valid.error();
    return std::wstring(prefix) + text::ToLower(valid.value());
}

std::wstring_view FileNameOf(std::wstring_view path) noexcept {
    const auto slash = path.find_last_of(L'\\');
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

}  // namespace

Result<std::wstring> MakeIdForAumid(std::wstring_view aumid) {
    return MakePrefixedId(kAumidIdPrefix, aumid, "AppUserModelID");
}

Result<std::wstring> MakeIdForParsingName(std::wstring_view parsingName) {
    return MakePrefixedId(kShellIdPrefix, parsingName, "parsing name");
}

Result<std::wstring> MakeIdForWindowClass(std::wstring_view className) {
    return MakePrefixedId(kWindowIdPrefix, className, "window class");
}

Result<std::wstring> MakeIdForExecutable(std::wstring_view path, const PathPolicy& policy) {
    auto normalized = NormalizeExecutablePath(path, policy);
    if (!normalized) return normalized.error();
    std::wstring full = std::move(normalized).value();
    // 8.3 short names ("C:\PROGRA~1\...") would give the same file two identities. Expanding them
    // needs the file system (and the file must exist), so this is best effort and only attempted when
    // the path can contain a short name at all. Never on UNC paths: GetLongPathNameW would then go to
    // the network, and the window tracker computes ids on the UI thread (it is the only caller that
    // allows UNC paths).
    const bool unc = full.size() >= 2 && full[0] == L'\\' && full[1] == L'\\';
    if (!unc && full.find(L'~') != std::wstring::npos) {
        const DWORD needed = ::GetLongPathNameW(full.c_str(), nullptr, 0);
        if (needed > 0) {
            std::wstring expanded(needed, L'\0');
            const DWORD written = ::GetLongPathNameW(full.c_str(), expanded.data(), needed);
            if (written > 0 && written < needed) {
                expanded.resize(written);
                full = std::move(expanded);
            }
        }
    }
    return std::wstring(kExecutableIdPrefix) + text::ToLower(full);
}

IdScheme GetIdScheme(std::wstring_view id) noexcept {
    const auto hasValue = [id](std::wstring_view prefix) {
        return id.size() > prefix.size() && id.substr(0, prefix.size()) == prefix;
    };
    if (hasValue(kAumidIdPrefix)) return IdScheme::Aumid;
    if (hasValue(kExecutableIdPrefix)) return IdScheme::Executable;
    if (hasValue(kShellIdPrefix)) return IdScheme::Shell;
    if (hasValue(kWindowIdPrefix)) return IdScheme::Window;
    return IdScheme::Invalid;
}

bool IsPackagedAumid(std::wstring_view aumid) noexcept {
    const auto bang = aumid.find(L'!');
    if (bang == std::wstring_view::npos) return false;
    const auto familyName = aumid.substr(0, bang);
    const auto appId = aumid.substr(bang + 1);
    if (appId.empty() || appId.size() > 64) return false;
    if (!std::all_of(appId.begin(), appId.end(),
                     [](wchar_t c) { return IsAsciiAlphanumeric(c) || c == L'.' || c == L'-' || c == L'_'; })) {
        return false;
    }
    const auto underscore = familyName.find(L'_');
    if (underscore == std::wstring_view::npos) return false;
    const auto name = familyName.substr(0, underscore);
    const auto publisherId = familyName.substr(underscore + 1);
    if (name.size() < 3 || name.size() > 50) return false;
    if (!std::all_of(name.begin(), name.end(),
                     [](wchar_t c) { return IsAsciiAlphanumeric(c) || c == L'.' || c == L'-'; })) {
        return false;
    }
    return publisherId.size() == 13 && std::all_of(publisherId.begin(), publisherId.end(), IsAsciiAlphanumeric);
}

bool IsExplicitAumid(std::wstring_view aumid) noexcept {
    const auto trimmed = text::Trim(aumid);
    if (trimmed.empty() || trimmed.size() > kMaxExplicitAumidLength || HasControlCharacters(trimmed)) return false;
    if (trimmed.front() == L'{') return false;
    // The Apps folder synthesizes these for shortcuts that carry arguments; they identify one .lnk,
    // are never attached to the application's windows and change when the shortcut is recreated.
    if (StartsWithIgnoreCase(trimmed, L"Microsoft.AutoGenerated.")) return false;
    return trimmed.find_first_of(L"\\/:") == std::wstring_view::npos;
}

Result<std::wstring> NormalizeAbsoluteFilePath(std::wstring_view path, std::wstring_view requiredExtension,
                                               const PathPolicy& policy) {
    const auto trimmed = text::Trim(path);
    if (trimmed.empty()) return InvalidPath("path is empty");
    if (trimmed.size() > kMaxPathLength) return InvalidPath("path is too long");
    if (HasControlCharacters(trimmed)) return InvalidPath("path contains control characters");

    std::wstring candidate(trimmed);
    std::replace(candidate.begin(), candidate.end(), L'/', L'\\');

    // Win32 and NT namespace prefixes. Only the long-path forms of ordinary drive and UNC paths are
    // accepted; everything else addresses devices or raw volumes.
    if (StartsWithIgnoreCase(candidate, L"\\??\\")) return InvalidPath("object-manager paths are not allowed");
    if (StartsWithIgnoreCase(candidate, L"\\\\.\\")) return InvalidPath("device paths are not allowed");
    if (StartsWithIgnoreCase(candidate, L"\\\\?\\")) {
        const std::wstring rest = candidate.substr(4);
        if (StartsWithIgnoreCase(rest, L"UNC\\")) {
            candidate = L"\\\\" + rest.substr(4);
        } else if (IsDriveAbsolute(rest)) {
            candidate = rest;
        } else {
            return InvalidPath("device paths are not allowed");
        }
    }

    const bool unc = candidate.size() >= 2 && candidate[0] == L'\\' && candidate[1] == L'\\';
    if (unc) {
        if (!policy.allowUnc) return InvalidPath("network (UNC) paths are not allowed");
        if (!IsWellFormedUnc(candidate)) return InvalidPath("malformed UNC path");
    } else if (!IsDriveAbsolute(candidate)) {
        return InvalidPath("path must be absolute");
    }

    // ':' after the drive letter would name an alternate data stream.
    const std::wstring_view body = std::wstring_view(candidate).substr(2);
    if (body.find_first_of(L"<>\"|?*:") != std::wstring_view::npos) {
        return InvalidPath("path contains reserved characters");
    }

    // Lexical normalization: collapses ".", "..", repeated separators and trailing dots/spaces. For an
    // absolute path GetFullPathNameW does not consult the current directory or the file system.
    const DWORD needed = ::GetFullPathNameW(candidate.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return InvalidPath("path cannot be normalized");
    std::wstring full(needed, L'\0');
    const DWORD written = ::GetFullPathNameW(candidate.c_str(), needed, full.data(), nullptr);
    if (written == 0 || written >= needed) return InvalidPath("path cannot be normalized");
    full.resize(written);

    // Reserved device names ("C:\x\CON") are mapped to \\.\CON by some Windows versions.
    if (StartsWithIgnoreCase(full, L"\\\\.\\") || StartsWithIgnoreCase(full, L"\\\\?\\")) {
        return InvalidPath("device paths are not allowed");
    }
    if (unc ? !IsWellFormedUnc(full) : !IsDriveAbsolute(full)) return InvalidPath("path must be absolute");
    if (full.back() == L'\\') return InvalidPath("path names a directory");

    const auto fileName = FileNameOf(full);
    if (!requiredExtension.empty()) {
        if (!EndsWithIgnoreCase(fileName, requiredExtension) || fileName.size() <= requiredExtension.size()) {
            return InvalidPath("path does not name a " + text::Narrow(requiredExtension) + " file");
        }
    }
    if (!unc && full[0] >= L'a' && full[0] <= L'z') full[0] = static_cast<wchar_t>(full[0] - L'a' + L'A');
    return full;
}

Result<std::wstring> NormalizeExecutablePath(std::wstring_view path, const PathPolicy& policy) {
    return NormalizeAbsoluteFilePath(path, L".exe", policy);
}

Result<std::wstring> NormalizeShortcutPath(std::wstring_view path, const PathPolicy& policy) {
    return NormalizeAbsoluteFilePath(path, L".lnk", policy);
}

Result<std::wstring> ValidateParsingName(std::wstring_view parsingName) {
    return ValidateName(parsingName, "parsing name");
}

Result<std::wstring> MakeApplicationId(const ApplicationInfo& app) {
    const auto aumid = text::Trim(app.appUserModelId);
    if (!aumid.empty() && (IsPackagedAumid(aumid) || IsExplicitAumid(aumid))) {
        if (auto id = MakeIdForAumid(aumid)) return id;
    }
    const bool hasArguments = !text::Trim(app.arguments).empty();
    if (!app.executablePath.empty() && !hasArguments) {
        if (auto id = MakeIdForExecutable(app.executablePath)) return id;
    }
    if (!app.parsingName.empty()) {
        if (auto id = MakeIdForParsingName(app.parsingName)) return id;
    }
    if (!app.shortcutPath.empty()) {
        if (auto shortcut = NormalizeShortcutPath(app.shortcutPath)) {
            if (auto id = MakeIdForParsingName(shortcut.value())) return id;
        }
    }
    if (!app.executablePath.empty() && hasArguments) {
        if (auto id = MakeIdForExecutable(app.executablePath)) return id;
    }
    return MakeError(ErrorCode::InvalidArgument, "application has no usable identity");
}

bool RefersToSameApplication(const ApplicationInfo& app, const RunningApplication& running) {
    if (!app.id.empty() && text::EqualsIgnoreCase(app.id, running.id)) return true;
    const auto appAumid = text::Trim(app.appUserModelId);
    const auto runningAumid = text::Trim(running.appUserModelId);
    if (!appAumid.empty() && !runningAumid.empty()) return text::EqualsIgnoreCase(appAumid, runningAumid);
    return !app.executablePath.empty() && !running.executablePath.empty() &&
           text::EqualsIgnoreCase(app.executablePath, running.executablePath);
}

}  // namespace aether::apps
