// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Battery and power provider. A dedicated MTA thread subscribes to the Windows.System.Power.PowerManager
// change events (BatteryStatus, PowerSupplyStatus, RemainingChargePercent, RemainingDischargeTime,
// EnergySaverStatus), reads PowerManager plus GetSystemPowerStatus on every change, maps the readings
// with MapBatteryReadings and publishes only snapshots that differ from the previous one.
//
// When PowerManager cannot be used (activation failure, or disabled through the options) the provider
// falls back to GetSystemPowerStatus alone. That API has no change notification without a window, so it
// is then re-read every `fallbackRefreshInterval` and whenever Refresh() is called (the application
// forwards WM_POWERBROADCAST through platform::SystemEvents::PowerSettingChanged).
#pragma once

#include "status/StatusTypes.h"

#include <chrono>
#include <memory>

namespace aether::status {

struct BatteryProviderOptions {
    bool usePowerManager = true;  // false forces the GetSystemPowerStatus fallback (used by tests)
    // Positive, at most 4,294,967 s (the largest whole-second finite Win32 wait).
    // Start() rejects values outside this range, even when PowerManager is preferred.
    std::chrono::seconds fallbackRefreshInterval{30};
};

class BatteryProvider final : public IStatusProvider<BatterySnapshot> {
public:
    explicit BatteryProvider(BatteryProviderOptions options = {});
    ~BatteryProvider() override;
    BatteryProvider(const BatteryProvider&) = delete;
    BatteryProvider& operator=(const BatteryProvider&) = delete;

    Result<void> Start(Listener listener) override;
    void Stop() noexcept override;
    [[nodiscard]] BatterySnapshot Current() const override;

    // Thread-safe: re-reads the power state now (for WM_POWERBROADCAST / resume notifications).
    void Refresh() noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace aether::status
