// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Runs the real audio providers read-only against the default endpoints. No test changes mute or
// volume. Friendly names and platform detail strings are omitted from diagnostics because device
// names can contain personal information. Listener state is declared before providers so a failed
// assertion still stops and joins the provider before destroying its captures.
#include "AetherTest.h"

#include "status/audio/AudioProvider.h"
#include "status/audio/EndpointReading.h"
#include "status/audio/MicrophoneProvider.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace aether;
using namespace aether::status;
using namespace aether::status::audio;
using namespace std::chrono_literals;

namespace {

constexpr auto kFirstSnapshotTimeout = 10s;

std::string StateName(ProviderState state) {
    switch (state) {
        case ProviderState::Initializing: return "Initializing";
        case ProviderState::Available: return "Available";
        case ProviderState::Unavailable: return "Unavailable";
        case ProviderState::Unsupported: return "Unsupported";
        case ProviderState::Error: return "Error";
    }
    return "?";
}

std::string Optional(const std::optional<float>& value) {
    if (!value) return "unknown";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(*value));
    return buffer;
}

std::string Optional(const std::optional<bool>& value) { return value ? (*value ? "true" : "false") : "unknown"; }

std::string Describe(const AudioSnapshot& s) {
    return "state=" + StateName(s.state) + " deviceAvailable=" + (s.deviceAvailable ? "true" : "false") +
           " volume=" + Optional(s.volume) + " muted=" + Optional(s.muted) + " hasDeviceName=" +
           (s.deviceName.empty() ? "false" : "true") + " hasDetail=" + (s.detail.empty() ? "false" : "true");
}

std::string Describe(const MicrophoneSnapshot& s) {
    return "state=" + StateName(s.state) + " deviceAvailable=" + (s.deviceAvailable ? "true" : "false") +
           " inputVolume=" + Optional(s.inputVolume) + " muted=" + Optional(s.muted) + " hasDeviceName=" +
           (s.deviceName.empty() ? "false" : "true") + " inUseDetection=" +
           (s.inUseDetection == Support::Unsupported ? "Unsupported" : "other") +
           " hasDetail=" + (s.detail.empty() ? "false" : "true");
}

// Records every delivery (on the provider's COM thread) and lets the test wait for one.
template <class Snapshot>
class Recorder {
public:
    struct Delivery {
        Snapshot snapshot;
        ChangeOrigin origin;
        std::thread::id thread;
    };

    std::function<void(const Snapshot&, ChangeOrigin)> Listener() {
        return [this](const Snapshot& snapshot, ChangeOrigin origin) {
            {
                std::lock_guard lock(mutex_);
                deliveries_.push_back({snapshot, origin, std::this_thread::get_id()});
            }
            changed_.notify_all();
        };
    }

    // First delivery at or after `from` matching `predicate`.
    template <class Predicate>
    std::optional<Delivery> WaitFor(std::size_t from, Predicate predicate, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        std::optional<Delivery> found;
        changed_.wait_for(lock, timeout, [&] {
            for (std::size_t i = from; i < deliveries_.size(); ++i) {
                if (predicate(deliveries_[i])) {
                    found = deliveries_[i];
                    return true;
                }
            }
            return false;
        });
        return found;
    }

    std::optional<Delivery> WaitForFirstReading(std::chrono::milliseconds timeout) {
        return WaitFor(0, [](const Delivery& d) { return d.snapshot.state != ProviderState::Initializing; }, timeout);
    }

    std::size_t Count() const {
        std::lock_guard lock(mutex_);
        return deliveries_.size();
    }

    Delivery Last() const {
        std::lock_guard lock(mutex_);
        return deliveries_.back();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Delivery> deliveries_;
};

template <class Snapshot>
void CheckConsistent(const Snapshot& snapshot, const std::optional<float>& level) {
    switch (snapshot.state) {
        case ProviderState::Available:
            CHECK(snapshot.deviceAvailable);
            if (level) {
                CHECK(*level >= 0.0f);
                CHECK(*level <= 1.0f);
            }
            if (!snapshot.muted || !level) TEST_NOTE("bound, but some values were unreadable");
            break;
        case ProviderState::Unavailable:
        case ProviderState::Error:
            CHECK_FALSE(snapshot.deviceAvailable);
            CHECK_FALSE(level.has_value());
            CHECK_FALSE(snapshot.muted.has_value());
            CHECK_FALSE(snapshot.detail.empty());
            break;
        default:
            CHECK(false);  // Initializing/Unsupported are never published as a reading
    }
}

}  // namespace

AETHER_TEST("status_audio integration: output provider reports the default render endpoint") {
    Recorder<AudioSnapshot> recorder;
    AudioProvider provider;
    REQUIRE(provider.StartWithOrigin(recorder.Listener()).ok());
    CHECK_EQ(provider.StartWithOrigin(recorder.Listener()).error().code, ErrorCode::InvalidState);

    const auto first = recorder.WaitForFirstReading(kFirstSnapshotTimeout);
    REQUIRE(first.has_value());
    TEST_NOTE("output: " + Describe(first->snapshot));
    CHECK_EQ(first->origin, ChangeOrigin::External);
    CHECK(first->thread != std::this_thread::get_id());
    CheckConsistent(first->snapshot, first->snapshot.volume);

    provider.Stop();
    const std::size_t afterStop = recorder.Count();
    // After Stop() nothing is published, so Current() is exactly the last delivered snapshot.
    CHECK(SameSnapshot(provider.Current(), recorder.Last().snapshot));
    std::this_thread::sleep_for(300ms);
    CHECK_EQ(recorder.Count(), afterStop);  // no listener call after Stop() returned
    provider.Stop();                        // idempotent
    CHECK_EQ(provider.SetMuted(true).error().code, ErrorCode::InvalidState);
}

