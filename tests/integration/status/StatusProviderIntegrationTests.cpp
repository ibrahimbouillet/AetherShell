// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Starts the real status providers on this machine (a laptop with a battery) and checks that their first
// snapshots are plausible and that Stop() ends every listener call.
#include "AetherTest.h"

#include "status/KeyboardLayouts.h"
#include "status/SnapshotUtilities.h"
#include "status/StatusProviders.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::status;
using namespace std::chrono_literals;

namespace {

// Thread-safe record of every snapshot a provider delivered.
template <class Snapshot>
class Collector {
public:
    typename IStatusProvider<Snapshot>::Listener Listener() {
        return [this](const Snapshot& snapshot) {
            if (inListener_.exchange(true)) concurrentCalls_ = true;
            {
                std::lock_guard lock(mutex_);
                snapshots_.push_back(snapshot);
                times_.push_back(std::chrono::steady_clock::now());
            }
            changed_.notify_all();
            inListener_ = false;
        };
    }

    // Waits until at least `count` snapshots arrived. Returns false on timeout.
    bool WaitFor(std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return snapshots_.size() >= count; });
    }

    std::size_t Count() const {
        std::lock_guard lock(mutex_);
        return snapshots_.size();
    }
    Snapshot At(std::size_t index) const {
        std::lock_guard lock(mutex_);
        return snapshots_.at(index);
    }
    Snapshot Last() const {
        std::lock_guard lock(mutex_);
        return snapshots_.back();
    }
    std::chrono::steady_clock::time_point TimeAt(std::size_t index) const {
        std::lock_guard lock(mutex_);
        return times_.at(index);
    }
    bool HadConcurrentCalls() const { return concurrentCalls_.load(); }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Snapshot> snapshots_;
    std::vector<std::chrono::steady_clock::time_point> times_;
    std::atomic<bool> inListener_{false};
    std::atomic<bool> concurrentCalls_{false};
};

std::int64_t FileTimeValue(const SYSTEMTIME& time) {
    FILETIME fileTime{};
    ::SystemTimeToFileTime(&time, &fileTime);
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return static_cast<std::int64_t>(value.QuadPart);
}

SYSTEMTIME LocalFields(const ClockSnapshot& s) {
    SYSTEMTIME time{};
    time.wYear = static_cast<WORD>(s.year);
    time.wMonth = static_cast<WORD>(s.month);
    time.wDay = static_cast<WORD>(s.day);
    time.wHour = static_cast<WORD>(s.hour);
    time.wMinute = static_cast<WORD>(s.minute);
    time.wSecond = static_cast<WORD>(s.second);
    return time;
}

double SecondsBetween(std::chrono::system_clock::time_point a, std::chrono::system_clock::time_point b) {
    return std::chrono::duration<double>(a - b).count();
}

}  // namespace

