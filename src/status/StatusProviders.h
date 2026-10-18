// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Single entry point for the Top Status Bar: creates the clock, battery, input language and network
// providers with their options. The providers are returned unstarted; the caller starts each one with
// its listener (marshalling to the UI thread with IUiDispatcher) and forwards system notifications:
//
//   SystemEvents::TimeChanged                    -> clock->NotifyTimeChanged()
//   SystemEvents::SettingChanged("intl")         -> clock->NotifyTimeChanged(), keyboard->Refresh()
//   SystemEvents::ResumedFromSleep               -> clock->NotifyTimeChanged(), battery->Refresh(),
//                                                   network->Refresh()
//   SystemEvents::PowerSettingChanged            -> battery->Refresh()
//
// The concrete types are returned (not IStatusProvider<...>) because those notification hooks are not
// part of the shared contract.
#pragma once

#include "core/Result.h"
#include "status/BatteryProvider.h"
#include "status/ClockProvider.h"
#include "status/KeyboardProvider.h"
#include "status/NetworkProvider.h"

#include <memory>

namespace aether::status {

struct StatusProviderOptions {
    ClockResolution clockResolution = ClockResolution::Minutes;
    BatteryProviderOptions battery;
    KeyboardProviderOptions keyboard;
    NetworkProviderOptions network;
};

struct StatusProviders {
    std::unique_ptr<ClockProvider> clock;
    std::unique_ptr<BatteryProvider> battery;
    std::unique_ptr<KeyboardProvider> keyboard;
    std::unique_ptr<NetworkProvider> network;

    // Stops every provider (idempotent, noexcept). After it returns no listener is called again.
    void StopAll() noexcept;
};

// Creates the four providers. Nothing is started and nothing blocks, so this is safe on the UI thread.
[[nodiscard]] Result<StatusProviders> CreateStatusProviders(const StatusProviderOptions& options = {});

}  // namespace aether::status
