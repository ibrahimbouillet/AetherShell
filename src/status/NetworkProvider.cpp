// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/NetworkProvider.h"

#include "diagnostics/Logger.h"
#include "status/NetworkMapping.h"
#include "status/ProviderHost.h"
#include "status/SnapshotUtilities.h"
#include "status/WinRtSupport.h"

#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Connectivity.h>

#include <algorithm>
#include <cstdint>
#include <format>

namespace aether::status {
namespace {

namespace connectivity = winrt::Windows::Networking::Connectivity;

// One query of the Internet connection profile. Runs on the provider thread only: some of these calls
// block for a noticeable time (connectivity level evaluation), which is why they never run on the UI thread.
NetworkReadings QueryNetwork() {
    NetworkReadings readings;
    connectivity::ConnectionProfile profile{nullptr};
    try {
        profile = connectivity::NetworkInformation::GetInternetConnectionProfile();
    } catch (const winrt::hresult_error& error) {
        readings.queryFailed = true;
        readings.error = "GetInternetConnectionProfile failed: " + DescribeHResult(error);
        return readings;
    }
    if (!profile) return readings;  // no profile routes to the Internet
    readings.hasInternetProfile = true;

    // ProfileName (the SSID for Wi-Fi) is intentionally never read: see NetworkMapping.h.
    readings.connectivity =
        TryRead<RawConnectivityLevel>([&] { return profile.GetNetworkConnectivityLevel(); }, readings.error);
    readings.isWlan = TryRead<bool>([&] { return profile.IsWlanConnectionProfile(); }, readings.error);
    readings.isWwan = TryRead<bool>([&] { return profile.IsWwanConnectionProfile(); }, readings.error);
    // NetworkAdapter() may be null (for example for some VPN profiles).
    if (auto iana = TryRead<std::optional<std::uint32_t>>(
            [&]() -> std::optional<std::uint32_t> {
                const auto adapter = profile.NetworkAdapter();
                if (!adapter) return std::nullopt;
                return adapter.IanaInterfaceType();
            },
            readings.error)) {
        readings.ianaInterfaceType = *iana;
    }
    // GetSignalBars() returns a null reference when Windows has no value (wired, or not reported).
    if (auto bars = TryRead<std::optional<std::uint8_t>>(
            [&]() -> std::optional<std::uint8_t> {
                const auto value = profile.GetSignalBars();
                if (!value) return std::nullopt;
                return value.Value();
            },
            readings.error)) {
        readings.signalBars = *bars;
    }
    return readings;
}

DWORD ToWaitMs(std::chrono::milliseconds value) noexcept {
    return static_cast<DWORD>(std::clamp<std::int64_t>(value.count(), 0, 60'000));
}

}  // namespace

struct NetworkProvider::Impl {
    explicit Impl(NetworkProviderOptions o) : options(o) {}

    void Run(HANDLE stopEvent, const std::shared_ptr<WakeSignal>& wake);
    void QueryAndPublish();
    // Waits until no wake signal arrived for `debounce` (at most `maxDebounce`). False when stopping.
    bool Debounce(HANDLE stopEvent, HANDLE wakeEvent) const;

    const NetworkProviderOptions options;
    ProviderState lastLoggedState = ProviderState::Initializing;  // provider thread only
    ProviderHost<NetworkSnapshot> host;                           // last: joined first
};

void NetworkProvider::Impl::QueryAndPublish() {
    const NetworkSnapshot snapshot = MapNetworkReadings(QueryNetwork());
    if (host.Publish(snapshot, &SameContent)) {
        // Describe() never contains a profile name.
        if (snapshot.state != lastLoggedState) {
            AETHER_LOG_INFO("status.network", "{}", Describe(snapshot));
            lastLoggedState = snapshot.state;
        } else {
            AETHER_LOG_DEBUG("status.network", "{}", Describe(snapshot));
        }
    }
}

bool NetworkProvider::Impl::Debounce(HANDLE stopEvent, HANDLE wakeEvent) const {
    const auto deadline = std::chrono::steady_clock::now() + options.maxDebounce;
    for (;;) {
        using std::chrono::milliseconds;
        const auto left = std::chrono::duration_cast<milliseconds>(deadline - std::chrono::steady_clock::now());
        const DWORD timeout = ToWaitMs(std::min(options.debounce, std::max(left, milliseconds::zero())));
        const DWORD result = WaitForAny({stopEvent, wakeEvent}, timeout);
        if (result == 0) return false;
        if (result == 1 && std::chrono::steady_clock::now() < deadline) continue;  // still bursting
        if (result == WAIT_FAILED) {
            // Do not spin; the caller queries anyway.
            return ::WaitForSingleObject(stopEvent, ToWaitMs(options.debounce)) != WAIT_OBJECT_0;
        }
        return true;  // quiet period elapsed, or the burst hit maxDebounce
    }
}

void NetworkProvider::Impl::Run(HANDLE stopEvent, const std::shared_ptr<WakeSignal>& wake) {
    lastLoggedState = ProviderState::Initializing;
    WinRtApartmentScope apartment;
    if (!apartment.IsUsable()) {
        NetworkSnapshot failed;
        failed.state = ProviderState::Error;
        failed.detail = std::format("CoInitializeEx failed (0x{:08X})", static_cast<std::uint32_t>(apartment.Status()));
        AETHER_LOG_ERROR("status.network", "{}", failed.detail);
        host.Publish(failed);
        ::WaitForSingleObject(stopEvent, INFINITE);
        return;
    }

    connectivity::NetworkInformation::NetworkStatusChanged_revoker subscription;
    try {
        // The handler runs on a thread-pool thread, possibly after revocation; it only signals the
        // shared wake event, which the captured shared_ptr keeps alive.
        subscription = connectivity::NetworkInformation::NetworkStatusChanged(
            winrt::auto_revoke, [wake](const winrt::Windows::Foundation::IInspectable&) { wake->Signal(); });
    } catch (const winrt::hresult_error& error) {
        NetworkSnapshot failed;
        failed.state = ProviderState::Error;
        failed.detail = "cannot subscribe to NetworkStatusChanged: " + DescribeHResult(error);
        AETHER_LOG_ERROR("status.network", "{}", failed.detail);
        host.Publish(failed);
        ::WaitForSingleObject(stopEvent, INFINITE);
        return;
    }

    QueryAndPublish();
    for (;;) {
        const DWORD result = WaitForAny({stopEvent, wake->Handle()}, INFINITE);
        if (result == 0) break;
        if (result == WAIT_FAILED) {
            AETHER_LOG_ERROR("status.network", "wait failed (error {})", ::GetLastError());
            NetworkSnapshot failed;
            failed.state = ProviderState::Error;
            failed.detail = "internal wait failed";
            host.Publish(failed, &SameContent);
            ::WaitForSingleObject(stopEvent, INFINITE);
            break;
        }
        if (!Debounce(stopEvent, wake->Handle())) break;
        QueryAndPublish();
    }
    // `subscription` is revoked here, on the apartment thread, before COM is uninitialized.
}

NetworkProvider::NetworkProvider(NetworkProviderOptions options) : impl_(std::make_shared<Impl>(options)) {}

NetworkProvider::~NetworkProvider() { Stop(); }

Result<void> NetworkProvider::Start(Listener listener) {
    const auto impl = impl_;
    // Bound both the individual wait and the steady-clock deadline arithmetic. Invalid durations
    // must not silently turn debouncing into a hot loop or overflow the deadline.
    if (impl->options.debounce <= std::chrono::milliseconds::zero() ||
        impl->options.maxDebounce < impl->options.debounce ||
        impl->options.maxDebounce > std::chrono::minutes(1)) {
        return MakeError(ErrorCode::InvalidArgument, "network debounce requires 0 < debounce <= maxDebounce <= 60 s");
    }
    return impl->host.Start(std::move(listener), L"Aether status: network",
                            [impl](HANDLE stop, const std::shared_ptr<WakeSignal>& wake) { impl->Run(stop, wake); });
}

void NetworkProvider::Stop() noexcept { impl_->host.Stop(); }

NetworkSnapshot NetworkProvider::Current() const { return impl_->host.Current(); }

void NetworkProvider::Refresh() noexcept { impl_->host.Wake(); }

}  // namespace aether::status
