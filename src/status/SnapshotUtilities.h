// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Value comparison and diagnostic text for the snapshots declared in StatusTypes.h. The contract
// header does not define operator==, so providers use these to suppress duplicate notifications.
#pragma once

#include "status/StatusTypes.h"

#include <string>
#include <string_view>

namespace aether::status {

[[nodiscard]] bool SameContent(const ClockSnapshot& a, const ClockSnapshot& b) noexcept;
[[nodiscard]] bool SameContent(const BatterySnapshot& a, const BatterySnapshot& b) noexcept;
[[nodiscard]] bool SameContent(const KeyboardSnapshot& a, const KeyboardSnapshot& b) noexcept;
[[nodiscard]] bool SameContent(const NetworkSnapshot& a, const NetworkSnapshot& b) noexcept;

[[nodiscard]] std::string_view ToString(ProviderState state) noexcept;
[[nodiscard]] std::string_view ToString(ChargeState state) noexcept;
[[nodiscard]] std::string_view ToString(PowerSource source) noexcept;
[[nodiscard]] std::string_view ToString(ConnectionKind kind) noexcept;
[[nodiscard]] std::string_view ToString(InternetConnectivity connectivity) noexcept;

// One-line UTF-8 descriptions for logs and test output. They contain no personal data (the network
// description never includes a profile name).
[[nodiscard]] std::string Describe(const ClockSnapshot& snapshot);
[[nodiscard]] std::string Describe(const BatterySnapshot& snapshot);
[[nodiscard]] std::string Describe(const KeyboardSnapshot& snapshot);
[[nodiscard]] std::string Describe(const NetworkSnapshot& snapshot);

}  // namespace aether::status
