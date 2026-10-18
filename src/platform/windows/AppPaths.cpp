// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/AppPaths.h"

#include "core/FileSystem.h"
#include "platform/windows/Win32Error.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace aether::platform {

Result<std::filesystem::path> KnownFolder(const GUID& folderId) {
    PWSTR raw = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(folderId, KF_FLAG_CREATE, nullptr, &raw);
    if (FAILED(hr)) {
        if (raw) ::CoTaskMemFree(raw);
        return ErrorFromHResult(hr, "SHGetKnownFolderPath");
    }
    std::filesystem::path path(raw);
    ::CoTaskMemFree(raw);
    return path;
}

std::filesystem::path CurrentExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        if (buffer.size() >= 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}

Result<AppPaths> AppPaths::Resolve(const std::optional<std::filesystem::path>& overrideRoot) {
    AppPaths paths;
    if (overrideRoot && !overrideRoot->empty()) {
        paths.dataRoot = std::filesystem::absolute(*overrideRoot);
    } else {
        auto local = KnownFolder(FOLDERID_LocalAppData);
        if (!local) return local.error();
        paths.dataRoot = local.value() / L"AetherShell";
    }
    paths.configDir = paths.dataRoot / L"config";
    paths.logDir = paths.dataRoot / L"logs";
    paths.cacheDir = paths.dataRoot / L"cache";
    paths.updatesDir = paths.dataRoot / L"updates";
    for (const auto* dir : {&paths.configDir, &paths.logDir, &paths.cacheDir, &paths.updatesDir}) {
        if (auto created = fs::EnsureDirectory(*dir); !created) return created.error();
    }
    paths.executablePath = CurrentExecutablePath();
    paths.executableDir = paths.executablePath.parent_path();
    return paths;
}

}  // namespace aether::platform
