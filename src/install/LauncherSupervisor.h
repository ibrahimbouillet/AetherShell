// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include "install/Install.h"

#include <functional>

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

// AS4-17: when a stop is requested, the supervisor hands its child over only once the child can
// receive control commands (Quit/maintenance), has exited, or `timeout` elapsed. A child that never
// opens its channel is left running, never terminated; the requester has its own bounded wait.
enum class ControlReadiness { Listening, Exited, TimedOut };
[[nodiscard]] ControlReadiness AwaitControlReadiness(HANDLE child, const std::function<bool()>& listening,
    std::chrono::milliseconds timeout, std::chrono::milliseconds interval = std::chrono::milliseconds(100));

// Maintenance and explicit Quit/recovery cancel backoff and wait a bounded time for the owner.
// Call while holding the install lifecycle lock when modifying/removing the installation.
[[nodiscard]] Result<void> StopLauncherSupervisor(const InstallLayout& layout,
    std::chrono::milliseconds timeout = std::chrono::seconds(3));

}  // namespace aether::install
