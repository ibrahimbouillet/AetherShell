// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aether::settings {

enum class StartupState { Disabled, Enabled, DisabledByUser, DifferentTarget, UnknownApproval };
struct StartupRegistryPaths {
    std::wstring run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    std::wstring approval = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
};

// Blocking registry seam. Tests inject memory; production uses HKCU only. The approval value is
// read-only: an explicit Task Manager disable is never changed or bypassed.
class IStartupRegistry {
public:
    virtual ~IStartupRegistry() = default;
    virtual Result<std::optional<std::wstring>> ReadRun() = 0;
    virtual Result<std::vector<std::uint8_t>> ReadApproval() = 0;
    virtual Result<void> WriteRun(std::wstring command) = 0;
    virtual Result<void> DeleteRun() = 0;
};

Result<std::shared_ptr<IStartupRegistry>> MakeWindowsStartupRegistry(StartupRegistryPaths paths = {});

class StartupRegistration {
public:
    explicit StartupRegistration(std::shared_ptr<IStartupRegistry> registry);
    // Blocking: call on a worker. Enable follows an accepted disclosed setup choice or direct
    // Settings Apply. Unknown StartupApproved formats make Enable fail Unsupported.
    Result<StartupState> IsEnabled(const std::filesystem::path& expectedPath);
    Result<void> Enable(const std::filesystem::path& launcherPath);
    Result<void> Disable();
    // Setup opt-out must not remove another installation's startup target or change approval.
    Result<void> DisableIfOwned(const std::filesystem::path& launcherPath);

private:
    std::shared_ptr<IStartupRegistry> registry_;
};

}  // namespace aether::settings
