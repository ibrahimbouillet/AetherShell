// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Network provider built on Windows.Networking.Connectivity. A dedicated MTA thread subscribes to
// NetworkInformation::NetworkStatusChanged and queries GetInternetConnectionProfile() (connectivity
// level, WLAN/WWAN, adapter IANA type, signal bars) once at start and after every burst of changes.
//
// NetworkStatusChanged arrives in bursts (an adapter coming up raises several events within a few
// hundred milliseconds), so the query runs only once no further event has arrived for `debounce`
// (bounded by `maxDebounce` so a storm of events cannot starve it). There is no polling loop: between
// events the thread sleeps on kernel objects.
//
// Privacy: the Wi-Fi SSID / profile name is never read (Windows 11 24H2+ treats it as location data);
// NetworkSnapshot::profileName stays empty.
#pragma once

#include "status/StatusTypes.h"

#include <chrono>
#include <memory>

namespace aether::status {

struct NetworkProviderOptions {
    // Start() requires 0 < debounce <= maxDebounce <= 60 s.
    std::chrono::milliseconds debounce{300};
    std::chrono::milliseconds maxDebounce{2'000};
};

class NetworkProvider final : public IStatusProvider<NetworkSnapshot> {
public:
    explicit NetworkProvider(NetworkProviderOptions options = {});
    ~NetworkProvider() override;
    NetworkProvider(const NetworkProvider&) = delete;
    NetworkProvider& operator=(const NetworkProvider&) = delete;

    Result<void> Start(Listener listener) override;
    void Stop() noexcept override;
    [[nodiscard]] NetworkSnapshot Current() const override;

    // Thread-safe. Re-queries after the debounce delay (for example after resume from sleep).
    void Refresh() noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace aether::status
