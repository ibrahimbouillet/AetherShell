// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "status/BatteryProvider.h"
#include "status/NetworkProvider.h"

#include <atomic>
#include <chrono>

using namespace aether;
using namespace aether::status;
using namespace std::chrono_literals;

AETHER_TEST("battery provider: rejects refresh intervals that would spin or overflow a Win32 wait") {
    for (const auto interval : {0s, -1s, 4'294'968s, std::chrono::seconds::max()}) {
        // The validation applies to the fallback even when the primary source is requested.
        for (const bool usePowerManager : {false, true}) {
            BatteryProvider provider(BatteryProviderOptions{usePowerManager, interval});
            std::atomic<int> calls{0};
            const auto started = provider.Start([&](const BatterySnapshot&) { ++calls; });
            CHECK_FALSE(started.ok());
            if (!started) CHECK_EQ(started.error().code, ErrorCode::InvalidArgument);
            provider.Stop();
            CHECK_EQ(calls.load(), 0);
            CHECK_EQ(provider.Current().state, ProviderState::Initializing);
        }
    }
}

AETHER_TEST("network provider: rejects invalid debounce deadlines before starting a worker") {
    for (const auto options : {NetworkProviderOptions{0ms, 2s}, NetworkProviderOptions{-1ms, 2s},
                               NetworkProviderOptions{300ms, 299ms}, NetworkProviderOptions{300ms, -1ms},
                               NetworkProviderOptions{300ms, 60'001ms},
                               NetworkProviderOptions{300ms, std::chrono::milliseconds::max()}}) {
        NetworkProvider provider(options);
        std::atomic<int> calls{0};
        const auto started = provider.Start([&](const NetworkSnapshot&) { ++calls; });
        CHECK_FALSE(started.ok());
        if (!started) CHECK_EQ(started.error().code, ErrorCode::InvalidArgument);
        provider.Stop();
        CHECK_EQ(calls.load(), 0);
        CHECK_EQ(provider.Current().state, ProviderState::Initializing);
    }
}
