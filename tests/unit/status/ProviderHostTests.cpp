// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "core/FileSystem.h"

#include "status/ProviderHost.h"
#include "status/SnapshotPublisher.h"
#include "status/SnapshotUtilities.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace aether;
using namespace aether::status;
using namespace std::chrono_literals;

namespace {

BatterySnapshot Battery(int percent) {
    BatterySnapshot snapshot;
    snapshot.state = ProviderState::Available;
    snapshot.batteryPresent = true;
    snapshot.percent = percent;
    return snapshot;
}

template <class Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

}  // namespace

AETHER_TEST("publisher: delivers, deduplicates and tracks the current snapshot") {
    SnapshotPublisher<BatterySnapshot> publisher;
    CHECK_FALSE(publisher.Publish(Battery(50)));  // not open yet

    int calls = 0;
    int lastPercent = -1;
    publisher.Open([&](const BatterySnapshot& s) {
        ++calls;
        lastPercent = s.percent.value_or(-1);
    });
    CHECK_EQ(publisher.Current().state, ProviderState::Initializing);

    CHECK(publisher.Publish(Battery(50), &SameContent));
    CHECK_FALSE(publisher.Publish(Battery(50), &SameContent));  // identical: suppressed
    CHECK(publisher.Publish(Battery(49), &SameContent));
    CHECK(publisher.Publish(Battery(49)));  // no comparison requested: delivered
    CHECK_EQ(calls, 3);
    CHECK_EQ(lastPercent, 49);
    CHECK_EQ(publisher.Current().percent, std::optional<int>(49));

    publisher.Close();
    CHECK_FALSE(publisher.IsOpen());
    CHECK_FALSE(publisher.Publish(Battery(10)));
    CHECK_EQ(calls, 3);
}

AETHER_TEST("publisher: a throwing listener does not escape") {
    SnapshotPublisher<BatterySnapshot> publisher;
    publisher.Open([](const BatterySnapshot&) { throw std::runtime_error("consumer bug"); });
    CHECK(publisher.Publish(Battery(1)));
    CHECK_EQ(publisher.Current().percent, std::optional<int>(1));
}

AETHER_TEST("publisher: Close from inside the listener does not deadlock") {
    SnapshotPublisher<BatterySnapshot> publisher;
    int calls = 0;
    publisher.Open([&](const BatterySnapshot&) {
        ++calls;
        publisher.Close();
    });
    CHECK(publisher.Publish(Battery(1)));
    CHECK_FALSE(publisher.Publish(Battery(2)));
    CHECK_EQ(calls, 1);
}

AETHER_TEST("publisher: Close waits for a listener running on another thread") {
    SnapshotPublisher<BatterySnapshot> publisher;
    std::atomic<bool> entered{false};
    std::atomic<bool> finished{false};
    publisher.Open([&](const BatterySnapshot&) {
        entered = true;
        std::this_thread::sleep_for(200ms);
        finished = true;
    });
    std::jthread producer([&] { publisher.Publish(Battery(1)); });
    REQUIRE(WaitUntil([&] { return entered.load(); }));
    publisher.Close();
    CHECK(finished.load());  // Close() returned only after the listener did
    producer.join();
}

AETHER_TEST("provider host: runs the body on its own thread and stops it") {
    std::atomic<int> calls{0};
    std::atomic<int> wakes{0};
    std::atomic<bool> exited{false};
    const auto testThread = std::this_thread::get_id();
    std::atomic<bool> ranElsewhere{false};
    std::atomic<int> second{0};
    ProviderHost<BatterySnapshot> host;
    CHECK_EQ(host.Start(nullptr, L"test", [](HANDLE, const std::shared_ptr<WakeSignal>&) {}).error().code,
             ErrorCode::InvalidArgument);
    auto started = host.Start(
        [&](const BatterySnapshot&) { ++calls; },
        L"Aether status test",
        [&](HANDLE stop, const std::shared_ptr<WakeSignal>& wake) {
            ranElsewhere = std::this_thread::get_id() != testThread;
            host.Publish(Battery(10), &SameContent);
            for (;;) {
                const DWORD signalled = WaitForAny({stop, wake->Handle()}, INFINITE);
                if (signalled != 1) break;
                ++wakes;
                host.Publish(Battery(10 + wakes.load()), &SameContent);
            }
            exited = true;
        });
    REQUIRE(started.ok());
    CHECK_EQ(host.Start([](const BatterySnapshot&) {}, L"again", [](HANDLE, const std::shared_ptr<WakeSignal>&) {})
                 .error()
                 .code,
             ErrorCode::InvalidState);

    REQUIRE(WaitUntil([&] { return calls.load() == 1; }));
    CHECK(ranElsewhere.load());
    host.Wake();
    REQUIRE(WaitUntil([&] { return calls.load() == 2; }));
    CHECK_EQ(host.Current().percent, std::optional<int>(11));

    host.Stop();
    CHECK(exited.load());
    const int afterStop = calls.load();
    host.Wake();  // harmless after Stop
    std::this_thread::sleep_for(50ms);
    CHECK_EQ(calls.load(), afterStop);
    host.Stop();  // idempotent

    // A stopped host can be started again.
    REQUIRE(host.Start([&](const BatterySnapshot&) { ++second; }, L"Aether status test 2",
                       [&](HANDLE stop, const std::shared_ptr<WakeSignal>&) {
                           host.Publish(Battery(99));
                           ::WaitForSingleObject(stop, INFINITE);
                       })
                .ok());
    REQUIRE(WaitUntil([&] { return second.load() == 1; }));
    host.Stop();
}

