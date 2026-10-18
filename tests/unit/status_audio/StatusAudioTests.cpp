// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure building blocks of the audio providers (snapshot construction, volume
// clamping, deduplication, HRESULT classification, completions) and the COM
// worker thread. Nothing here touches an audio device.
#include "AetherTest.h"

#include "core/FileSystem.h"
#include "diagnostics/Logger.h"
#include "status/audio/AudioControl.h"
#include "status/audio/AudioProvider.h"
#include "status/audio/EndpointReading.h"
#include "status/audio/MicrophoneProvider.h"
#include "status/audio/MtaWorker.h"
#include "status/audio/SnapshotPublisher.h"

#include <audioclient.h>
#include <mmdeviceapi.h>  // E_NOTFOUND
#include <objbase.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::status;
using namespace aether::status::audio;
using namespace std::chrono_literals;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfinity = std::numeric_limits<float>::infinity();

EndpointReading BoundReading(float volume, bool muted, std::wstring name = L"Speakers (Test Audio)") {
    EndpointReading reading;
    reading.binding = EndpointBinding::Bound;
    reading.volume = volume;
    reading.muted = muted;
    reading.deviceName = std::move(name);
    return reading;
}

// A reading in an unbound state that still carries stale values, to prove they are never published.
EndpointReading UnboundReading(EndpointBinding binding, std::string detail) {
    EndpointReading reading = BoundReading(0.8f, true);
    reading.binding = binding;
    reading.detail = std::move(detail);
    return reading;
}

// Blocks until `predicate` is true or `timeout` elapses.
class Latch {
public:
    void Set() {
        {
            std::lock_guard lock(mutex_);
            set_ = true;
        }
        changed_.notify_all();
    }
    bool Wait(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return set_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool set_ = false;
};

}  // namespace

// ---------------------------------------------------------------------------------------------
// Volume clamping
// ---------------------------------------------------------------------------------------------

AETHER_TEST("status_audio: reported volume is clamped and non-finite values are unknown") {
    CHECK_EQ(SanitizeVolume(0.42f), std::optional<float>(0.42f));
    CHECK_EQ(SanitizeVolume(0.0f), std::optional<float>(0.0f));
    CHECK_EQ(SanitizeVolume(1.0f), std::optional<float>(1.0f));
    CHECK_EQ(SanitizeVolume(-0.25f), std::optional<float>(0.0f));
    CHECK_EQ(SanitizeVolume(1.75f), std::optional<float>(1.0f));
    CHECK_FALSE(SanitizeVolume(kNaN).has_value());
    CHECK_FALSE(SanitizeVolume(kInfinity).has_value());
    CHECK_FALSE(SanitizeVolume(-kInfinity).has_value());
}

