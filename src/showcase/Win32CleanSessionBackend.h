// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "showcase/CleanSession.h"

#include <memory>
#include <span>

namespace aether::showcase {

// Native backend used only by the explicitly user-started showcase runner. It considers visible,
// same-user, same-session desktop applications and protects the Windows shell, critical processes,
// the showcase worker and Aether itself. Tests must use a fake backend.
class Win32CleanSessionBackend final : public ICleanSessionBackend {
public:
    Win32CleanSessionBackend() noexcept;
    ~Win32CleanSessionBackend() override;

    Win32CleanSessionBackend(const Win32CleanSessionBackend&) = delete;
    Win32CleanSessionBackend& operator=(const Win32CleanSessionBackend&) = delete;
    Win32CleanSessionBackend(Win32CleanSessionBackend&& other) noexcept;
    Win32CleanSessionBackend& operator=(Win32CleanSessionBackend&& other) noexcept;

    [[nodiscard]] static Result<Win32CleanSessionBackend> Create(
        std::span<const std::uint32_t> keepProcessIds = {}) noexcept;
    [[nodiscard]] static Result<std::uint32_t> CurrentSessionId() noexcept;
    // Includes the runner, its launching controller chain and explicitly retained controllers.
    // Identities stay in memory; a zero creation time means Windows denied the optional identity
    // query, so the PID is conservatively protected.
    [[nodiscard]] std::span<const ProcessIdentity> ProtectedControllerProcesses() const noexcept;

    [[nodiscard]] Result<std::vector<VisibleApplication>> EnumerateVisibleApplications(
        std::uint32_t sessionId) override;
    [[nodiscard]] Result<RelaunchDescriptor> CaptureRelaunchDescriptor(ProcessIdentity process) override;
    [[nodiscard]] Result<void> RequestGracefulClose(ProcessIdentity process) override;
    [[nodiscard]] Result<bool> WaitForExit(ProcessIdentity process, std::uint32_t timeoutMilliseconds) override;
    [[nodiscard]] Result<void> TerminateApplication(ProcessIdentity process) override;
    [[nodiscard]] Result<bool> IsRunning(ProcessIdentity process) override;
    [[nodiscard]] Result<void> Relaunch(ProcessIdentity process, const RelaunchDescriptor& descriptor) override;

private:
    struct Impl;
    explicit Win32CleanSessionBackend(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::showcase
