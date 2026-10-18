// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure mapping from raw power readings to a BatterySnapshot. The two sources are:
//  * Windows.System.Power.PowerManager (primary; WinRT, event driven),
//  * GetSystemPowerStatus (cross-check and fallback; Win32).
// The raw types mirror the platform values so this file needs neither WinRT nor windows.h, and every
// case (no battery, unknown values, charging, full, AC idle, energy saver) can be unit tested.
#pragma once

#include "status/StatusTypes.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace aether::status {

// Mirrors Windows.System.Power.BatteryStatus.
enum class RawBatteryStatus { NotPresent = 0, Discharging = 1, Idle = 2, Charging = 3 };
// Mirrors Windows.System.Power.PowerSupplyStatus.
enum class RawPowerSupplyStatus { NotPresent = 0, Inadequate = 1, Adequate = 2 };
// Mirrors Windows.System.Power.EnergySaverStatus.
enum class RawEnergySaverStatus { Disabled = 0, Off = 1, On = 2 };

// Values read from PowerManager. A member is std::nullopt when its property could not be read.
struct PowerManagerReadings {
    std::optional<RawBatteryStatus> battery;
    std::optional<RawPowerSupplyStatus> supply;
    std::optional<int> remainingChargePercent;          // as reported; validated by the mapping
    std::optional<std::chrono::seconds> remainingDischargeTime;  // as reported; TimeSpan::max() is "unknown"
    std::optional<RawEnergySaverStatus> energySaver;
};

// SYSTEM_POWER_STATUS fields, unchanged.
struct SystemPowerReadings {
    std::uint8_t acLineStatus = 255;        // 0 offline, 1 online, 255 unknown
    std::uint8_t batteryFlag = 255;         // 1 high, 2 low, 4 critical, 8 charging, 128 no battery; 255 unknown
    std::uint8_t batteryLifePercent = 255;  // 0..100, 255 unknown
    std::uint8_t systemStatusFlag = 0;      // 1 = battery/energy saver on
    std::uint32_t batteryLifeTime = 0xFFFFFFFF;  // seconds, 0xFFFFFFFF unknown (or on AC)
};

struct BatteryReadings {
    std::optional<PowerManagerReadings> powerManager;  // nullopt: PowerManager unavailable
    std::optional<SystemPowerReadings> systemPower;    // nullopt: GetSystemPowerStatus failed
    std::string powerManagerError;  // why PowerManager is missing, for the snapshot detail (UTF-8)
};

// Rules:
//  * Battery presence comes from PowerManager.BatteryStatus when known, else from the
//    GetSystemPowerStatus battery flag; when both are known and disagree, PowerManager wins and the
//    disagreement is noted in `detail`. No battery: state Unavailable, batteryPresent=false and no
//    percent (never 0%, and never PowerManager's 100% placeholder).
//  * Percent: PowerManager, else GetSystemPowerStatus; values outside 0..100 (255 = unknown) are nullopt.
//  * Charge: Charging / Discharging as reported; Idle on external power is Full at 100% and NotCharging
//    below; without PowerManager the same rules use the AC line status and the charging flag.
//  * Remaining time only while discharging, and only when Windows reports a finite estimate.
//  * No usable source at all: state Error.
[[nodiscard]] BatterySnapshot MapBatteryReadings(const BatteryReadings& readings);

}  // namespace aether::status
