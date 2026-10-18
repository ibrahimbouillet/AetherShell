// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "status/BatteryMapping.h"

#include <chrono>

using namespace aether::status;
using namespace std::chrono_literals;

namespace {

PowerManagerReadings Pm(RawBatteryStatus battery, RawPowerSupplyStatus supply, std::optional<int> percent,
                        RawEnergySaverStatus saver = RawEnergySaverStatus::Off) {
    PowerManagerReadings readings;
    readings.battery = battery;
    readings.supply = supply;
    readings.remainingChargePercent = percent;
    readings.energySaver = saver;
    return readings;
}

SystemPowerReadings Sp(std::uint8_t ac, std::uint8_t flag, std::uint8_t percent,
                       std::uint32_t lifeTime = 0xFFFFFFFF, std::uint8_t saver = 0) {
    SystemPowerReadings readings;
    readings.acLineStatus = ac;
    readings.batteryFlag = flag;
    readings.batteryLifePercent = percent;
    readings.batteryLifeTime = lifeTime;
    readings.systemStatusFlag = saver;
    return readings;
}

// TimeSpan::max() as std::chrono::seconds: PowerManager's "unknown" remaining time.
constexpr std::chrono::seconds kTimeSpanMax{922'337'203'685};

}  // namespace

AETHER_TEST("battery mapping: desktop without a battery is unavailable, never 0%") {
    // PowerManager reports NotPresent and a placeholder 100%; GetSystemPowerStatus agrees (flag 128, 255%).
    BatteryReadings readings;
    readings.powerManager = Pm(RawBatteryStatus::NotPresent, RawPowerSupplyStatus::Adequate, 100);
    readings.systemPower = Sp(1, 128, 255);
    const BatterySnapshot snapshot = MapBatteryReadings(readings);
    CHECK_EQ(snapshot.state, ProviderState::Unavailable);
    CHECK_FALSE(snapshot.batteryPresent);
    CHECK_FALSE(snapshot.percent.has_value());
    CHECK_EQ(snapshot.charge, ChargeState::Unknown);
    CHECK_EQ(snapshot.source, PowerSource::External);
    CHECK_FALSE(snapshot.remaining.has_value());

    // Same machine, GetSystemPowerStatus only.
    BatteryReadings fallback;
    fallback.systemPower = Sp(1, 128, 255);
    const BatterySnapshot fromFallback = MapBatteryReadings(fallback);
    CHECK_EQ(fromFallback.state, ProviderState::Unavailable);
    CHECK_FALSE(fromFallback.batteryPresent);
    CHECK_FALSE(fromFallback.percent.has_value());

    // A machine without a battery cannot be running on one, whatever the supply reading says.
    BatteryReadings odd;
    odd.powerManager = Pm(RawBatteryStatus::NotPresent, RawPowerSupplyStatus::NotPresent, 0);
    const BatterySnapshot oddSnapshot = MapBatteryReadings(odd);
    CHECK_EQ(oddSnapshot.state, ProviderState::Unavailable);
    CHECK_EQ(oddSnapshot.source, PowerSource::Unknown);
    CHECK_FALSE(oddSnapshot.percent.has_value());
}

AETHER_TEST("battery mapping: unknown percent and time stay unknown") {
    BatteryReadings readings;
    readings.powerManager = Pm(RawBatteryStatus::Discharging, RawPowerSupplyStatus::NotPresent, std::nullopt);
    readings.powerManager->remainingDischargeTime = kTimeSpanMax;
    readings.systemPower = Sp(0, 0, 255, 0xFFFFFFFF);
    const BatterySnapshot snapshot = MapBatteryReadings(readings);
    CHECK_EQ(snapshot.state, ProviderState::Available);
    CHECK(snapshot.batteryPresent);
    CHECK_FALSE(snapshot.percent.has_value());
    CHECK_EQ(snapshot.charge, ChargeState::Discharging);
    CHECK_FALSE(snapshot.remaining.has_value());

    // Out-of-range PowerManager values are rejected; a valid GetSystemPowerStatus value is used instead.
    BatteryReadings crossCheck;
    crossCheck.powerManager = Pm(RawBatteryStatus::Discharging, RawPowerSupplyStatus::NotPresent, 255);
    crossCheck.systemPower = Sp(0, 0, 64);
    CHECK_EQ(MapBatteryReadings(crossCheck).percent, std::optional<int>(64));

    crossCheck.systemPower = Sp(0, 0, 255);
    CHECK_FALSE(MapBatteryReadings(crossCheck).percent.has_value());

    // Zero and negative estimates are not estimates.
    BatteryReadings zero;
    zero.powerManager = Pm(RawBatteryStatus::Discharging, RawPowerSupplyStatus::NotPresent, 50);
    zero.powerManager->remainingDischargeTime = 0s;
    CHECK_FALSE(MapBatteryReadings(zero).remaining.has_value());
}

AETHER_TEST("battery mapping: charging and discharging") {
    BatteryReadings charging;
    charging.powerManager = Pm(RawBatteryStatus::Charging, RawPowerSupplyStatus::Adequate, 57);
    charging.powerManager->remainingDischargeTime = 3h;  // ignored while charging
    charging.systemPower = Sp(1, 8, 57);
    const BatterySnapshot c = MapBatteryReadings(charging);
    CHECK_EQ(c.state, ProviderState::Available);
    CHECK(c.batteryPresent);
    CHECK_EQ(c.percent, std::optional<int>(57));
    CHECK_EQ(c.charge, ChargeState::Charging);
    CHECK_EQ(c.source, PowerSource::External);
    CHECK_FALSE(c.remaining.has_value());
    CHECK(c.detail.empty());

    BatteryReadings discharging;
    discharging.powerManager = Pm(RawBatteryStatus::Discharging, RawPowerSupplyStatus::NotPresent, 42);
    discharging.powerManager->remainingDischargeTime = 2h + 30min;
    discharging.systemPower = Sp(0, 0, 42, 9000);
    const BatterySnapshot d = MapBatteryReadings(discharging);
    CHECK_EQ(d.charge, ChargeState::Discharging);
    CHECK_EQ(d.source, PowerSource::Battery);
    CHECK_EQ(d.percent, std::optional<int>(42));
    REQUIRE(d.remaining.has_value());
    CHECK_EQ(d.remaining->count(), 9000);

    // Plugged into an inadequate charger: external power, still discharging.
    BatteryReadings inadequate;
    inadequate.powerManager = Pm(RawBatteryStatus::Discharging, RawPowerSupplyStatus::Inadequate, 30);
    const BatterySnapshot i = MapBatteryReadings(inadequate);
    CHECK_EQ(i.source, PowerSource::External);
    CHECK_EQ(i.charge, ChargeState::Discharging);
}

AETHER_TEST("battery mapping: full on AC and idle below 100%") {
    BatteryReadings full;
    full.powerManager = Pm(RawBatteryStatus::Idle, RawPowerSupplyStatus::Adequate, 100);
    const BatterySnapshot f = MapBatteryReadings(full);
    CHECK_EQ(f.state, ProviderState::Available);
    CHECK_EQ(f.charge, ChargeState::Full);
    CHECK_EQ(f.percent, std::optional<int>(100));

    BatteryReadings idle;
    idle.powerManager = Pm(RawBatteryStatus::Idle, RawPowerSupplyStatus::Adequate, 80);  // charge limit / health mode
    CHECK_EQ(MapBatteryReadings(idle).charge, ChargeState::NotCharging);

    // Idle with an unknown percent on AC: not provably full.
    BatteryReadings unknownIdle;
    unknownIdle.powerManager = Pm(RawBatteryStatus::Idle, RawPowerSupplyStatus::Adequate, std::nullopt);
    CHECK_EQ(MapBatteryReadings(unknownIdle).charge, ChargeState::NotCharging);

    // Idle without external power is not a meaningful state: unknown rather than invented.
    BatteryReadings idleOnBattery;
    idleOnBattery.powerManager = Pm(RawBatteryStatus::Idle, RawPowerSupplyStatus::NotPresent, 70);
    CHECK_EQ(MapBatteryReadings(idleOnBattery).charge, ChargeState::Unknown);
}

AETHER_TEST("battery mapping: energy saver") {
    BatteryReadings on;
    on.powerManager = Pm(RawBatteryStatus::Discharging, RawPowerSupplyStatus::NotPresent, 15, RawEnergySaverStatus::On);
    CHECK(MapBatteryReadings(on).energySaverOn);

    BatteryReadings off;
    off.powerManager =
        Pm(RawBatteryStatus::Discharging, RawPowerSupplyStatus::NotPresent, 15, RawEnergySaverStatus::Off);
    off.systemPower = Sp(0, 0, 15, 0xFFFFFFFF, 1);  // PowerManager wins when it answered
    CHECK_FALSE(MapBatteryReadings(off).energySaverOn);

    BatteryReadings disabled;
    disabled.powerManager =
        Pm(RawBatteryStatus::Charging, RawPowerSupplyStatus::Adequate, 15, RawEnergySaverStatus::Disabled);
    CHECK_FALSE(MapBatteryReadings(disabled).energySaverOn);

    BatteryReadings fallback;
    fallback.systemPower = Sp(0, 0, 15, 3600, 1);
    CHECK(MapBatteryReadings(fallback).energySaverOn);
}

AETHER_TEST("battery mapping: GetSystemPowerStatus fallback") {
    BatteryReadings discharging;
    discharging.systemPower = Sp(0, 1, 63, 7200);
    discharging.powerManagerError = "Class not registered (0x80040154)";
    const BatterySnapshot d = MapBatteryReadings(discharging);
    CHECK_EQ(d.state, ProviderState::Available);
    CHECK(d.batteryPresent);
    CHECK_EQ(d.percent, std::optional<int>(63));
    CHECK_EQ(d.charge, ChargeState::Discharging);
    CHECK_EQ(d.source, PowerSource::Battery);
    REQUIRE(d.remaining.has_value());
    CHECK_EQ(d.remaining->count(), 7200);
    CHECK(d.detail.find("GetSystemPowerStatus") != std::string::npos);
    CHECK(d.detail.find("0x80040154") != std::string::npos);

    BatteryReadings charging;
    charging.systemPower = Sp(1, 8 | 1, 90);
    CHECK_EQ(MapBatteryReadings(charging).charge, ChargeState::Charging);

    BatteryReadings full;
    full.systemPower = Sp(1, 1, 100);
    CHECK_EQ(MapBatteryReadings(full).charge, ChargeState::Full);

    BatteryReadings acIdle;
    acIdle.systemPower = Sp(1, 1, 79);
    CHECK_EQ(MapBatteryReadings(acIdle).charge, ChargeState::NotCharging);

    BatteryReadings unknownLine;
    unknownLine.systemPower = Sp(255, 2, 20);
    const BatterySnapshot u = MapBatteryReadings(unknownLine);
    CHECK_EQ(u.state, ProviderState::Available);
    CHECK_EQ(u.source, PowerSource::Unknown);
    CHECK_EQ(u.charge, ChargeState::Unknown);
    CHECK_EQ(u.percent, std::optional<int>(20));
}

AETHER_TEST("battery mapping: partial PowerManager readings use the cross-check") {
    // BatteryStatus could not be read: presence and charge come from GetSystemPowerStatus.
    BatteryReadings readings;
    readings.powerManager = PowerManagerReadings{};
    readings.powerManager->supply = RawPowerSupplyStatus::Adequate;
    readings.powerManager->remainingChargePercent = 88;
    readings.systemPower = Sp(1, 8, 88);
    const BatterySnapshot snapshot = MapBatteryReadings(readings);
    CHECK_EQ(snapshot.state, ProviderState::Available);
    CHECK(snapshot.batteryPresent);
    CHECK_EQ(snapshot.percent, std::optional<int>(88));
    CHECK_EQ(snapshot.charge, ChargeState::Charging);
    CHECK_EQ(snapshot.source, PowerSource::External);
}

AETHER_TEST("battery mapping: presence disagreement is reported, PowerManager wins") {
    // PowerManager sees a battery, GetSystemPowerStatus says "no system battery".
    BatteryReadings readings;
    readings.powerManager = Pm(RawBatteryStatus::Discharging, RawPowerSupplyStatus::NotPresent, 40);
    readings.systemPower = Sp(0, 128, 255);
    const BatterySnapshot present = MapBatteryReadings(readings);
    CHECK_EQ(present.state, ProviderState::Available);
    CHECK(present.batteryPresent);
    CHECK_EQ(present.percent, std::optional<int>(40));
    CHECK(present.detail.find("disagrees") != std::string::npos);

    // The other way round: still no battery, never a percentage.
    readings.powerManager = Pm(RawBatteryStatus::NotPresent, RawPowerSupplyStatus::Adequate, 100);
    readings.systemPower = Sp(1, 1, 80);
    const BatterySnapshot absent = MapBatteryReadings(readings);
    CHECK_EQ(absent.state, ProviderState::Unavailable);
    CHECK_FALSE(absent.batteryPresent);
    CHECK_FALSE(absent.percent.has_value());
    CHECK(absent.detail.find("disagrees") != std::string::npos);

    // An unknown battery flag (255) is no disagreement.
    readings.powerManager = Pm(RawBatteryStatus::Charging, RawPowerSupplyStatus::Adequate, 70);
    readings.systemPower = Sp(1, 255, 255);
    CHECK(MapBatteryReadings(readings).detail.empty());
}

AETHER_TEST("battery mapping: no usable source is an error, not a guess") {
    BatteryReadings nothing;
    nothing.powerManagerError = "activation failed";
    const BatterySnapshot none = MapBatteryReadings(nothing);
    CHECK_EQ(none.state, ProviderState::Error);
    CHECK_FALSE(none.batteryPresent);
    CHECK_FALSE(none.percent.has_value());
    CHECK(none.detail.find("activation failed") != std::string::npos);

    // Both sources answered, but neither knows whether a battery exists.
    BatteryReadings unknown;
    unknown.powerManager = PowerManagerReadings{};
    unknown.systemPower = Sp(255, 255, 255);
    const BatterySnapshot u = MapBatteryReadings(unknown);
    CHECK_EQ(u.state, ProviderState::Error);
    CHECK_FALSE(u.percent.has_value());
}
