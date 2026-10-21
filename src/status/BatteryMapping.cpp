// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/BatteryMapping.h"

namespace aether::status {
namespace {

constexpr std::uint8_t kFlagCharging = 8;
constexpr std::uint8_t kFlagNoBattery = 128;
constexpr std::uint8_t kUnknownByte = 255;
constexpr std::uint32_t kUnknownLifeTime = 0xFFFFFFFF;
// Windows reports "unknown" as TimeSpan::max() (PowerManager) or 0xFFFFFFFF (GetSystemPowerStatus).
// Anything beyond a week is one of those sentinels, not an estimate.
constexpr std::chrono::seconds kLongestPlausibleEstimate = std::chrono::hours(24 * 7);

std::optional<int> ValidPercent(int value) {
    if (value < 0 || value > 100) return std::nullopt;
    return value;
}

std::optional<std::chrono::seconds> ValidEstimate(std::chrono::seconds value) {
    if (value <= std::chrono::seconds::zero() || value > kLongestPlausibleEstimate) return std::nullopt;
    return value;
}

ChargeState IdleCharge(PowerSource source, const std::optional<int>& percent) {
    if (source != PowerSource::External) return ChargeState::Unknown;  // idle on battery: not meaningful
    return percent == 100 ? ChargeState::Full : ChargeState::NotCharging;
}

}  // namespace

BatterySnapshot MapBatteryReadings(const BatteryReadings& readings) {
    BatterySnapshot snapshot;
    const PowerManagerReadings* pm = readings.powerManager ? &*readings.powerManager : nullptr;
    const SystemPowerReadings* sp = readings.systemPower ? &*readings.systemPower : nullptr;

    if (pm == nullptr && sp == nullptr) {
        snapshot.state = ProviderState::Error;
        snapshot.detail = "no power information source is available";
        if (!readings.powerManagerError.empty()) snapshot.detail += ": " + readings.powerManagerError;
        return snapshot;
    }
    if (pm == nullptr) {
        snapshot.detail = "PowerManager unavailable, using GetSystemPowerStatus";
        if (!readings.powerManagerError.empty()) snapshot.detail += " (" + readings.powerManagerError + ")";
    }

    // Power source.
    if (pm != nullptr && pm->supply) {
        snapshot.source =
            *pm->supply == RawPowerSupplyStatus::NotPresent ? PowerSource::Battery : PowerSource::External;
    } else if (sp != nullptr) {
        snapshot.source = sp->acLineStatus == 1   ? PowerSource::External
                          : sp->acLineStatus == 0 ? PowerSource::Battery
                                                  : PowerSource::Unknown;
    }

    // Energy saver (formerly battery saver).
    if (pm != nullptr && pm->energySaver) {
        snapshot.energySaverOn = *pm->energySaver == RawEnergySaverStatus::On;
    } else if (sp != nullptr) {
        snapshot.energySaverOn = sp->systemStatusFlag == 1;
    }

    // Battery presence. PowerManager is authoritative when it answered; GetSystemPowerStatus is the
    // cross-check, and a disagreement is reported in `detail` (both read the same composite battery, so
    // it normally means one of them is mid-update).
    std::optional<bool> present;
    std::optional<bool> presentPerSystemPower;
    if (sp != nullptr && sp->batteryFlag != kUnknownByte) {
        presentPerSystemPower = (sp->batteryFlag & kFlagNoBattery) == 0;
    }
    if (pm != nullptr && pm->battery) {
        present = *pm->battery != RawBatteryStatus::NotPresent;
        if (presentPerSystemPower && *presentPerSystemPower != *present) {
            if (!snapshot.detail.empty()) snapshot.detail += "; ";
            snapshot.detail += "GetSystemPowerStatus disagrees about battery presence";
        }
    } else {
        present = presentPerSystemPower;
    }
    if (!present) {
        snapshot.state = ProviderState::Error;
        if (!snapshot.detail.empty()) snapshot.detail += "; ";
        snapshot.detail += "Windows reports an unknown battery status";
        return snapshot;
    }
    if (!*present) {
        snapshot.state = ProviderState::Unavailable;
        snapshot.batteryPresent = false;
        // Without a battery the machine cannot be running on one.
        if (snapshot.source == PowerSource::Battery) snapshot.source = PowerSource::Unknown;
        return snapshot;
    }

    snapshot.state = ProviderState::Available;
    snapshot.batteryPresent = true;
    // Only a known, agreeing Win32 battery flag can report the OS low/critical indication.
    if (sp != nullptr && sp->batteryFlag != kUnknownByte && presentPerSystemPower.value_or(false))
        snapshot.lowOrCritical = (sp->batteryFlag & (2u | 4u)) != 0;

    if (pm != nullptr && pm->remainingChargePercent) snapshot.percent = ValidPercent(*pm->remainingChargePercent);
    if (!snapshot.percent && sp != nullptr && sp->batteryLifePercent != kUnknownByte) {
        snapshot.percent = ValidPercent(sp->batteryLifePercent);
    }

    if (pm != nullptr && pm->battery) {
        switch (*pm->battery) {
            case RawBatteryStatus::Charging: snapshot.charge = ChargeState::Charging; break;
            case RawBatteryStatus::Discharging: snapshot.charge = ChargeState::Discharging; break;
            case RawBatteryStatus::Idle: snapshot.charge = IdleCharge(snapshot.source, snapshot.percent); break;
            case RawBatteryStatus::NotPresent: break;  // handled above
        }
    } else if (sp != nullptr) {
        if (sp->batteryFlag != kUnknownByte && (sp->batteryFlag & kFlagCharging) != 0) {
            snapshot.charge = ChargeState::Charging;
        } else if (sp->acLineStatus == 1) {
            snapshot.charge = IdleCharge(PowerSource::External, snapshot.percent);
        } else if (sp->acLineStatus == 0) {
            snapshot.charge = ChargeState::Discharging;
        }
    }

    if (snapshot.charge == ChargeState::Discharging) {
        if (pm != nullptr && pm->remainingDischargeTime) {
            snapshot.remaining = ValidEstimate(*pm->remainingDischargeTime);
        }
        if (!snapshot.remaining && sp != nullptr && sp->batteryLifeTime != kUnknownLifeTime) {
            snapshot.remaining = ValidEstimate(std::chrono::seconds(sp->batteryLifeTime));
        }
    }
    return snapshot;
}

}  // namespace aether::status