AETHER_TEST("clock provider: real time, boundary-aligned ticks, notifications and Stop") {
    // Callback state is declared first so provider teardown joins before it is destroyed, including
    // when REQUIRE unwinds the test before its explicit Stop().
    Collector<ClockSnapshot> collector;
    ClockProvider clock(ClockResolution::Seconds);
    REQUIRE(clock.Start(collector.Listener()).ok());
    REQUIRE(collector.WaitFor(1, 3s));

    const ClockSnapshot first = collector.At(0);
    TEST_NOTE(Describe(first));
    REQUIRE_EQ(first.state, ProviderState::Available);
    const double utcOffset = SecondsBetween(first.utc, std::chrono::system_clock::now());
    CHECK(utcOffset > -2.0 && utcOffset <= 0.0);
    // The local fields describe local wall-clock time.
    SYSTEMTIME localNow{};
    ::GetLocalTime(&localNow);
    const double localOffset =
        static_cast<double>(FileTimeValue(localNow) - FileTimeValue(LocalFields(first))) / 10'000'000.0;
    CHECK(localOffset >= 0.0 && localOffset < 2.0);
    CHECK_FALSE(first.timeZoneName.empty());
    CHECK(clock.Current().utc >= first.utc);  // another tick may arrive between separate reads

    // Ticks land just after second boundaries.
    REQUIRE(collector.WaitFor(3, 3s));
    for (std::size_t i = 1; i < 3; ++i) {
        const ClockSnapshot tick = collector.At(i);
        const auto subSecond = std::chrono::duration_cast<std::chrono::milliseconds>(
            tick.utc.time_since_epoch() % std::chrono::seconds(1));
        TEST_NOTE(std::format("tick {} at +{} ms after the boundary", i, subSecond.count()));
        CHECK(subSecond < 500ms);  // generous: other builds may be loading this machine
        CHECK(tick.utc > collector.At(i - 1).utc);
    }

    // NotifyTimeChanged republishes immediately (well before the next boundary would).
    const std::size_t before = collector.Count();
    const auto requested = std::chrono::steady_clock::now();
    clock.NotifyTimeChanged();
    REQUIRE(collector.WaitFor(before + 1, 2s));
    const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(collector.TimeAt(before) - requested);
    TEST_NOTE(std::format("NotifyTimeChanged latency {} ms", latency.count()));
    CHECK(latency < 900ms);

    // Changing the resolution publishes right away as well.
    const std::size_t beforeResolution = collector.Count();
    clock.SetResolution(ClockResolution::Minutes);
    CHECK_EQ(clock.Resolution(), ClockResolution::Minutes);
    REQUIRE(collector.WaitFor(beforeResolution + 1, 2s));
    clock.SetResolution(ClockResolution::Seconds);

    clock.Stop();
    const std::size_t afterStop = collector.Count();
    clock.NotifyTimeChanged();
    std::this_thread::sleep_for(1500ms);  // more than one second boundary
    CHECK_EQ(collector.Count(), afterStop);
    CHECK_FALSE(collector.HadConcurrentCalls());
    clock.Stop();  // idempotent
}

AETHER_TEST("battery provider: this laptop reports a battery") {
    SYSTEM_POWER_STATUS status{};
    REQUIRE(::GetSystemPowerStatus(&status));
    TEST_NOTE(std::format("GetSystemPowerStatus: ac={} flag={} percent={} life={}", status.ACLineStatus,
                          status.BatteryFlag, status.BatteryLifePercent, status.BatteryLifeTime));

    Collector<BatterySnapshot> collector;
    BatteryProvider battery;
    REQUIRE(battery.Start(collector.Listener()).ok());
    REQUIRE(collector.WaitFor(1, 5s));
    const BatterySnapshot snapshot = collector.At(0);
    TEST_NOTE(Describe(snapshot));
    CHECK_NE(snapshot.state, ProviderState::Initializing);

    if ((status.BatteryFlag & 128) == 0 && status.BatteryFlag != 255) {
        CHECK_EQ(snapshot.state, ProviderState::Available);
        CHECK(snapshot.batteryPresent);
        if (snapshot.percent) {
            CHECK(*snapshot.percent >= 0 && *snapshot.percent <= 100);
            if (status.BatteryLifePercent <= 100) CHECK(std::abs(*snapshot.percent - status.BatteryLifePercent) <= 2);
        }
        CHECK_NE(snapshot.source, PowerSource::Unknown);
        if (snapshot.source == PowerSource::Battery) CHECK_EQ(snapshot.charge, ChargeState::Discharging);
    } else if (status.BatteryFlag != 255) {
        CHECK_EQ(snapshot.state, ProviderState::Unavailable);
        CHECK_FALSE(snapshot.percent.has_value());
    } else {
        // Win32's unknown flag is not evidence that the battery is absent. PowerManager may still
        // know its state, or the provider may report an explicit error if neither source knows.
        TEST_NOTE("GetSystemPowerStatus battery presence is unknown; no absence is inferred");
        if (snapshot.percent) CHECK(*snapshot.percent >= 0 && *snapshot.percent <= 100);
    }

    // Refresh() of an unchanged state is deduplicated.
    battery.Refresh();
    std::this_thread::sleep_for(400ms);
    TEST_NOTE(std::format("snapshots after Refresh: {}", collector.Count()));

    battery.Stop();
    const std::size_t afterStop = collector.Count();
    battery.Refresh();
    std::this_thread::sleep_for(400ms);
    CHECK_EQ(collector.Count(), afterStop);
    CHECK_FALSE(collector.HadConcurrentCalls());
}