AETHER_TEST("provider host: Stop from inside the listener is tolerated") {
    std::atomic<int> calls{0};
    ProviderHost<BatterySnapshot> host;
    REQUIRE(host.Start(
                    [&](const BatterySnapshot&) {
                        ++calls;
                        host.Stop();  // cannot join itself: closes delivery and signals the thread
                    },
                    L"Aether status test",
                    [&](HANDLE stop, const std::shared_ptr<WakeSignal>&) {
                        host.Publish(Battery(1));
                        host.Publish(Battery(2));  // closed by the listener: not delivered
                        ::WaitForSingleObject(stop, INFINITE);
                    })
                .ok());
    REQUIRE(WaitUntil([&] { return calls.load() >= 1; }));
    host.Stop();  // completes the join
    CHECK_EQ(calls.load(), 1);
}

AETHER_TEST("provider host: an exception in the body becomes an Error snapshot") {
    std::atomic<int> calls{0};
    ProviderHost<BatterySnapshot> host;
    REQUIRE(host.Start([&](const BatterySnapshot&) { ++calls; }, L"Aether status test",
                       [&](HANDLE, const std::shared_ptr<WakeSignal>&) {
                           host.Publish(Battery(5));
                           throw std::runtime_error("simulated failure");
                       })
                .ok());
    REQUIRE(WaitUntil([&] { return calls.load() == 2; }));
    const BatterySnapshot failed = host.Current();
    CHECK_EQ(failed.state, ProviderState::Error);
    CHECK_EQ(failed.detail, std::string("status provider failed"));
    CHECK_FALSE(failed.percent.has_value());  // the last good value is not carried into the error
    host.Stop();  // the thread idles until Stop and joins normally
    CHECK_EQ(calls.load(), 2);

    // Snapshot types without a detail field (the clock) are supported too.
    std::atomic<int> clockCalls{0};
    ProviderHost<ClockSnapshot> clockHost;
    REQUIRE(clockHost.Start([&](const ClockSnapshot&) { ++clockCalls; }, L"Aether status test",
                            [](HANDLE, const std::shared_ptr<WakeSignal>&) { throw 42; })
                .ok());
    REQUIRE(WaitUntil([&] { return clockCalls.load() == 1; }));
    CHECK_EQ(clockHost.Current().state, ProviderState::Error);
    clockHost.Stop();
}

AETHER_TEST("provider host: concurrent stop and callback stop never hold a lifecycle lock while joining") {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> callbackStopped{false};
    std::atomic<bool> firstStopped{false};
    std::atomic<bool> secondStopped{false};
    std::atomic<bool> restartReturned{false};
    std::atomic<bool> restartRejected{false};
    std::atomic<HANDLE> stopHandle{nullptr};
    ProviderHost<BatterySnapshot> host;
    REQUIRE(host.Start([&](const BatterySnapshot&) {
                entered = true;
                // Bounded even on an assertion failure, so test teardown can always join.
                WaitUntil([&] { return release.load(); }, 3s);
                host.Stop();
                callbackStopped = true;
            }, L"Aether lifecycle race", [&](HANDLE stop, const std::shared_ptr<WakeSignal>&) {
                stopHandle = stop;
                host.Publish(Battery(20));
                ::WaitForSingleObject(stop, INFINITE);
            }).ok());
    REQUIRE(WaitUntil([&] { return entered.load(); }));
    std::jthread first([&] { host.Stop(); firstStopped = true; });
    REQUIRE(WaitUntil([&] { return ::WaitForSingleObject(stopHandle.load(), 0) == WAIT_OBJECT_0; }));
    std::jthread second([&] { host.Stop(); secondStopped = true; });
    std::jthread restart([&] {
        const auto started = host.Start([](const BatterySnapshot&) {}, L"Aether overlapping start",
            [](HANDLE stop, const std::shared_ptr<WakeSignal>&) { ::WaitForSingleObject(stop, INFINITE); });
        restartRejected = !started && started.error().code == ErrorCode::InvalidState;
        restartReturned = true;
    });
    CHECK(WaitUntil([&] { return restartReturned.load(); }, 1s));
    CHECK(restartRejected.load());
    CHECK_FALSE(firstStopped.load());
    CHECK_FALSE(secondStopped.load());
    release = true;
    first.join();
    second.join();
    restart.join();
    CHECK(callbackStopped.load());
    CHECK(firstStopped.load());
    CHECK(secondStopped.load());
    CHECK_EQ(host.Current().percent, std::optional<int>(20));
    REQUIRE(host.Start([](const BatterySnapshot&) {}, L"Aether restart after join",
        [](HANDLE stop, const std::shared_ptr<WakeSignal>&) { ::WaitForSingleObject(stop, INFINITE); }).ok());
    host.Stop();
}

