// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/LifecyclePolicy.h"

namespace aether::install {

bool SetupStartupDefault(bool interactive, bool existingInstallation, std::optional<bool> savedChoice,
                         settings::StartupState registration) noexcept {
    using settings::StartupState;
    if (!interactive || registration == StartupState::DisabledByUser ||
        registration == StartupState::UnknownApproval || registration == StartupState::DifferentTarget) return false;
    if (savedChoice) return *savedChoice && registration == StartupState::Enabled;
    return !existingInstallation || registration == StartupState::Enabled;
}

std::optional<std::chrono::milliseconds> CrashRestartPolicy::NextDelay(
    std::uint32_t exitCode, bool sameInstallation) noexcept {
    const auto severity = exitCode & 0xE0000000u;
    if (!sameInstallation || exitCode == 0xC000013Au ||
        (severity != 0xC0000000u && severity != 0xE0000000u) || attempts_ >= 3) return std::nullopt;
    const auto delay = std::chrono::milliseconds(1000u << attempts_);
    ++attempts_;
    return delay;
}

}  // namespace aether::install
