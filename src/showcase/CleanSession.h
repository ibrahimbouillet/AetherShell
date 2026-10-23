// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aether::showcase {

struct ProcessIdentity final {
    std::uint32_t processId = 0;
    std::uint64_t creationTime = 0;

    friend bool operator==(const ProcessIdentity&, const ProcessIdentity&) = default;
};

struct VisibleApplication final {
    ProcessIdentity process;
    std::uint32_t sessionId = 0;
    bool belongsToCurrentUser = false;
    bool hasVisibleWindow = false;
    bool isSystemOrShell = false;
    bool isShowcaseProcess = false;
};

// Executable and arguments are private runtime data. Implementations must not log, persist or
// expose them in errors. The command line is held only in memory until relaunch is attempted.
class RelaunchDescriptor final {
public:
    RelaunchDescriptor(std::wstring executablePath, std::wstring commandLine,
                       std::vector<std::wstring> packagedApplicationIds = {});
    ~RelaunchDescriptor();

    RelaunchDescriptor(const RelaunchDescriptor&) = delete;
    RelaunchDescriptor& operator=(const RelaunchDescriptor&) = delete;
    RelaunchDescriptor(RelaunchDescriptor&& other) noexcept;
    RelaunchDescriptor& operator=(RelaunchDescriptor&& other) noexcept;

    [[nodiscard]] const std::wstring& ExecutablePath() const noexcept { return executablePath_; }
    [[nodiscard]] const std::wstring& CommandLine() const noexcept { return commandLine_; }
    [[nodiscard]] const std::vector<std::wstring>& PackagedApplicationIds() const noexcept {
        return packagedApplicationIds_;
    }
    void Clear() noexcept;

private:
    std::wstring executablePath_;
    std::wstring commandLine_;
    std::vector<std::wstring> packagedApplicationIds_;
};

class ICleanSessionBackend {
public:
    virtual ~ICleanSessionBackend() = default;

    [[nodiscard]] virtual Result<std::vector<VisibleApplication>> EnumerateVisibleApplications(
        std::uint32_t sessionId) = 0;
    [[nodiscard]] virtual Result<RelaunchDescriptor> CaptureRelaunchDescriptor(ProcessIdentity process) = 0;
    [[nodiscard]] virtual Result<void> RequestGracefulClose(ProcessIdentity process) = 0;
    [[nodiscard]] virtual Result<bool> WaitForExit(ProcessIdentity process, std::uint32_t timeoutMilliseconds) = 0;
    [[nodiscard]] virtual Result<void> TerminateApplication(ProcessIdentity process) = 0;
    [[nodiscard]] virtual Result<bool> IsRunning(ProcessIdentity process) = 0;
    [[nodiscard]] virtual Result<void> Relaunch(ProcessIdentity process, const RelaunchDescriptor& descriptor) = 0;
};

enum class CleanSessionState { Idle, Closing, Ready, Restoring, Restored, Failed };

// Coordinates a clean capture session. The native backend supplies process/window operations;
// tests use a fake backend and never close real applications. Destruction best-effort restores any
// applications that this coordinator stopped.
class CleanSession final {
public:
    CleanSession(ICleanSessionBackend& backend, std::uint32_t sessionId,
                 std::uint32_t gracefulCloseTimeoutMilliseconds = 2500);
    ~CleanSession();

    CleanSession(const CleanSession&) = delete;
    CleanSession& operator=(const CleanSession&) = delete;
    CleanSession(CleanSession&&) = delete;
    CleanSession& operator=(CleanSession&&) = delete;

    [[nodiscard]] Result<void> Begin();
    [[nodiscard]] Result<void> Restore() noexcept;
    [[nodiscard]] CleanSessionState State() const noexcept { return state_; }

    [[nodiscard]] static std::vector<ProcessIdentity> SelectApplicationsToClose(
        const std::vector<VisibleApplication>& applications, std::uint32_t sessionId);

private:
    struct SavedApplication final {
        ProcessIdentity process;
        RelaunchDescriptor descriptor;
    };

    [[nodiscard]] Result<void> RollBackAfterFailure(const Error& cause) noexcept;

    ICleanSessionBackend& backend_;
    std::uint32_t sessionId_;
    std::uint32_t gracefulCloseTimeoutMilliseconds_;
    CleanSessionState state_ = CleanSessionState::Idle;
    std::vector<SavedApplication> saved_;
};

}  // namespace aether::showcase