AETHER_TEST("battery provider: GetSystemPowerStatus fallback agrees with PowerManager") {
    Collector<BatterySnapshot> primaryCollector;
    Collector<BatterySnapshot> fallbackCollector;
    BatteryProvider primary;
    BatteryProvider fallback(BatteryProviderOptions{false, std::chrono::seconds(30)});
    REQUIRE(primary.Start(primaryCollector.Listener()).ok());
    REQUIRE(fallback.Start(fallbackCollector.Listener()).ok());
    REQUIRE(primaryCollector.WaitFor(1, 5s));
    REQUIRE(fallbackCollector.WaitFor(1, 5s));
    const BatterySnapshot a = primaryCollector.Last();
    const BatterySnapshot b = fallbackCollector.Last();
    TEST_NOTE("PowerManager:         " + Describe(a));
    TEST_NOTE("GetSystemPowerStatus: " + Describe(b));
    CHECK_EQ(a.state, b.state);
    CHECK_EQ(a.batteryPresent, b.batteryPresent);
    CHECK_EQ(a.source, b.source);
    if (a.percent && b.percent) CHECK(std::abs(*a.percent - *b.percent) <= 2);
    CHECK(b.detail.find("GetSystemPowerStatus") != std::string::npos);
    primary.Stop();
    fallback.Stop();
}

AETHER_TEST("keyboard provider: foreground input language") {
    Collector<KeyboardSnapshot> collector;
    KeyboardProvider keyboard;
    REQUIRE(keyboard.Start(collector.Listener()).ok());
    REQUIRE(collector.WaitFor(1, 3s));
    const KeyboardSnapshot snapshot = collector.At(0);
    TEST_NOTE(Describe(snapshot));
    REQUIRE_EQ(snapshot.state, ProviderState::Available);
    CHECK_FALSE(snapshot.shortLabel.empty());
    CHECK_FALSE(snapshot.localeName.empty());
    CHECK_FALSE(snapshot.languageName.empty());
    CHECK(snapshot.shortLabel.size() <= 3);

    // Keep the real provider alive beyond one comparison period. Exact transition behaviour is
    // tested with injected readings: the user's foreground window or language can change between
    // any two Win32 reads, so an integration test must not require two separate samples to match.
    std::this_thread::sleep_for(1200ms);
    const KeyboardSnapshot current = keyboard.Current();
    CHECK_EQ(current.state, ProviderState::Available);
    CHECK_FALSE(current.localeName.empty());
    CHECK_FALSE(current.shortLabel.empty());

    // Refresh() re-reads but only publishes a change.
    keyboard.Refresh();
    std::this_thread::sleep_for(300ms);
    TEST_NOTE(std::format("snapshots after Refresh: {}", collector.Count()));

    keyboard.Stop();
    const std::size_t afterStop = collector.Count();
    keyboard.Refresh();
    std::this_thread::sleep_for(1500ms);  // longer than the 1 s comparison timer
    CHECK_EQ(collector.Count(), afterStop);
    CHECK_FALSE(collector.HadConcurrentCalls());
}

AETHER_TEST("keyboard layouts: every installed input locale can be described") {
    const int count = ::GetKeyboardLayoutList(0, nullptr);
    REQUIRE(count > 0);
    std::vector<HKL> layouts(static_cast<std::size_t>(count));
    const int filled = ::GetKeyboardLayoutList(count, layouts.data());
    REQUIRE(filled > 0);
    for (int i = 0; i < filled; ++i) {
        const auto hkl = reinterpret_cast<std::uintptr_t>(layouts[static_cast<std::size_t>(i)]);
        const KeyboardSnapshot snapshot = DescribeKeyboardLayout(hkl);
        TEST_NOTE(std::format("HKL {:016X}: {}", hkl, Describe(snapshot)));
        CHECK_EQ(snapshot.state, ProviderState::Available);
        CHECK_FALSE(snapshot.shortLabel.empty());
        CHECK_FALSE(snapshot.layoutName.empty());
    }
}

