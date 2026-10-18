// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/FileSystem.h"

#include "core/Strings.h"

#include <windows.h>

#include <atomic>

namespace aether::fs {
namespace {

Error Win32Failure(ErrorCode fallback, std::string context, DWORD code) {
    ErrorCode category = fallback;
    switch (code) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: category = ErrorCode::NotFound; break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION: category = ErrorCode::AccessDenied; break;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: category = ErrorCode::InsufficientSpace; break;
        default: break;
    }
    return MakeError(category, std::move(context), static_cast<std::int64_t>(code));
}

struct FileHandle {
    HANDLE handle = INVALID_HANDLE_VALUE;
    FileHandle() = default;
    explicit FileHandle(HANDLE h) : handle(h) {}
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    ~FileHandle() {
        if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
    }
    bool valid() const { return handle != INVALID_HANDLE_VALUE; }
};

}  // namespace

Result<std::string> ReadFile(const std::filesystem::path& path, std::uint64_t maxBytes) {
    FileHandle file(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file.valid()) return Win32Failure(ErrorCode::Io, "cannot open file", ::GetLastError());

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file.handle, &size)) return Win32Failure(ErrorCode::Io, "cannot query file size", ::GetLastError());
    if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > maxBytes) {
        return MakeError(ErrorCode::Integrity, "file exceeds the allowed size");
    }

    std::string contents(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(contents.size() - offset, 1u << 20));
        DWORD read = 0;
        if (!::ReadFile(file.handle, contents.data() + offset, chunk, &read, nullptr)) {
            return Win32Failure(ErrorCode::Io, "cannot read file", ::GetLastError());
        }
        if (read == 0) break;
        offset += read;
    }
    contents.resize(offset);
    return contents;
}

Result<void> WriteFileAtomic(const std::filesystem::path& path, std::string_view contents) {
    static std::atomic<unsigned> counter{0};
    if (path.has_parent_path()) {
        if (auto created = EnsureDirectory(path.parent_path()); !created) return created.error();
    }
    auto temp = path;
    temp += L".tmp-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);

    {
        FileHandle file(::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.valid()) return Win32Failure(ErrorCode::Io, "cannot create temporary file", ::GetLastError());
        std::size_t offset = 0;
        while (offset < contents.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(contents.size() - offset, 1u << 20));
            DWORD written = 0;
            if (!::WriteFile(file.handle, contents.data() + offset, chunk, &written, nullptr)) {
                const DWORD error = ::GetLastError();
                ::CloseHandle(std::exchange(file.handle, INVALID_HANDLE_VALUE));
                ::DeleteFileW(temp.c_str());
                return Win32Failure(ErrorCode::Io, "cannot write temporary file", error);
            }
            offset += written;
        }
        if (!::FlushFileBuffers(file.handle)) {
            const DWORD error = ::GetLastError();
            ::CloseHandle(std::exchange(file.handle, INVALID_HANDLE_VALUE));
            ::DeleteFileW(temp.c_str());
            return Win32Failure(ErrorCode::Io, "cannot flush temporary file", error);
        }
    }

    if (!::MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = ::GetLastError();
        ::DeleteFileW(temp.c_str());
        return Win32Failure(ErrorCode::Io, "cannot replace destination file", error);
    }
    return Ok();
}

Result<void> EnsureDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec && !std::filesystem::is_directory(path)) {
        return MakeError(ErrorCode::Io, "cannot create directory: " + ec.message(), ec.value());
    }
    return Ok();
}

Result<std::uint64_t> AvailableDiskSpace(const std::filesystem::path& path) {
    ULARGE_INTEGER available{};
    // GetDiskFreeSpaceEx needs an existing directory: walk up from files and missing paths.
    auto probe = path;
    std::error_code ec;
    while (!probe.empty() && !std::filesystem::is_directory(probe, ec)) {
        auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    if (!::GetDiskFreeSpaceExW(probe.c_str(), &available, nullptr, nullptr)) {
        return Win32Failure(ErrorCode::Io, "cannot query free disk space", ::GetLastError());
    }
    return static_cast<std::uint64_t>(available.QuadPart);
}

bool IsPathWithin(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto normalizedRoot = root.lexically_normal();
    const auto normalizedCandidate = candidate.lexically_normal();
    if (!normalizedRoot.is_absolute() || !normalizedCandidate.is_absolute()) return false;
    const auto relative = normalizedCandidate.lexically_relative(normalizedRoot);
    if (relative.empty() || relative == L".") return false;
    const auto first = *relative.begin();
    return first != L".." && !relative.is_absolute() && relative.native().find(L':') == std::wstring::npos;
}

}  // namespace aether::fs
