// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/KnownApplications.h"

#include "applications/ApplicationIdentity.h"
#include "core/Strings.h"
#include "platform/windows/Win32Error.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <optional>

namespace aether::apps {
namespace {

// "<windowsDirectory>\explorer.exe", validated and lexically normalized; nullopt when unusable.
std::optional<std::wstring> CanonicalExplorerPath(std::wstring_view windowsDirectory) {
    auto directory = text::Trim(windowsDirectory);
    while (!directory.empty() && (directory.back() == L'\\' || directory.back() == L'/')) directory.remove_suffix(1);
    if (directory.empty()) return std::nullopt;
    auto path = NormalizeExecutablePath(std::wstring(directory) + L"\\explorer.exe");
    if (!path) return std::nullopt;
    return std::move(path).value();
}

bool IsExplorerAumid(std::wstring_view aumid) noexcept {
    return text::EqualsIgnoreCase(text::Trim(aumid), kFileExplorerAumid);
}

void ApplyCanonicalIdentity(ApplicationInfo& app, std::optional<std::wstring> executablePath) {
    app.id = FileExplorerId();
    app.appUserModelId = std::wstring(kFileExplorerAumid);
    app.parsingName = std::wstring(kFileExplorerAumid);
    app.arguments.clear();
    if (executablePath) app.executablePath = std::move(*executablePath);
    app.kind = app.executablePath.empty() ? ApplicationKind::Other : ApplicationKind::Desktop;
    if (text::Trim(app.displayName).empty()) app.displayName = std::wstring(kFileExplorerDefaultName);
}

bool IsExistingRegularFile(const std::wstring& path) noexcept {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) == 0;
}

}  // namespace

std::wstring FileExplorerId() { return std::wstring(kAumidIdPrefix) + text::ToLower(kFileExplorerAumid); }

Result<std::wstring> SystemWindowsDirectory() {
    std::array<wchar_t, MAX_PATH + 1> buffer{};
    const UINT length = ::GetSystemWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (length == 0) return platform::LastWin32Error("reading the Windows directory");
    if (length >= buffer.size()) {
        std::wstring large(length, L'\0');
        const UINT written = ::GetSystemWindowsDirectoryW(large.data(), length);
        if (written == 0 || written >= length) {
            return MakeError(ErrorCode::Platform, "the Windows directory could not be read");
        }
        large.resize(written);
        return large;
    }
    return std::wstring(buffer.data(), length);
}

bool IsFileExplorerExecutable(std::wstring_view executablePath, std::wstring_view windowsDirectory) {
    if (text::Trim(executablePath).empty()) return false;
    const auto canonical = CanonicalExplorerPath(windowsDirectory);
    if (!canonical) return false;
    PathPolicy policy;
    policy.allowUnc = false;
    const auto path = NormalizeExecutablePath(executablePath, policy);
    return path && text::EqualsIgnoreCase(path.value(), *canonical);
}

bool IsFileExplorer(const ApplicationInfo& app, std::wstring_view windowsDirectory) {
    if (!text::Trim(app.arguments).empty()) return false;
    const auto aumid = text::Trim(app.appUserModelId);
    if (!aumid.empty()) return IsExplorerAumid(aumid);
    return IsFileExplorerExecutable(app.executablePath, windowsDirectory);
}

bool CanonicalizeFileExplorer(ApplicationInfo& app, std::wstring_view windowsDirectory) {
    if (!IsFileExplorer(app, windowsDirectory)) return false;
    ApplyCanonicalIdentity(app, CanonicalExplorerPath(windowsDirectory));
    return true;
}

bool CanonicalizeFileExplorerWindow(WindowIdentity& identity, std::wstring_view windowsDirectory) {
    if (!text::Trim(identity.appUserModelId).empty()) return false;
    if (!IsFileExplorerExecutable(identity.executablePath, windowsDirectory)) return false;
    identity.id = FileExplorerId();
    identity.appUserModelId = std::wstring(kFileExplorerAumid);
    return true;
}

Result<ApplicationInfo> MakeFileExplorerApplication(const std::vector<ApplicationInfo>& catalog,
                                                    std::wstring windowsDirectory) {
    try {
        if (windowsDirectory.empty()) {
            auto system = SystemWindowsDirectory();
            if (!system) return system.error();
            windowsDirectory = std::move(system).value();
        }
        const auto found = std::find_if(catalog.begin(), catalog.end(), [](const ApplicationInfo& app) {
            return IsExplorerAumid(app.appUserModelId) && text::Trim(app.arguments).empty();
        });
        auto executable = CanonicalExplorerPath(windowsDirectory);
        if (executable && !IsExistingRegularFile(*executable)) executable.reset();
        if (found == catalog.end() && !executable) {
            return MakeError(ErrorCode::NotFound, "File Explorer was not found");
        }
        ApplicationInfo app;
        if (found != catalog.end()) {
            app = *found;
            // A catalog path that is not the system's explorer.exe would group unrelated windows.
            if (!executable && !IsFileExplorerExecutable(app.executablePath, windowsDirectory)) app.executablePath.clear();
        } else {
            app.displayName = std::wstring(kFileExplorerDefaultName);
        }
        ApplyCanonicalIdentity(app, std::move(executable));
        return app;
    } catch (...) {
        return MakeError(ErrorCode::Internal, "the File Explorer entry could not be built");
    }
}

}  // namespace aether::apps
