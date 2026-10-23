// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include "install/Install.h"

namespace aether::install {

// One supervisor per canonical installation, using session-local kernel objects only.
class LauncherSupervisor {
public:
    LauncherSupervisor() = default;
    LauncherSupervisor(const LauncherSupervisor&) = delete;
    LauncherSupervisor(LauncherSupervisor&& other) noexcept;
    ~LauncherSupervisor();
    void Release() noexcept;
    [[nodiscard]] static Result<LauncherSupervisor> Acquire(const InstallLayout& layout);
    [[nodiscard]] Result<bool> StopRequested(std::chrono::milliseconds timeout) const;
    [[nodiscard]] HANDLE StopEvent() const noexcept { return stop_.Get(); }
private:
    platform::UniqueHandle lease_, stop_;
    bool held_ = false;
};

// Maintenance and explicit Quit/recovery cancel backoff and wait a bounded time for the owner.
// Call while holding the install lifecycle lock when modifying/removing the installation.
[[nodiscard]] Result<void> StopLauncherSupervisor(const InstallLayout& layout,
    std::chrono::milliseconds timeout = std::chrono::seconds(3));

}  // namespace aether::install
