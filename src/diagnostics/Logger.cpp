// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "diagnostics/Logger.h"

#include "core/Strings.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>

namespace aether::diag {
namespace {

struct LoggerState {
    std::mutex mutex;
    LoggerOptions options;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::uint64_t fileBytes = 0;
    std::filesystem::path currentPath;
    std::atomic<int> minimum{static_cast<int>(Severity::Info)};
    std::atomic<bool> debugger{true};
};

LoggerState& State() {
    static LoggerState state;
    return state;
}

std::filesystem::path RotatedPath(const LoggerOptions& options, int index) {
    if (index == 0) return options.directory / (options.baseName + L".log");
    return options.directory / (options.baseName + L"." + std::to_wstring(index) + L".log");
}

void CloseFileLocked(LoggerState& state) {
    if (state.file != INVALID_HANDLE_VALUE) {
        ::FlushFileBuffers(state.file);
        ::CloseHandle(state.file);
        state.file = INVALID_HANDLE_VALUE;
    }
}

void OpenFileLocked(LoggerState& state) {
    CloseFileLocked(state);
    if (state.options.directory.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(state.options.directory, ec);
    state.currentPath = RotatedPath(state.options, 0);
    state.file = ::CreateFileW(state.currentPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (state.file == INVALID_HANDLE_VALUE) {
        state.currentPath.clear();
        return;
    }
    LARGE_INTEGER size{};
    state.fileBytes = ::GetFileSizeEx(state.file, &size) ? static_cast<std::uint64_t>(size.QuadPart) : 0;
}

void RotateLocked(LoggerState& state) {
    CloseFileLocked(state);
    const auto& options = state.options;
    std::error_code ec;
    std::filesystem::remove(RotatedPath(options, options.maxRotatedFiles), ec);
    for (int i = options.maxRotatedFiles - 1; i >= 0; --i) {
        const auto from = RotatedPath(options, i);
        if (std::filesystem::exists(from, ec)) {
            std::filesystem::rename(from, RotatedPath(options, i + 1), ec);
        }
    }
    OpenFileLocked(state);
}

std::string Timestamp() {
    SYSTEMTIME utc{};
    ::GetSystemTime(&utc);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", utc.wYear, utc.wMonth, utc.wDay,
                  utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds);
    return buffer;
}

}  // namespace

std::string_view ToString(Severity severity) noexcept {
    switch (severity) {
        case Severity::Trace: return "TRACE";
        case Severity::Debug: return "DEBUG";
        case Severity::Info: return "INFO ";
        case Severity::Warning: return "WARN ";
        case Severity::Error: return "ERROR";
        case Severity::Critical: return "CRIT ";
    }
    return "?????";
}

void Logger::Initialize(const LoggerOptions& options) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.options = options;
    if (state.options.maxRotatedFiles < 0) state.options.maxRotatedFiles = 0;
    if (state.options.maxFileBytes < 16 * 1024) state.options.maxFileBytes = 16 * 1024;
    state.minimum = static_cast<int>(options.minimumSeverity);
    state.debugger = options.writeToDebugger;
    OpenFileLocked(state);
}

void Logger::Shutdown() noexcept {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    CloseFileLocked(state);
    state.currentPath.clear();
}

void Logger::SetMinimumSeverity(Severity severity) noexcept { State().minimum = static_cast<int>(severity); }

bool Logger::IsEnabled(Severity severity) noexcept { return static_cast<int>(severity) >= State().minimum.load(); }

void Logger::Write(Severity severity, std::string_view component, std::string_view message) noexcept {
    if (!IsEnabled(severity)) return;
    try {
        std::string line;
        line.reserve(64 + component.size() + message.size());
        line += Timestamp();
        line += " [";
        line += ToString(severity);
        line += "] [";
        line += text::SanitizeForLog(component, 32);
        line += "] ";
        line += text::SanitizeForLog(message, 4096);
        line += "\r\n";

        auto& state = State();
        if (state.debugger) {
            const std::wstring wide = text::Widen(line);
            ::OutputDebugStringW(wide.c_str());
        }

        std::lock_guard lock(state.mutex);
        if (state.file == INVALID_HANDLE_VALUE) return;
        if (state.fileBytes + line.size() > state.options.maxFileBytes) RotateLocked(state);
        if (state.file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        if (::WriteFile(state.file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)) {
            state.fileBytes += written;
        }
        if (severity >= Severity::Error) ::FlushFileBuffers(state.file);
    } catch (...) {
        // Logging must never take the application down.
    }
}

std::filesystem::path Logger::CurrentFile() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    return state.currentPath;
}

std::filesystem::path Logger::Directory() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    return state.options.directory;
}

}  // namespace aether::diag