AETHER_TEST("network provider: first snapshot and Stop") {
    Collector<NetworkSnapshot> collector;
    NetworkProvider network;
    REQUIRE(network.Start(collector.Listener()).ok());
    REQUIRE(collector.WaitFor(1, 5s));
    const NetworkSnapshot snapshot = collector.At(0);
    TEST_NOTE(Describe(snapshot));
    CHECK_NE(snapshot.state, ProviderState::Initializing);
    CHECK_NE(snapshot.state, ProviderState::Error);
    CHECK(snapshot.profileName.empty());
    if (snapshot.kind == ConnectionKind::None) {
        CHECK_EQ(snapshot.connectivity, InternetConnectivity::None);
    } else {
        CHECK_NE(snapshot.connectivity, InternetConnectivity::Unknown);
    }
    if (snapshot.signalBars) CHECK(*snapshot.signalBars <= 5);

    // Refresh() goes through the debounce and only publishes a change.
    network.Refresh();
    std::this_thread::sleep_for(800ms);
    TEST_NOTE(std::format("snapshots after Refresh: {}", collector.Count()));

    network.Stop();
    const std::size_t afterStop = collector.Count();
    network.Refresh();
    std::this_thread::sleep_for(800ms);
    CHECK_EQ(collector.Count(), afterStop);
    CHECK_FALSE(collector.HadConcurrentCalls());
}

AETHER_TEST("status providers: factory starts all four and StopAll silences them") {
    Collector<ClockSnapshot> clock;
    Collector<BatterySnapshot> battery;
    Collector<KeyboardSnapshot> keyboard;
    Collector<NetworkSnapshot> network;
    auto created = CreateStatusProviders(StatusProviderOptions{ClockResolution::Seconds, {}, {}, {}});
    REQUIRE(created.ok());
    StatusProviders& providers = created.value();
    REQUIRE(providers.clock != nullptr);
    REQUIRE(providers.battery != nullptr);
    REQUIRE(providers.keyboard != nullptr);
    REQUIRE(providers.network != nullptr);

    REQUIRE(providers.clock->Start(clock.Listener()).ok());
    REQUIRE(providers.battery->Start(battery.Listener()).ok());
    REQUIRE(providers.keyboard->Start(keyboard.Listener()).ok());
    REQUIRE(providers.network->Start(network.Listener()).ok());
    CHECK(clock.WaitFor(1, 5s));
    CHECK(battery.WaitFor(1, 5s));
    CHECK(keyboard.WaitFor(1, 5s));
    CHECK(network.WaitFor(1, 5s));

    // A second Start while running is refused.
    CHECK_FALSE(providers.clock->Start(clock.Listener()).ok());

    providers.StopAll();
    const std::size_t clockCount = clock.Count();
    const std::size_t batteryCount = battery.Count();
    const std::size_t keyboardCount = keyboard.Count();
    const std::size_t networkCount = network.Count();
    providers.clock->NotifyTimeChanged();
    providers.battery->Refresh();
    providers.keyboard->Refresh();
    providers.network->Refresh();
    std::this_thread::sleep_for(1500ms);
    CHECK_EQ(clock.Count(), clockCount);
    CHECK_EQ(battery.Count(), batteryCount);
    CHECK_EQ(keyboard.Count(), keyboardCount);
    CHECK_EQ(network.Count(), networkCount);
    providers.StopAll();  // idempotent
}

AETHER_TEST("status providers: Stop waits for a listener call in progress") {
    std::atomic<bool> inListener{false};
    std::atomic<int> calls{0};
    ClockProvider clock(ClockResolution::Seconds);
    REQUIRE(clock.Start([&](const ClockSnapshot&) {
                     inListener = true;
                     ++calls;
                     std::this_thread::sleep_for(300ms);  // a slow consumer
                     inListener = false;
                 })
                .ok());
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!inListener.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    REQUIRE(inListener.load());
    clock.Stop();
    CHECK_FALSE(inListener.load());  // Stop() returned only after the call finished
    const int afterStop = calls.load();
    std::this_thread::sleep_for(1200ms);
    CHECK_EQ(calls.load(), afterStop);
}
