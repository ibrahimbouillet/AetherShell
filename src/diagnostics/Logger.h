// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Centralized, thread-safe diagnostic log.
//
//   AETHER_LOG_INFO("dock", "loaded {} pinned applications", count);
//   AETHER_LOG_ERROR("updates", "download failed: {}", error.Describe());
//
// Messages are UTF-8; convert wide strings with aether::text::Narrow first.
// Never log credentials, tokens or unnecessary personal data, and never log
// per animation frame. Control characters are stripped so untrusted text
// cannot forge log lines. Files rotate at a size limit with bounded retention.
#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace aether::diag {

enum class Severity : int { Trace = 0, Debug, Info, Warning, Error, Critical };

[[nodiscard]] std::string_view ToString(Severity severity) noexcept;

struct LoggerOptions {
    std::filesystem::path directory;          // empty: debugger output only
    std::wstring baseName = L"aethershell";   // aethershell.log, aethershell.1.log, ...
    std::uint64_t maxFileBytes = 1024 * 1024;
    int maxRotatedFiles = 4;
    Severity minimumSeverity = Severity::Info;
    bool writeToDebugger = true;
};

class Logger {
public:
    // Opens the log file. Safe to call once at startup; later calls reconfigure.
    static void Initialize(const LoggerOptions& options);
    static void Shutdown() noexcept;

    static void SetMinimumSeverity(Severity severity) noexcept;
    [[nodiscard]] static bool IsEnabled(Severity severity) noexcept;

    static void Write(Severity severity, std::string_view component, std::string_view message) noexcept;

    // Path of the active log file (empty when logging only to the debugger).
    [[nodiscard]] static std::filesystem::path CurrentFile();
    [[nodiscard]] static std::filesystem::path Directory();
};

template <class... Args>
void Log(Severity severity, std::string_view component, std::format_string<Args...> format, Args&&... args) noexcept {
    if (!Logger::IsEnabled(severity)) return;
    try {
        Logger::Write(severity, component, std::format(format, std::forward<Args>(args)...));
    } catch (...) {
        Logger::Write(severity, component, "<log formatting failed>");
    }
}

}  // namespace aether::diag

#define AETHER_LOG_TRACE(component, ...) ::aether::diag::Log(::aether::diag::Severity::Trace, component, __VA_ARGS__)
#define AETHER_LOG_DEBUG(component, ...) ::aether::diag::Log(::aether::diag::Severity::Debug, component, __VA_ARGS__)
#define AETHER_LOG_INFO(component, ...) ::aether::diag::Log(::aether::diag::Severity::Info, component, __VA_ARGS__)
#define AETHER_LOG_WARN(component, ...) ::aether::diag::Log(::aether::diag::Severity::Warning, component, __VA_ARGS__)
#define AETHER_LOG_ERROR(component, ...) ::aether::diag::Log(::aether::diag::Severity::Error, component, __VA_ARGS__)
#define AETHER_LOG_CRITICAL(component, ...) \
    ::aether::diag::Log(::aether::diag::Severity::Critical, component, __VA_ARGS__)