AETHER_TEST("status_audio integration: microphone provider reports the default capture endpoint") {
    Recorder<MicrophoneSnapshot> recorder;
    MicrophoneProvider provider;
    REQUIRE(provider.StartWithOrigin(recorder.Listener()).ok());

    const auto first = recorder.WaitForFirstReading(kFirstSnapshotTimeout);
    REQUIRE(first.has_value());
    TEST_NOTE("microphone: " + Describe(first->snapshot));
    CHECK_EQ(first->origin, ChangeOrigin::External);
    CHECK_EQ(first->snapshot.inUseDetection, Support::Unsupported);
    CheckConsistent(first->snapshot, first->snapshot.inputVolume);

    provider.Stop();
    const std::size_t afterStop = recorder.Count();
    CHECK(SameSnapshot(provider.Current(), recorder.Last().snapshot));
    std::this_thread::sleep_for(300ms);
    CHECK_EQ(recorder.Count(), afterStop);
}

AETHER_TEST("status_audio integration: both providers run side by side on their own COM threads") {
    Recorder<AudioSnapshot> output;
    Recorder<MicrophoneSnapshot> input;
    AudioProvider outputProvider;
    MicrophoneProvider inputProvider;
    REQUIRE(outputProvider.StartWithOrigin(output.Listener()).ok());
    REQUIRE(inputProvider.StartWithOrigin(input.Listener()).ok());

    const auto outputFirst = output.WaitForFirstReading(kFirstSnapshotTimeout);
    const auto inputFirst = input.WaitForFirstReading(kFirstSnapshotTimeout);
    REQUIRE(outputFirst.has_value());
    REQUIRE(inputFirst.has_value());
    TEST_NOTE("output (side by side): " + Describe(outputFirst->snapshot));
    TEST_NOTE("microphone (side by side): " + Describe(inputFirst->snapshot));
    CHECK(outputFirst->thread != inputFirst->thread);  // one MTA worker per provider

    inputProvider.Stop();
    outputProvider.Stop();
}

AETHER_TEST("status_audio integration: plain listener, restart and stop from the listener") {
    // Declared before the provider: the listener below touches them on the COM thread.
    std::mutex mutex;
    std::condition_variable changed;
    int calls = 0;
    Recorder<AudioSnapshot> recorder;
    AudioProvider provider;

    // IStatusProvider::Start with a listener that stops the provider from inside its first call.
    REQUIRE(provider
                .Start([&](const AudioSnapshot&) {
                    provider.Stop();
                    {
                        std::lock_guard lock(mutex);
                        ++calls;
                    }
                    changed.notify_all();
                })
                .ok());
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, kFirstSnapshotTimeout, [&] { return calls > 0; }));
    }
    std::this_thread::sleep_for(300ms);
    {
        std::lock_guard lock(mutex);
        CHECK_EQ(calls, 1);
    }

    // Restart after a stop issued from the listener: a fresh session delivers a new first snapshot.
    REQUIRE(provider.StartWithOrigin(recorder.Listener()).ok());
    const auto again = recorder.WaitForFirstReading(kFirstSnapshotTimeout);
    REQUIRE(again.has_value());
    TEST_NOTE("output after restart: " + Describe(again->snapshot));
    provider.Stop();
}

AETHER_TEST("status_audio integration: concurrent Stop calls wait for an active listener") {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    bool firstCalling = false;
    bool firstReturned = false;
    bool secondCalling = false;
    bool secondReturned = false;
    std::atomic<int> calls{0};
    AudioProvider provider;
    REQUIRE(provider.Start([&](const AudioSnapshot&) {
        ++calls;
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait_for(lock, 5s, [&] { return release; });
    }).ok());
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, kFirstSnapshotTimeout, [&] { return entered; }));
    }
    auto stop = [&](bool& calling, bool& returned) {
        {
            std::lock_guard lock(mutex);
            calling = true;
        }
        changed.notify_all();
        provider.Stop();
        {
            std::lock_guard lock(mutex);
            returned = true;
        }
        changed.notify_all();
    };
    std::thread first([&] { stop(firstCalling, firstReturned); });
    bool firstStarted;
    bool firstEarly;
    {
        std::unique_lock lock(mutex);
        firstStarted = changed.wait_for(lock, 5s, [&] { return firstCalling; });
        firstEarly = changed.wait_for(lock, 100ms, [&] { return firstReturned; });
    }
    std::thread second([&] { stop(secondCalling, secondReturned); });
    bool secondStarted;
    bool secondEarly;
    {
        std::unique_lock lock(mutex);
        secondStarted = changed.wait_for(lock, 5s, [&] { return secondCalling; });
        secondEarly = changed.wait_for(lock, 100ms, [&] { return secondReturned; });
        release = true;
    }
    changed.notify_all();
    first.join();
    second.join();
    CHECK(firstStarted);
    CHECK(secondStarted);
    CHECK_FALSE(firstEarly);
    CHECK_FALSE(secondEarly);
    CHECK(firstReturned);
    CHECK(secondReturned);
    CHECK_EQ(calls.load(), 1);
}

