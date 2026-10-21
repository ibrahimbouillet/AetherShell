// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace aether::weather {

struct WindowsLocation {
    double latitude = 0;
    double longitude = 0;
    std::optional<std::wstring> country; // unset unless Windows provides a real country; never inferred
};

// Pure coordinate validation and coarse 0.1-degree rounding. Unknown coordinates are errors,
// never a default city or zero reading. No geolocation or network API is called.
[[nodiscard]] Result<WindowsLocation> CoarsenWindowsLocation(double latitude, double longitude);
[[nodiscard]] bool IsWindowsLocationSampleFresh(std::chrono::milliseconds age) noexcept;

// UI STA and foreground only, directly after a first-use/Settings click explaining that coarse
// coordinates go to the weather service. Never blocks. The callback can run on a completion thread;
// the host must capture a lifetime guard and post back to its dispatcher. false means denied.
[[nodiscard]] Result<void> BeginWindowsLocationAccess(std::function<void(Result<bool>)> completion);

// Serial MTA worker only. appConsent records a prior explicit successful BeginWindowsLocationAccess
// result and may persist across launches. It is not OS permission: policy/status is checked afresh,
// and denied/revoked access remains an error. A fresh default preference cannot count as consent.
// This method never calls a permission-request API. Coordinates are rounded to 0.1 degree,
// sampled at requested 10-km accuracy and rejected when older than 30 minutes. Windows policy/device
// or unpackaged-desktop restrictions stay errors. No IP/SSID lookup, logs, storage or reverse geocoder.
[[nodiscard]] Result<WindowsLocation> ReadWindowsLocation(bool appConsent,
    CancellationToken cancellation = {}, std::chrono::milliseconds timeout = std::chrono::seconds(10));

} // namespace aether::weather
