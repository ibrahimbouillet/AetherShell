// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "status/StatusProviders.h"
#include "status/WinRtSupport.h"

#include <objbase.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace aether::status;
using namespace std::chrono_literals;

namespace {

template <class Predicate>
bool WaitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

template <class Provider>
void DestroyFromListener() {
    std::atomic<bool> destroyed{false};
    auto provider = std::make_unique<Provider>();
    REQUIRE(provider->Start([&](const auto&) { provider.reset(); destroyed = true; }).ok());
    REQUIRE(WaitUntil([&] { return destroyed.load(); }));
    CHECK(provider == nullptr);
}

}  // namespace

AETHER_TEST("status providers: each public owner may be destroyed by its first listener") {
    DestroyFromListener<ClockProvider>();
    DestroyFromListener<BatteryProvider>();
    DestroyFromListener<KeyboardProvider>();
    DestroyFromListener<NetworkProvider>();
}

AETHER_TEST("status WinRT: provider MTA scopes and factories survive repeated restart") {
    HRESULT before = E_PENDING;
    HRESULT after = E_PENDING;
    HRESULT inside = E_PENDING;
    HRESULT initialized = E_PENDING;
    APTTYPE type = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    std::jthread apartment([&] {
        before = ::CoGetApartmentType(&type, &qualifier);
        {
            WinRtApartmentScope scope;
            initialized = scope.Status();
            inside = ::CoGetApartmentType(&type, &qualifier);
        }
        after = ::CoGetApartmentType(&type, &qualifier);
    });
    apartment.join();
    CHECK(SUCCEEDED(initialized));
    CHECK(SUCCEEDED(inside));
    // Provider scopes leave the explicitly owned shared runtime alive between sessions. Its final
    // balanced release is checked in an isolated process by status_runtime_shutdown_tests.
    CHECK(SUCCEEDED(after));
    (void)before;

    for (int round = 0; round < 3; ++round) {
        std::atomic<bool> powerRead{false};
        std::atomic<bool> networkRead{false};
        BatteryProvider battery;
        NetworkProvider network;
        REQUIRE(battery.Start([&](const BatterySnapshot& s) { powerRead = s.state != ProviderState::Initializing; }).ok());
        REQUIRE(network.Start([&](const NetworkSnapshot& s) { networkRead = s.state != ProviderState::Initializing; }).ok());
        REQUIRE(WaitUntil([&] { return powerRead.load() && networkRead.load(); }));
        CHECK_NE(battery.Current().state, ProviderState::Error);
        CHECK_NE(network.Current().state, ProviderState::Error);
        battery.Stop();
        network.Stop();
    }
}
