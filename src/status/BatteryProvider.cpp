// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/BatteryProvider.h"

#include "diagnostics/Logger.h"
#include "status/BatteryMapping.h"
#include "status/ProviderHost.h"
#include "status/SnapshotUtilities.h"
#include "status/WinRtSupport.h"

#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.Power.h>

#include <cstdint>
#include <format>

namespace aether::status {
namespace {

namespace power = winrt::Windows::System::Power;
using winrt::Windows::Foundation::IInspectable;

// Change events can arrive in bursts (plugging in raises supply, status and time events together).
// Waiting briefly before reading avoids publishing a half-updated intermediate state.
constexpr DWORD kSettleDelayMs = 150;

// Reads every PowerManager property; a property that fails stays unknown and the first error is kept.
std::optional<PowerManagerReadings> ReadPowerManager(std::string& error) {
    using power::PowerManager;
    PowerManagerReadings readings;
    readings.battery = TryRead<RawBatteryStatus>([] { return PowerManager::BatteryStatus(); }, error);
    readings.supply = TryRead<RawPowerSupplyStatus>([] { return PowerManager::PowerSupplyStatus(); }, error);
    readings.remainingChargePercent = TryRead<int>([] { return PowerManager::RemainingChargePercent(); }, error);
    readings.remainingDischargeTime = TryRead<std::chrono::seconds>(
        [] { return std::chrono::duration_cast<std::chrono::seconds>(PowerManager::RemainingDischargeTime()); },
        error);
    readings.energySaver = TryRead<RawEnergySaverStatus>([] { return PowerManager::EnergySaverStatus(); }, error);
    if (!readings.battery && !readings.supply && !readings.remainingChargePercent && !readings.energySaver) {
        return std::nullopt;  // nothing usable: let the mapping fall back entirely
    }
    return readings;
}

std::optional<SystemPowerReadings> ReadSystemPower() noexcept {
    SYSTEM_POWER_STATUS status{};
    if (!::GetSystemPowerStatus(&status)) return std::nullopt;
    SystemPowerReadings readings;
    readings.acLineStatus = status.ACLineStatus;
    readings.batteryFlag = status.BatteryFlag;
    readings.batteryLifePercent = status.BatteryLifePercent;
    readings.systemStatusFlag = status.SystemStatusFlag;
    readings.batteryLifeTime = status.BatteryLifeTime;
    return readings;
}

// RAII set of PowerManager event registrations. Destroying it unregisters every handler.
struct PowerSubscriptions {
    power::PowerManager::BatteryStatusChanged_revoker battery;
    power::PowerManager::PowerSupplyStatusChanged_revoker supply;
    power::PowerManager::RemainingChargePercentChanged_revoker percent;
    power::PowerManager::RemainingDischargeTimeChanged_revoker dischargeTime;
    power::PowerManager::EnergySaverStatusChanged_revoker energySaver;
};

}  // namespace

struct BatteryProvider::Impl {
    explicit Impl(BatteryProviderOptions o) : options(o) {}

    void Run(HANDLE stopEvent, const std::shared_ptr<WakeSignal>& wake);
    void ReadAndPublish(bool usePowerManager, const std::string& subscriptionError);