AETHER_TEST("provider host: destruction inside its listener retains worker session until unwind") {
    std::atomic<bool> destroyed{false};
    std::atomic<bool> bodyExited{false};
    auto lease = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = lease;
    auto host = std::make_unique<ProviderHost<BatterySnapshot>>();
    auto* raw = host.get();
    auto body = [&, lease](HANDLE stop, const std::shared_ptr<WakeSignal>&) {
        raw->Publish(Battery(5));
        ::WaitForSingleObject(stop, INFINITE);
        bodyExited = true;
    };
    lease.reset();
    REQUIRE(raw->Start([&](const BatterySnapshot&) { host.reset(); destroyed = true; },
        L"Aether self destruction", std::move(body)).ok());
    REQUIRE(WaitUntil([&] { return destroyed.load() && bodyExited.load() && weak.expired(); }));
    CHECK(host == nullptr);
}

AETHER_TEST("snapshot utilities: comparisons and descriptions") {
    ClockSnapshot a;
    a.state = ProviderState::Available;
    a.hour = 14;
    ClockSnapshot b = a;
    CHECK(SameContent(a, b));
    b.timeZoneName = L"Other";
    CHECK_FALSE(SameContent(a, b));

    KeyboardSnapshot k1;
    k1.state = ProviderState::Available;
    k1.shortLabel = L"FR";
    KeyboardSnapshot k2 = k1;
    CHECK(SameContent(k1, k2));
    k2.fromForegroundWindow = true;
    CHECK_FALSE(SameContent(k1, k2));

    BatterySnapshot unknown;
    unknown.state = ProviderState::Available;
    unknown.batteryPresent = true;
    CHECK(Describe(unknown).find("unknown%") != std::string::npos);
    CHECK_EQ(ToString(ProviderState::Unavailable), std::string_view("Unavailable"));
    CHECK_EQ(ToString(ChargeState::NotCharging), std::string_view("NotCharging"));
    CHECK_EQ(ToString(InternetConnectivity::Constrained), std::string_view("Constrained"));
}

AETHER_TEST("status diagnostics: listener and body exception payloads are never logged") {
    struct ShutdownLog { ~ShutdownLog() { diag::Logger::Shutdown(); } } shutdown;
    diag::LoggerOptions options;
    options.directory=test::MakeTempDirectory("status-log-privacy");
    options.baseName=L"status"; options.writeToDebugger=false;
    diag::Logger::Initialize(options);
    SnapshotPublisher<BatterySnapshot> publisher;
    publisher.Open([](const BatterySnapshot&) { throw std::runtime_error("synthetic-private-listener-marker"); });
    CHECK(publisher.Publish(Battery(1))); publisher.Close();
    std::atomic<bool> reported{false};
    ProviderHost<BatterySnapshot> host;
    REQUIRE(host.Start([&](const BatterySnapshot&) { reported=true; },L"Aether private-exception test",
        [](HANDLE,const std::shared_ptr<WakeSignal>&) { throw std::runtime_error("synthetic-private-body-marker"); }).ok());
    REQUIRE(WaitUntil([&] { return reported.load(); }));
    host.Stop(); diag::Logger::Shutdown();
    const auto contents=fs::ReadFile(options.directory/L"status.log",64*1024); REQUIRE(contents.ok());
    CHECK(contents->find("synthetic-private")==std::string::npos);
    CHECK(contents->find("status listener threw an exception")!=std::string::npos);
    CHECK(contents->find("status provider body failed")!=std::string::npos);
    CHECK_EQ(host.Current().detail,std::string("status provider failed"));
    std::filesystem::remove_all(options.directory);
}
