// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/StatusProviders.h"

#include <exception>
#include <string>

namespace aether::status {

void StatusProviders::StopAll() noexcept {
    // Each Stop() closes the listener first and then joins its thread; the order is irrelevant.
    if (network) network->Stop();
    if (keyboard) keyboard->Stop();
    if (battery) battery->Stop();
    if (clock) clock->Stop();
}

Result<StatusProviders> CreateStatusProviders(const StatusProviderOptions& options) {
    try {
        StatusProviders providers;
        providers.clock = std::make_unique<ClockProvider>(options.clockResolution);
        providers.battery = std::make_unique<BatteryProvider>(options.battery);
        providers.keyboard = std::make_unique<KeyboardProvider>(options.keyboard);
        providers.network = std::make_unique<NetworkProvider>(options.network);
        return providers;
    } catch (const std::exception& ex) {
        return MakeError(ErrorCode::Internal, std::string("cannot create the status providers: ") + ex.what());
    }
}

}  // namespace aether::status
