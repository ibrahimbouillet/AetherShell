// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "platform/windows/TaskbarReplacement.h"

#include <filesystem>
#include <string>

namespace aether::app {

struct TaskbarRecoveryReport {
    platform::TaskbarRecovery outcome = platform::TaskbarRecovery::NothingToRestore;
    bool replacementTurnedOff = false;  // the settings no longer ask to replace the taskbar
};

// `--restore-taskbar` when no Aether Shell instance is running (D21). Blocking: call on a worker.
// Restores the native taskbar from the persisted baseline (without one, makes its windows visible),
// then turns the replacement setting off, as the running-instance command does, so that the next
// normal start does not hide the taskbar again. A newer or unreadable settings file is not touched,
// and a missing one is not created.
[[nodiscard]] Result<TaskbarRecoveryReport> RecoverTaskbarWithoutInstance(
    const std::filesystem::path& configDirectory,
    const platform::TaskbarBackendFactory& makeBackend =
        platform::TaskbarBackendFactory(platform::MakeWindowsTaskbarBackend));

// Plain-language result for the message shown to the user.
[[nodiscard]] std::wstring DescribeTaskbarRecovery(const Result<TaskbarRecoveryReport>& result);

}  // namespace aether::app
