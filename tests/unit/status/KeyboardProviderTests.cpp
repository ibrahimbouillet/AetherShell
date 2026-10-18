// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// KeyboardProvider driven by an injected reading source: change detection, deduplication, error
// reporting and Refresh(), independent of the real foreground window.
#include "AetherTest.h"

#include "status/KeyboardProvider.h"
#include "status/SnapshotUtilities.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::status;
using namespace std::chrono_literals;

namespace {

constexpr std::uintptr_t kFrench = 0x040C040C;   // fr-FR, French layout
constexpr std::uintptr_t kEnglish = 0x04090409;  // en-US, US layout

// Thread-safe stand-in for the foreground window's input locale.
class FakeInputLocale {
public:
    void Set(std::uintptr_t hkl, bool foreground = true) {
        std::lock_guard lock(mutex_);
        reading_ = InputLocaleReading{hkl, foreground};
    }
    InputLocaleReading Read() {
        ++reads_;
        std::lock_guard lock(mutex_);
        return reading_;
    }
    int Reads() const { return reads_.load(); }

private:
    std::mutex mutex_;
    InputLocaleReading reading_{kFrench, true};
    std::atomic<int> reads_{0};
};

class Recorder {
public:
    KeyboardProvider::Listener Listener() {
        return [this](const KeyboardSnapshot& snapshot) {
            std::lock_guard lock(mutex_);
            snapshots_.push_back(snapshot);
        };
    }
    std::size_t Count() const {
        std::lock_guard lock(mutex_);
        return snapshots_.size();
    }
    KeyboardSnapshot Last() const {
        std::lock_guard lock(mutex_);
        return snapshots_.back();
    }

private:
    mutable std::mutex mutex_;
    std::vector<KeyboardSnapshot> snapshots_;
};

template <class Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

KeyboardProviderOptions FakeOptions(FakeInputLocale& fake, std::chrono::milliseconds interval) {
    KeyboardProviderOptions options;
    options.checkInterval = interval;
    options.readInputLocale = [&fake] { return fake.Read(); };
    return options;
}

}  // namespace

AETHER_TEST("keyboard provider: publishes the first reading and every change, nothing else") {
    FakeInputLocale fake;
    Recorder recorder;
    KeyboardProvider provider(FakeOptions(fake, 50ms));
    REQUIRE(provider.Start(recorder.Listener()).ok());
    REQUIRE(WaitUntil([&] { return recorder.Count() == 1; }));
    KeyboardSnapshot first = recorder.Last();
    CHECK_EQ(first.state, ProviderState::Available);
    CHECK_EQ(first.shortLabel, std::wstring(L"FR"));
    CHECK_EQ(first.localeName, std::wstring(L"fr-FR"));
    CHECK(first.fromForegroundWindow);

    // Unchanged readings on every timer tick: no further snapshot.
    const int readsBefore = fake.Reads();
    std::this_thread::sleep_for(300ms);
    CHECK(fake.Reads() > readsBefore);  // the timer keeps comparing
    CHECK_EQ(recorder.Count(), 1u);

    fake.Set(kEnglish);
    REQUIRE(WaitUntil([&] { return recorder.Count() == 2; }));
    CHECK_EQ(recorder.Last().shortLabel, std::wstring(L"EN"));
    CHECK_EQ(recorder.Last().localeName, std::wstring(L"en-US"));
    CHECK_EQ(provider.Current().localeName, std::wstring(L"en-US"));

    // Same language, but read from the fallback (no foreground window): that is a visible change.
    fake.Set(kEnglish, false);
    REQUIRE(WaitUntil([&] { return recorder.Count() == 3; }));
    CHECK_FALSE(recorder.Last().fromForegroundWindow);

    // An unreadable input locale is an error, never an invented language.
    fake.Set(0, false);
    REQUIRE(WaitUntil([&] { return recorder.Count() == 4; }));
    CHECK_EQ(recorder.Last().state, ProviderState::Error);
    CHECK(recorder.Last().shortLabel.empty());
    CHECK_FALSE(recorder.Last().detail.empty());

    fake.Set(kFrench);
    REQUIRE(WaitUntil([&] { return recorder.Count() == 5; }));
    CHECK_EQ(recorder.Last().state, ProviderState::Available);

    provider.Stop();
    const std::size_t afterStop = recorder.Count();
    fake.Set(kEnglish);
    std::this_thread::sleep_for(200ms);
    CHECK_EQ(recorder.Count(), afterStop);
    provider.Stop();  // idempotent
}

AETHER_TEST("keyboard provider: Refresh re-reads at once without duplicating snapshots") {
    FakeInputLocale fake;
    Recorder recorder;
    // A one-minute comparison period: anything observed below came from Refresh().
    KeyboardProvider provider(FakeOptions(fake, 60s));
    REQUIRE(provider.Start(recorder.Listener()).ok());
    REQUIRE(WaitUntil([&] { return recorder.Count() == 1; }));

    const int readsBefore = fake.Reads();
    provider.Refresh();
    REQUIRE(WaitUntil([&] { return fake.Reads() > readsBefore; }, 1s));
    std::this_thread::sleep_for(100ms);
    CHECK_EQ(recorder.Count(), 1u);  // same reading: deduplicated

    fake.Set(kEnglish);
    provider.Refresh();
    REQUIRE(WaitUntil([&] { return recorder.Count() == 2; }, 1s));
    CHECK_EQ(recorder.Last().shortLabel, std::wstring(L"EN"));
    provider.Stop();
}

AETHER_TEST("keyboard provider: restart and a throwing reading source") {
    FakeInputLocale fake;
    Recorder restartedRecorder;
    KeyboardProvider provider(FakeOptions(fake, 50ms));
    for (int round = 0; round < 2; ++round) {
        REQUIRE(provider.Start(restartedRecorder.Listener()).ok());
        REQUIRE(WaitUntil([&] { return restartedRecorder.Count() == static_cast<std::size_t>(round + 1); }));
        CHECK_EQ(restartedRecorder.Last().state, ProviderState::Available);
        provider.Stop();
    }

    KeyboardProviderOptions options;
    options.readInputLocale = []() -> InputLocaleReading { throw std::runtime_error("reader failed"); };
    Recorder recorder;
    KeyboardProvider failing(options);
    REQUIRE(failing.Start(recorder.Listener()).ok());
    REQUIRE(WaitUntil([&] { return recorder.Count() == 1; }));
    CHECK_EQ(recorder.Last().state, ProviderState::Error);
    CHECK_EQ(recorder.Last().detail, std::string("status provider failed"));
    failing.Stop();
}

AETHER_TEST("keyboard provider: listener can destroy its public owner and worker releases retained state") {
    FakeInputLocale fake;
    std::atomic<bool> destroyed{false};
    auto lease = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = lease;
    KeyboardProviderOptions options = FakeOptions(fake, 50ms);
    options.readInputLocale = [&, lease] { return fake.Read(); };
    auto provider = std::make_unique<KeyboardProvider>(std::move(options));
    lease.reset();
    REQUIRE(provider->Start([&](const KeyboardSnapshot&) { provider.reset(); destroyed = true; }).ok());
    REQUIRE(WaitUntil([&] { return destroyed.load() && weak.expired(); }));
    CHECK(provider == nullptr);
}
