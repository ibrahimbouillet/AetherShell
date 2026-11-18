// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aether::settings {

enum class StartupState { Disabled, Enabled, DisabledByUser, DifferentTarget, UnknownApproval };
// Documented limit of a Run-key command line, in characters, for the whole serialized command.
inline constexpr std::size_t kMaxRunCommandCharacters = 260;
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
    // D82: the per-user sign-in task, beside the Run value, that starts the launcher as soon as the person signs in
    // instead of after Windows' startup-program delay. The Run value stays the switch (Task Manager shows it; the
    // launcher checks it before starting). The task's launcher path, or none. Adapters without a task keep these.
    virtual Result<std::optional<std::wstring>> ReadSignInTask() { return std::optional<std::wstring>{}; }
    virtual Result<void> WriteSignInTask(const std::wstring& launcher) { (void)launcher; return Ok(); }
    virtual Result<void> DeleteSignInTask() { return Ok(); }
};

// Only the standard per-user Run key carries a sign-in task: an adapter given other keys (tests, isolated setups)
// never touches Task Scheduler.
Result<std::shared_ptr<IStartupRegistry>> MakeWindowsStartupRegistry(StartupRegistryPaths paths = {});

// D82: the task's name for a user (unique per account, as Task Scheduler's root folder is shared by all accounts) and
// its definition: a logon trigger for that user only, their own interactive token without elevation, normal
// priority, no time limit, one instance.
std::wstring SignInTaskName(std::wstring_view userSid);
std::wstring SignInTaskXml(std::wstring_view launcher, std::wstring_view userSid);

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
    // D82, at each start of an installed Aether: the sign-in task follows the Run value. On for this launcher: the
    // task exists and starts it. Off, or another installation's: this launcher's task is removed. A Task Manager
    // disable leaves the task in place; it then starts nothing, as the launcher checks the switch first.
    Result<void> ReconcileSignInTask(const std::filesystem::path& launcherPath);

private:
    std::shared_ptr<IStartupRegistry> registry_;
};

}  // namespace aether::settings
