// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "settings/StartupRegistration.h"
#include <chrono>
#include <cstdint>
#include <optional>

namespace aether::install {

// Pure policy: setup may disclose a default only for a new interactive installation. Existing
// settings, a missing previously registered entry and Windows startup disapproval take precedence.
[[nodiscard]] bool SetupStartupDefault(bool interactive, bool existingInstallation,
    std::optional<bool> savedChoice, settings::StartupState registration) noexcept;

class CrashRestartPolicy {
public:
    // At most three retries during this launcher's lifetime. Ordinary exits (including Task
    // Manager's usual exit code 1) and Ctrl+C never count as an exception or trigger a retry.
    [[nodiscard]] std::optional<std::chrono::milliseconds> NextDelay(
        std::uint32_t exitCode, bool sameInstallation = true) noexcept;
private:
    unsigned attempts_ = 0;
};

}  // namespace aether::install