AETHER_TEST("status_audio: requested volume is clamped and non-finite requests are rejected") {
    auto inRange = NormalizeRequestedVolume(0.5f);
    REQUIRE(inRange.ok());
    CHECK_EQ(inRange.value(), 0.5f);
    CHECK_EQ(NormalizeRequestedVolume(-3.0f).value(), 0.0f);
    CHECK_EQ(NormalizeRequestedVolume(1.0001f).value(), 1.0f);
    CHECK_EQ(NormalizeRequestedVolume(0.0f).value(), 0.0f);

    auto nan = NormalizeRequestedVolume(kNaN);
    REQUIRE_FALSE(nan.ok());
    CHECK_EQ(nan.error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(NormalizeRequestedVolume(kInfinity).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(NormalizeRequestedVolume(-kInfinity).error().code, ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------------------------------------
// Snapshot construction
// ---------------------------------------------------------------------------------------------

AETHER_TEST("status_audio: bound output reading becomes an Available snapshot") {
    const AudioSnapshot snapshot = MakeAudioSnapshot(BoundReading(0.3f, false));
    CHECK_EQ(snapshot.state, ProviderState::Available);
    CHECK(snapshot.deviceAvailable);
    CHECK_EQ(snapshot.volume, std::optional<float>(0.3f));
    CHECK_EQ(snapshot.muted, std::optional<bool>(false));
    CHECK_EQ(snapshot.deviceName, std::wstring(L"Speakers (Test Audio)"));
    CHECK(snapshot.detail.empty());
}

AETHER_TEST("status_audio: bound reading with unreadable values keeps them unknown") {
    EndpointReading reading = BoundReading(0.5f, false);
    reading.volume.reset();
    reading.muted.reset();
    reading.deviceName.clear();
    reading.detail = "volume level unavailable (0x80004005)";
    const AudioSnapshot snapshot = MakeAudioSnapshot(reading);
    CHECK_EQ(snapshot.state, ProviderState::Available);
    CHECK(snapshot.deviceAvailable);
    CHECK_FALSE(snapshot.volume.has_value());
    CHECK_FALSE(snapshot.muted.has_value());
    CHECK(snapshot.deviceName.empty());
    CHECK_EQ(snapshot.detail, std::string("volume level unavailable (0x80004005)"));

    CHECK_EQ(MakeAudioSnapshot(BoundReading(1.5f, true)).volume, std::optional<float>(1.0f));
    CHECK_FALSE(MakeAudioSnapshot(BoundReading(kNaN, true)).volume.has_value());
}

AETHER_TEST("status_audio: unbound readings never publish stale values") {
    const AudioSnapshot noDevice = MakeAudioSnapshot(UnboundReading(EndpointBinding::NoDevice, "no device"));
    CHECK_EQ(noDevice.state, ProviderState::Unavailable);
    CHECK_FALSE(noDevice.deviceAvailable);
    CHECK_FALSE(noDevice.volume.has_value());
    CHECK_FALSE(noDevice.muted.has_value());
    CHECK(noDevice.deviceName.empty());
    CHECK_EQ(noDevice.detail, std::string("no device"));

    const AudioSnapshot service =
        MakeAudioSnapshot(UnboundReading(EndpointBinding::ServiceUnavailable, "service stopped"));
    CHECK_EQ(service.state, ProviderState::Unavailable);
    CHECK_FALSE(service.deviceAvailable);
    CHECK_FALSE(service.volume.has_value());
    CHECK_EQ(service.detail, std::string("service stopped"));

    const AudioSnapshot failed = MakeAudioSnapshot(UnboundReading(EndpointBinding::Failed, "boom"));
    CHECK_EQ(failed.state, ProviderState::Error);
    CHECK_FALSE(failed.deviceAvailable);
    CHECK_FALSE(failed.muted.has_value());
    CHECK_EQ(failed.detail, std::string("boom"));

    const AudioSnapshot initializing = MakeAudioSnapshot(UnboundReading(EndpointBinding::Initializing, ""));
    CHECK_EQ(initializing.state, ProviderState::Initializing);
    CHECK_FALSE(initializing.deviceAvailable);
    CHECK_FALSE(initializing.volume.has_value());
}

AETHER_TEST("status_audio: microphone snapshot reports gain and never in-use detection") {
    const MicrophoneSnapshot bound = MakeMicrophoneSnapshot(BoundReading(0.75f, true, L"Microphone (Test)"));
    CHECK_EQ(bound.state, ProviderState::Available);
    CHECK(bound.deviceAvailable);
    CHECK_EQ(bound.inputVolume, std::optional<float>(0.75f));
    CHECK_EQ(bound.muted, std::optional<bool>(true));
    CHECK_EQ(bound.deviceName, std::wstring(L"Microphone (Test)"));
    CHECK_EQ(bound.inUseDetection, Support::Unsupported);

    CHECK_EQ(MakeMicrophoneSnapshot(BoundReading(-1.0f, false)).inputVolume, std::optional<float>(0.0f));

    const MicrophoneSnapshot none = MakeMicrophoneSnapshot(UnboundReading(EndpointBinding::NoDevice, "none"));
    CHECK_EQ(none.state, ProviderState::Unavailable);
    CHECK_FALSE(none.deviceAvailable);
    CHECK_FALSE(none.inputVolume.has_value());
    CHECK_FALSE(none.muted.has_value());
    CHECK_EQ(none.inUseDetection, Support::Unsupported);
    CHECK_EQ(MakeMicrophoneSnapshot(UnboundReading(EndpointBinding::Failed, "x")).state, ProviderState::Error);
}

AETHER_TEST("status_audio: failed endpoint reads clear old fields and recover independently") {
    EndpointReading reading = BoundReading(0.75f, true);
    ApplyEndpointLevelReadings(reading, E_FAIL, 0.0f, S_OK, false, "device name unavailable");
    const auto output = MakeAudioSnapshot(reading);
    CHECK_EQ(output.state, ProviderState::Available);
    CHECK(output.deviceAvailable);
    CHECK_FALSE(output.volume.has_value());
    CHECK_EQ(output.muted, std::optional<bool>(false));
    CHECK(output.detail.find("volume level unavailable (0x80004005)") != std::string::npos);
    CHECK(output.detail.find("device name unavailable") != std::string::npos);

    ApplyEndpointLevelReadings(reading, S_OK, 0.25f, E_ACCESSDENIED, false);
    CHECK_EQ(reading.volume, std::optional<float>(0.25f));
    CHECK_FALSE(reading.muted.has_value());
    CHECK_EQ(reading.detail, std::string("mute state unavailable (0x80070005)"));
    ApplyEndpointLevelReadings(reading, S_OK, 0.5f, S_OK, true);
    CHECK_EQ(reading.volume, std::optional<float>(0.5f));
    CHECK_EQ(reading.muted, std::optional<bool>(true));
    CHECK(reading.detail.empty());
}

// ---------------------------------------------------------------------------------------------
// Deduplication
// ---------------------------------------------------------------------------------------------

AETHER_TEST("status_audio: snapshot equality covers every field") {
    const AudioSnapshot base = MakeAudioSnapshot(BoundReading(0.5f, false));
    CHECK(SameSnapshot(base, base));
    AudioSnapshot other = base;
    other.volume = 0.51f;
    CHECK_FALSE(SameSnapshot(base, other));
    other = base;
    other.muted = true;
    CHECK_FALSE(SameSnapshot(base, other));
    other = base;
    other.muted.reset();
    CHECK_FALSE(SameSnapshot(base, other));
    other = base;
    other.deviceName = L"Headphones";
    CHECK_FALSE(SameSnapshot(base, other));
    other = base;
    other.detail = "partial";
    CHECK_FALSE(SameSnapshot(base, other));
    other = base;
    other.state = ProviderState::Error;
    CHECK_FALSE(SameSnapshot(base, other));
    other = base;
    other.deviceAvailable = false;
    CHECK_FALSE(SameSnapshot(base, other));

    const MicrophoneSnapshot mic = MakeMicrophoneSnapshot(BoundReading(0.5f, false));
    CHECK(SameSnapshot(mic, mic));
    MicrophoneSnapshot micOther = mic;
    micOther.inputVolume = 0.25f;
    CHECK_FALSE(SameSnapshot(mic, micOther));
    micOther = mic;
    micOther.inUseDetection = Support::Unknown;
    CHECK_FALSE(SameSnapshot(mic, micOther));
}

AETHER_TEST("status_audio: publisher delivers changes once, with their origin") {
    SnapshotPublisher<AudioSnapshot> publisher;
    std::vector<std::pair<AudioSnapshot, ChangeOrigin>> delivered;
    const auto session = publisher.Open([&](const AudioSnapshot& snapshot, ChangeOrigin origin) {
        delivered.emplace_back(snapshot, origin);
    });
    CHECK_EQ(publisher.Current().state, ProviderState::Initializing);

    const AudioSnapshot first = MakeAudioSnapshot(BoundReading(0.5f, false));
    CHECK(publisher.Publish(session, first, ChangeOrigin::External));
    CHECK_FALSE(publisher.Publish(session, first, ChangeOrigin::External));  // duplicate
    const AudioSnapshot muted = MakeAudioSnapshot(BoundReading(0.5f, true));
    CHECK(publisher.Publish(session, muted, ChangeOrigin::ThisProcess));
    CHECK_FALSE(publisher.Publish(session, muted, ChangeOrigin::External));

    REQUIRE_EQ(delivered.size(), 2u);
    CHECK_EQ(delivered[0].second, ChangeOrigin::External);
    CHECK_EQ(delivered[1].second, ChangeOrigin::ThisProcess);
    CHECK_EQ(delivered[1].first.muted, std::optional<bool>(true));
    CHECK(SameSnapshot(publisher.Current(), muted));
}

AETHER_TEST("status_audio: publisher ignores stale sessions and stops after Close") {
    SnapshotPublisher<MicrophoneSnapshot> publisher;
    int calls = 0;
    const auto oldSession = publisher.Open([&](const MicrophoneSnapshot&, ChangeOrigin) { ++calls; });
    const MicrophoneSnapshot snapshot = MakeMicrophoneSnapshot(BoundReading(0.5f, false));
    CHECK(publisher.Publish(oldSession, snapshot, ChangeOrigin::External));
    publisher.Close();
    CHECK_FALSE(publisher.Publish(oldSession, snapshot, ChangeOrigin::External));
    CHECK(SameSnapshot(publisher.Current(), snapshot));  // Current() keeps the last value after Close

    const auto newSession = publisher.Open([&](const MicrophoneSnapshot&, ChangeOrigin) { ++calls; });
    CHECK_NE(newSession, oldSession);
    CHECK_EQ(publisher.Current().state, ProviderState::Initializing);  // a new session starts fresh
    CHECK_FALSE(publisher.Publish(oldSession, snapshot, ChangeOrigin::External));
    CHECK(publisher.Publish(newSession, snapshot, ChangeOrigin::External));  // first of the session
    CHECK_EQ(calls, 2);
}

AETHER_TEST("status_audio: publisher contains listener exceptions") {
    SnapshotPublisher<AudioSnapshot> publisher;
    const auto session =
        publisher.Open([](const AudioSnapshot&, ChangeOrigin) { throw std::runtime_error("listener failure"); });
    CHECK(publisher.Publish(session, MakeAudioSnapshot(BoundReading(0.1f, false)), ChangeOrigin::External));
    CHECK_EQ(publisher.Current().volume, std::optional<float>(0.1f));
}

// ---------------------------------------------------------------------------------------------
// HRESULT classification and event context
// ---------------------------------------------------------------------------------------------

AETHER_TEST("status_audio: failures map to device, service or error states") {
    CHECK_EQ(ClassifyFailure(E_NOTFOUND), EndpointBinding::NoDevice);
    CHECK_EQ(ClassifyFailure(AUDCLNT_E_DEVICE_INVALIDATED), EndpointBinding::NoDevice);
    CHECK_EQ(ClassifyFailure(HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED)), EndpointBinding::NoDevice);
    CHECK_EQ(ClassifyFailure(AUDCLNT_E_SERVICE_NOT_RUNNING), EndpointBinding::ServiceUnavailable);
    CHECK_EQ(ClassifyFailure(RPC_E_DISCONNECTED), EndpointBinding::ServiceUnavailable);
    CHECK_EQ(ClassifyFailure(HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE)), EndpointBinding::ServiceUnavailable);
    CHECK_EQ(ClassifyFailure(E_FAIL), EndpointBinding::Failed);
    CHECK_EQ(ClassifyFailure(E_OUTOFMEMORY), EndpointBinding::Failed);
    CHECK_EQ(ClassifyFailure(E_ACCESSDENIED), EndpointBinding::Failed);

    CHECK(RequiresRebind(AUDCLNT_E_DEVICE_INVALIDATED));
    CHECK(RequiresRebind(AUDCLNT_E_SERVICE_NOT_RUNNING));
    CHECK_FALSE(RequiresRebind(E_INVALIDARG));
    CHECK_FALSE(RequiresRebind(E_FAIL));

    CHECK_EQ(ToString(EndpointBinding::ServiceUnavailable), std::string_view("ServiceUnavailable"));
    CHECK_EQ(ToString(EndpointBinding::Bound), std::string_view("Bound"));
}

AETHER_TEST("status_audio: event context is a stable per-process GUID") {
    const GUID& first = AudioEventContext();
    const GUID& second = AudioEventContext();
    CHECK(first == second);
    CHECK(&first == &second);
    CHECK_FALSE(first == GUID{});
    CHECK_EQ(OriginFromEventContext(first), ChangeOrigin::ThisProcess);
    CHECK_EQ(OriginFromEventContext(GUID{}), ChangeOrigin::External);
    GUID other = first;
    other.Data1 ^= 1u;
    CHECK_EQ(OriginFromEventContext(other), ChangeOrigin::External);
}

// ---------------------------------------------------------------------------------------------
// Completions
// ---------------------------------------------------------------------------------------------

AETHER_TEST("status_audio: completion runs exactly once") {
    int calls = 0;
    std::optional<ErrorCode> code;
    {
        CompletionOnce once([&](Result<void> result) {
            ++calls;
            code = result.ok() ? std::nullopt : std::optional<ErrorCode>(result.error().code);
        });
        once.Complete(Ok());
        once.Complete(MakeError(ErrorCode::Platform, "late"));
    }
    CHECK_EQ(calls, 1);
    CHECK_FALSE(code.has_value());
}

AETHER_TEST("status_audio: discarded completion reports Cancelled and exceptions are contained") {
    std::optional<ErrorCode> code;
    { CompletionOnce once([&](Result<void> result) { code = result.error().code; }); }
    CHECK_EQ(code, std::optional<ErrorCode>(ErrorCode::Cancelled));

    CompletionOnce throwing([](Result<void>) { throw std::runtime_error("completion failure"); });
    throwing.Complete(Ok());  // must not propagate
    CompletionOnce empty(nullptr);
    empty.Complete(Ok());
}

// ---------------------------------------------------------------------------------------------
// Control calls without a running provider never reach Core Audio
// ---------------------------------------------------------------------------------------------

AETHER_TEST("status_audio: control calls fail cleanly when the providers are not running") {
    AudioProvider output;
    CHECK_EQ(output.Current().state, ProviderState::Initializing);
    CHECK_EQ(output.SetMuted(true).error().code, ErrorCode::InvalidState);
    CHECK_EQ(output.ToggleMute().error().code, ErrorCode::InvalidState);
    CHECK_EQ(output.SetVolume(0.5f).error().code, ErrorCode::InvalidState);
    // Arguments are validated first: an invalid level is InvalidArgument whatever the state.
    CHECK_EQ(output.SetVolume(kNaN).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(output.SetVolume(-kInfinity).error().code, ErrorCode::InvalidArgument);

    std::optional<ErrorCode> asyncCode;
    output.ToggleMuteAsync([&](Result<void> result) { asyncCode = result.error().code; });
    CHECK_EQ(asyncCode, std::optional<ErrorCode>(ErrorCode::InvalidState));  // completed synchronously
    asyncCode.reset();
    output.SetVolumeAsync(kInfinity, [&](Result<void> result) { asyncCode = result.error().code; });
    CHECK_EQ(asyncCode, std::optional<ErrorCode>(ErrorCode::InvalidArgument));
    asyncCode.reset();
    output.SetVolumeAsync(0.25f, [&](Result<void> result) { asyncCode = result.error().code; });
    CHECK_EQ(asyncCode, std::optional<ErrorCode>(ErrorCode::InvalidState));

    MicrophoneProvider input;
    CHECK_EQ(input.Current().inUseDetection, Support::Unsupported);
    CHECK_EQ(input.SetMuted(false).error().code, ErrorCode::InvalidState);
    asyncCode.reset();
    input.SetMutedAsync(true, [&](Result<void> result) { asyncCode = result.error().code; });
    CHECK_EQ(asyncCode, std::optional<ErrorCode>(ErrorCode::InvalidState));

    CHECK_EQ(output.Start(nullptr).error().code, ErrorCode::InvalidArgument);
    output.Stop();  // never started: no-op
    output.Stop();
}

// ---------------------------------------------------------------------------------------------
// COM worker
// ---------------------------------------------------------------------------------------------

AETHER_TEST("status_audio: COM worker runs tasks in order inside the MTA") {
    MtaWorker worker;
    REQUIRE(worker.Start(L"Aether test worker").ok());
    CHECK_FALSE(worker.IsWorkerThread());
    CHECK_EQ(worker.Start(L"again").error().code, ErrorCode::InvalidState);

    std::mutex mutex;
    std::vector<int> order;
    std::optional<APTTYPE> apartment;
    bool onWorker = false;
    Latch done;
    for (int i = 0; i < 5; ++i) {
        CHECK(worker.Post([&, i] {
            std::lock_guard lock(mutex);
            order.push_back(i);
        }));
    }
    CHECK(worker.Post([&] {
        APTTYPE type{};
        APTTYPEQUALIFIER qualifier{};
        if (SUCCEEDED(::CoGetApartmentType(&type, &qualifier))) apartment = type;
        onWorker = worker.IsWorkerThread();
        CHECK(SUCCEEDED(worker.ComStatus()));
        done.Set();
    }));
    REQUIRE(done.Wait(5s));
    worker.Stop();
    CHECK_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
    CHECK_EQ(apartment, std::optional<APTTYPE>(APTTYPE_MTA));
    CHECK(onWorker);
    CHECK_FALSE(worker.Post([] {}));
    CHECK_FALSE(worker.PostAfter(1ms, [] {}));
}

AETHER_TEST("status_audio: COM worker runs delayed tasks by due time") {
    MtaWorker worker;
    REQUIRE(worker.Start(L"Aether test worker").ok());
    std::mutex mutex;
    std::vector<int> order;
    Latch done;
    const auto start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration lateElapsed{};
    CHECK(worker.PostAfter(120ms, [&] {
        std::lock_guard lock(mutex);
        order.push_back(2);
        lateElapsed = std::chrono::steady_clock::now() - start;
        done.Set();
    }));
    CHECK(worker.PostAfter(30ms, [&] {
        std::lock_guard lock(mutex);
        order.push_back(1);
    }));
    CHECK(worker.Post([&] {
        std::lock_guard lock(mutex);
        order.push_back(0);
    }));
    REQUIRE(done.Wait(5s));
    worker.Stop();
    CHECK_EQ(order, (std::vector<int>{0, 1, 2}));
    CHECK(lateElapsed >= 120ms);
}

AETHER_TEST("status_audio: COM worker Stop discards pending work and runs the final task") {
    MtaWorker worker;
    REQUIRE(worker.Start(L"Aether test worker").ok());
    Latch blockerStarted;
    Latch cancelled;
    std::atomic<bool> pendingRan{false};
    std::atomic<bool> timerRan{false};
    std::atomic<bool> finalOnWorker{false};
    std::optional<ErrorCode> cancelledCode;

    // The running task waits until the queued request has been discarded (its completion fires).
    CHECK(worker.Post([&] {
        blockerStarted.Set();
        cancelled.Wait(5s);
    }));
    REQUIRE(blockerStarted.Wait(5s));
    auto once = std::make_shared<CompletionOnce>([&](Result<void> result) {
        cancelledCode = result.ok() ? std::nullopt : std::optional<ErrorCode>(result.error().code);
        cancelled.Set();
    });
    CHECK(worker.Post([once, &pendingRan] {
        pendingRan = true;
        once->Complete(Ok());
    }));
    once.reset();  // the queued task now owns the only reference
    CHECK(worker.PostAfter(10ms, [&] { timerRan = true; }));

    worker.Stop([&] { finalOnWorker = worker.IsWorkerThread(); });
    CHECK_FALSE(pendingRan.load());
    CHECK_FALSE(timerRan.load());
    CHECK_EQ(cancelledCode, std::optional<ErrorCode>(ErrorCode::Cancelled));
    CHECK(finalOnWorker.load());
    worker.Stop();  // idempotent
}

AETHER_TEST("status_audio: COM worker can be stopped from its own task") {
    auto worker = std::make_unique<MtaWorker>();
    REQUIRE(worker->Start(L"Aether test worker").ok());
    Latch go;
    Latch stopped;
    std::atomic<bool> finalRan{false};
    std::atomic<bool> afterStopRan{false};
    CHECK(worker->Post([&] {
        go.Wait(5s);  // both tasks are queued before Stop() runs
        worker->Stop([&] { finalRan = true; });  // must not self-join
        stopped.Set();
    }));
    CHECK(worker->Post([&] { afterStopRan = true; }));
    go.Set();
    REQUIRE(stopped.Wait(5s));
    worker.reset();  // joins the thread, after the final task
    CHECK(finalRan.load());
    CHECK_FALSE(afterStopRan.load());
}

AETHER_TEST("status_audio: poster outlives its worker safely") {
    MtaWorker::Poster poster;
    CHECK_FALSE(poster.Post([] {}));  // default-constructed: no worker
    std::atomic<int> ran{0};
    {
        MtaWorker worker;
        REQUIRE(worker.Start(L"Aether test worker").ok());
        poster = worker.MakePoster();
        Latch done;
        CHECK(poster.Post([&] {
            ++ran;
            done.Set();
        }));
        REQUIRE(done.Wait(5s));
    }
    CHECK_FALSE(poster.Post([&] { ++ran; }));
    CHECK_EQ(ran.load(), 1);
}

AETHER_TEST("status_audio: every concurrent Stop waits for the running task") {
    Latch entered;
    Latch release;
    Latch firstCalling;
    Latch firstReturned;
    Latch secondCalling;
    Latch secondReturned;
    MtaWorker worker;  // destroyed before the state captured by its tasks
    REQUIRE(worker.Start(L"Aether test worker").ok());
    REQUIRE(worker.Post([&] {
        entered.Set();
        release.Wait(5s);
    }));
    REQUIRE(entered.Wait(5s));
    std::thread first([&] {
        firstCalling.Set();
        worker.Stop();
        firstReturned.Set();
    });
    const bool firstStarted = firstCalling.Wait(5s);
    const bool firstReturnedEarly = firstReturned.Wait(100ms);
    std::thread second([&] {
        secondCalling.Set();
        worker.Stop();
        secondReturned.Set();
    });
    const bool secondStarted = secondCalling.Wait(5s);
    const bool secondReturnedEarly = secondReturned.Wait(100ms);
    release.Set();
    first.join();
    second.join();
    CHECK(firstStarted);
    CHECK(secondStarted);
    CHECK_FALSE(firstReturnedEarly);
    CHECK_FALSE(secondReturnedEarly);
    CHECK(firstReturned.Wait(0ms));
    CHECK(secondReturned.Wait(0ms));
}

AETHER_TEST("status_audio: listener completion and worker exception text never reaches logs") {
    struct ShutdownLog {
        ~ShutdownLog() { diag::Logger::Shutdown(); }
    } shutdown;
    diag::LoggerOptions options;
    options.directory = test::MakeTempDirectory("audio-log-privacy");
    options.baseName = L"audio";
    options.writeToDebugger = false;
    diag::Logger::Initialize(options);

    SnapshotPublisher<AudioSnapshot> publisher;
    const auto session = publisher.Open([](const AudioSnapshot&, ChangeOrigin) {
        throw std::runtime_error("synthetic-private-listener-name");
    });
    CHECK(publisher.Publish(session, MakeAudioSnapshot(BoundReading(0.5f, false)), ChangeOrigin::External));
    CompletionOnce completion([](Result<void>) {
        throw std::runtime_error("synthetic-private-completion-name");
    });
    completion.Complete(Ok());
    Latch done;
    MtaWorker worker;
    REQUIRE(worker.Start(L"Aether test worker").ok());
    CHECK(worker.Post([] { throw std::runtime_error("synthetic-private-worker-name"); }));
    CHECK(worker.Post([&] { done.Set(); }));
    REQUIRE(done.Wait(5s));
    worker.Stop();
    diag::Logger::Shutdown();
    const auto contents = fs::ReadFile(options.directory / L"audio.log", 64 * 1024);
    REQUIRE(contents.ok());
    CHECK(contents.value().find("synthetic-private") == std::string::npos);
    CHECK(contents.value().find("status listener threw an exception") != std::string::npos);
    CHECK(contents.value().find("control completion threw an exception") != std::string::npos);
    CHECK(contents.value().find("COM worker task threw an exception") != std::string::npos);
}
