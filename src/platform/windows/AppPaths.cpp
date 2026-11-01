// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/AppPaths.h"

#include "core/FileSystem.h"
#include "platform/windows/Win32Error.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <climits>

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

Result<std::filesystem::path> DefaultDataRoot() {
    auto local = KnownFolder(FOLDERID_LocalAppData);
    if (!local) return local.error();
    return local.value() / L"AetherShell";
}

bool SameDataRoot(const std::filesystem::path& first, const std::filesystem::path& second) noexcept {
    try {
        if (!first.is_absolute() || !second.is_absolute()) return false;
        auto normalize = [](const std::filesystem::path& value) {
            auto text = value.lexically_normal().native();
            while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
            return text;
        };
        const auto a = normalize(first), b = normalize(second);
        if (a.size() > INT_MAX || b.size() > INT_MAX) return false;
        return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    } catch (...) { return false; }
}

Result<AppPaths> AppPaths::Resolve(const std::optional<std::filesystem::path>& overrideRoot) {
    AppPaths paths;
    if (overrideRoot && !overrideRoot->empty()) {
        paths.dataRoot = std::filesystem::absolute(*overrideRoot);
    } else {
        auto root = DefaultDataRoot();
        if (!root) return root.error();
        paths.dataRoot = std::move(*root);
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