    const BatteryProviderOptions options;
    ProviderState lastLoggedState = ProviderState::Initializing;  // provider thread only
    ProviderHost<BatterySnapshot> host;                           // last: joined first
};

void BatteryProvider::Impl::ReadAndPublish(bool usePowerManager, const std::string& subscriptionError) {
    BatteryReadings readings;
    std::string readError;
    if (usePowerManager) readings.powerManager = ReadPowerManager(readError);
    readings.systemPower = ReadSystemPower();
    readings.powerManagerError = !subscriptionError.empty() ? subscriptionError : readError;
    if (!options.usePowerManager) readings.powerManagerError = "disabled by configuration";

    const BatterySnapshot snapshot = MapBatteryReadings(readings);
    if (host.Publish(snapshot, &SameContent)) {
        if (snapshot.state != lastLoggedState) {
            AETHER_LOG_INFO("status.battery", "{}", Describe(snapshot));
            lastLoggedState = snapshot.state;
        } else {
            AETHER_LOG_DEBUG("status.battery", "{}", Describe(snapshot));
        }
    }
}

void BatteryProvider::Impl::Run(HANDLE stopEvent, const std::shared_ptr<WakeSignal>& wake) {
    lastLoggedState = ProviderState::Initializing;
    WinRtApartmentScope apartment;
    std::string subscriptionError;
    bool usePowerManager = options.usePowerManager && apartment.IsUsable();
    if (options.usePowerManager && !apartment.IsUsable()) {
        subscriptionError =
            std::format("CoInitializeEx failed (0x{:08X})", static_cast<std::uint32_t>(apartment.Status()));
    }

    PowerSubscriptions subscriptions;
    if (usePowerManager) {
        try {
            // The handlers only signal the shared wake event; they may run on any thread pool thread,
            // and possibly after revocation, which the shared_ptr makes safe.
            auto handler = [wake](const IInspectable&, const IInspectable&) { wake->Signal(); };
            using power::PowerManager;
            subscriptions.battery = PowerManager::BatteryStatusChanged(winrt::auto_revoke, handler);
            subscriptions.supply = PowerManager::PowerSupplyStatusChanged(winrt::auto_revoke, handler);
            subscriptions.percent = PowerManager::RemainingChargePercentChanged(winrt::auto_revoke, handler);
            subscriptions.dischargeTime = PowerManager::RemainingDischargeTimeChanged(winrt::auto_revoke, handler);
            subscriptions.energySaver = PowerManager::EnergySaverStatusChanged(winrt::auto_revoke, handler);
        } catch (const winrt::hresult_error& error) {
            subscriptionError = DescribeHResult(error);
            subscriptions = {};
            usePowerManager = false;
            AETHER_LOG_WARN("status.battery", "PowerManager unavailable, falling back to GetSystemPowerStatus: {}",
                            subscriptionError);
        }
    }

    const DWORD idleTimeout =
        usePowerManager ? INFINITE
                        : static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 options.fallbackRefreshInterval)
                                                 .count());
    ReadAndPublish(usePowerManager, subscriptionError);
    for (;;) {
        const DWORD signalled = WaitForAny({stopEvent, wake->Handle()}, idleTimeout);
        if (signalled == 0) break;
        if (signalled == WAIT_FAILED) {
            AETHER_LOG_ERROR("status.battery", "wait failed (error {})", ::GetLastError());
            BatterySnapshot failed;
            failed.state = ProviderState::Error;
            failed.detail = "internal wait failed";
            host.Publish(failed, &SameContent);
            ::WaitForSingleObject(stopEvent, INFINITE);
            break;
        }
        if (signalled == 1) {
            if (::WaitForSingleObject(stopEvent, kSettleDelayMs) == WAIT_OBJECT_0) break;
            ::ResetEvent(wake->Handle());  // events of the same burst are covered by the read below
        }
        ReadAndPublish(usePowerManager, subscriptionError);
    }
    // `subscriptions` is destroyed here, on the apartment thread, before COM is uninitialized.
}

BatteryProvider::BatteryProvider(BatteryProviderOptions options) : impl_(std::make_shared<Impl>(options)) {}

BatteryProvider::~BatteryProvider() { Stop(); }

Result<void> BatteryProvider::Start(Listener listener) {
    const auto impl = impl_;
    // Zero would busy-loop; larger values can wrap to a short wait or INFINITE when converted to
    // the DWORD millisecond timeout. Validate before starting any background work.
    constexpr std::chrono::seconds longestInterval{(INFINITE - 1ULL) / 1'000};
    if (impl->options.fallbackRefreshInterval <= std::chrono::seconds::zero() ||
        impl->options.fallbackRefreshInterval > longestInterval) {
        return MakeError(ErrorCode::InvalidArgument, "battery refresh interval is outside the finite wait range");
    }
    return impl->host.Start(std::move(listener), L"Aether status: battery",
                            [impl](HANDLE stop, const std::shared_ptr<WakeSignal>& wake) { impl->Run(stop, wake); });
}

void BatteryProvider::Stop() noexcept { impl_->host.Stop(); }

BatterySnapshot BatteryProvider::Current() const { return impl_->host.Current(); }

void BatteryProvider::Refresh() noexcept { impl_->host.Wake(); }

}  // namespace aether::status
