// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// In-memory sessions only: these tests never enumerate, hide or modify the native taskbar.
#include "AetherTest.h"
#include "app/TaskbarController.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::app;
using namespace std::chrono_literals;

namespace {

struct FakeWorld {
    std::thread::id owner = std::this_thread::get_id();
    std::atomic<unsigned> starts{0}, stops{0}, destroyed{0}, observations{0};
    std::atomic<unsigned> failStarts{0}, failStops{0};
    std::atomic<bool> touchedUi{false}, running{true}, blockFirstStart{false}, release{false};
    std::atomic<bool> lateSessionOnCancel{false}, cancellationSeen{false};
    std::mutex mutex;
    std::vector<std::chrono::steady_clock::time_point> startTimes;
    void Record() { if (std::this_thread::get_id() == owner) touchedUi = true; }
};

bool ConsumeFailure(std::atomic<unsigned>& count) {
    unsigned value = count.load();
    while (value && !count.compare_exchange_weak(value, value - 1)) {}
    return value != 0;
}

class FakeSession final : public ITaskbarSession {
public:
    explicit FakeSession(std::shared_ptr<FakeWorld> world) : world_(std::move(world)) {}
    ~FakeSession() override { world_->Record(); ++world_->destroyed; }
    Result<bool> Running() override { world_->Record(); ++world_->observations; return world_->running.load(); }
    Result<void> Stop() override {
        world_->Record(); ++world_->stops;
        if (ConsumeFailure(world_->failStops)) return MakeError(ErrorCode::Unavailable, "synthetic private marker");
        world_->running = false; return Ok();
    }
private:
    std::shared_ptr<FakeWorld> world_;
};

TaskbarController::Options FakeOptions(const std::shared_ptr<FakeWorld>& world, std::chrono::milliseconds delay = 0ms) {
    TaskbarController::Options options;
    options.retryDelay = delay;
    options.createSession = [world](const std::filesystem::path&, const CancellationToken& cancel)
        -> Result<std::unique_ptr<ITaskbarSession>> {
        world->Record();
        { std::lock_guard lock(world->mutex); world->startTimes.push_back(std::chrono::steady_clock::now()); }
        const auto number = ++world->starts;
        while (number == 1 && world->blockFirstStart && !world->release && !cancel.IsCancelled())
            std::this_thread::sleep_for(1ms);
        if (cancel.IsCancelled()) {
            world->cancellationSeen = true;
            if (!world->lateSessionOnCancel) return MakeError(ErrorCode::Cancelled, "synthetic cancellation");
        }
        if (ConsumeFailure(world->failStarts)) return MakeError(ErrorCode::Unavailable, "synthetic private marker");
        world->running = true;
        return std::unique_ptr<ITaskbarSession>(std::make_unique<FakeSession>(world));
    };
    return options;
}

bool Pump(ManualDispatcher& dispatcher, const std::function<bool()>& ready) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!ready() && std::chrono::steady_clock::now() < end) {
        dispatcher.RunPending(); std::this_thread::sleep_for(1ms);
    }
    dispatcher.RunPending(); return ready();
}

void Settle(ManualDispatcher& dispatcher, std::chrono::milliseconds duration = 30ms) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        dispatcher.RunPending(); std::this_thread::sleep_for(1ms);
    }
}

std::filesystem::path FixtureLauncher() { return L"C:\\AetherFixture\\AetherShell.exe"; }

class GateDispatcher final : public IUiDispatcher {
public:
    ManualDispatcher queue;
    std::atomic<bool> accept{true};
    bool Post(std::function<void()> work) override { return accept && queue.Post(std::move(work)); }
    bool IsUiThread() const noexcept override { return queue.IsUiThread(); }
};

bool Idle(const std::vector<TaskbarUpdate>& notes) {
    return !notes.empty() && !notes.back().active && !notes.back().scheduled;
}

}  // namespace

AETHER_TEST("taskbar controller: activation and restoration never block or touch native state on UI") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->blockFirstStart = true;
    std::vector<TaskbarUpdate> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) {
        CHECK(dispatcher.IsUiThread()); notes.push_back(update);
    }, FakeOptions(world));
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(controller.Request(true).ok());
    CHECK(std::chrono::steady_clock::now() - start < 100ms);
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 1 && !notes.empty(); }));
    CHECK_FALSE(notes.back().active); CHECK(notes.back().scheduled);
    for (int i = 0; i < 100; ++i) REQUIRE(controller.Refresh().ok());
    world->release = true;
    REQUIRE(Pump(dispatcher, [&] { return !notes.empty() && notes.back().active; }));
    CHECK(notes.back().scheduled);  // an active session is observed
    REQUIRE(controller.Request(false).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1 && Idle(notes); }));
    for (const auto& note : notes) CHECK_FALSE(note.suspended);
    CHECK_FALSE(world->touchedUi);
    controller.Stop();
}

AETHER_TEST("taskbar controller: cancelled late activation is stopped without a stale active callback") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->blockFirstStart = true; world->lateSessionOnCancel = true;
    std::vector<bool> active;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { active.push_back(update.active); }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 1; }));
    REQUIRE(controller.Request(false).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1; }));
    CHECK(world->cancellationSeen); CHECK_EQ(world->stops.load(), 1u);
    CHECK(std::none_of(active.begin(), active.end(), [](bool value) { return value; }));
    CHECK_FALSE(world->touchedUi);
}

AETHER_TEST("taskbar controller: shutdown restores on worker and suppresses queued callbacks") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->blockFirstStart = true; world->lateSessionOnCancel = true;
    unsigned callbacks = 0;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate&) { ++callbacks; }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!world->starts && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    REQUIRE_EQ(world->starts.load(), 1u);
    controller.Stop(); dispatcher.RunPending();
    CHECK_EQ(callbacks, 0u); CHECK(world->cancellationSeen);
    CHECK_EQ(world->destroyed.load(), 1u); CHECK_EQ(world->stops.load(), 1u); CHECK_FALSE(world->touchedUi);
    CHECK_FALSE(controller.Request(true).ok());
    CHECK_FALSE(controller.Suspend().ok()); CHECK_FALSE(controller.Resume().ok());
}

AETHER_TEST("taskbar controller: retries are bounded and restart or a new request resets the budget") {
    // Behaviour change (D23 lifecycle): retries are timed by the worker, so no Refresh is needed,
    // and a Restart series (Explorer restarted) has two extra attempts.
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->failStarts = 100;
    std::vector<TaskbarUpdate> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) {
        notes.push_back(update);
    }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 3 && Idle(notes); }));
    for (int i = 0; i < 20; ++i) REQUIRE(controller.Refresh().ok());
    Settle(dispatcher, 100ms); CHECK_EQ(world->starts.load(), 3u);
    REQUIRE(controller.Restart().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 8 && Idle(notes); }));
    Settle(dispatcher, 100ms); CHECK_EQ(world->starts.load(), 8u);
    REQUIRE(controller.Request(false).ok()); REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 11 && Idle(notes); }));
    Settle(dispatcher, 100ms); CHECK_EQ(world->starts.load(), 11u);
    for (const auto& note : notes) {
        CHECK_FALSE(note.active); CHECK(note.status.find(L"private marker") == std::wstring::npos);
    }
}

AETHER_TEST("taskbar controller: retry delay prevents timer bursts from hammering Explorer") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->failStarts = 10;
    TaskbarController controller(dispatcher, FixtureLauncher(), [](const TaskbarUpdate&) {}, FakeOptions(world, 2s));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 1; }));
    for (int i = 0; i < 100; ++i) REQUIRE(controller.Refresh().ok());
    Settle(dispatcher); CHECK_EQ(world->starts.load(), 1u);
}

AETHER_TEST("taskbar controller: a pending retry runs on time without any refresh") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->failStarts = 1;
    std::vector<TaskbarUpdate> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { notes.push_back(update); },
                                 FakeOptions(world, 80ms));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 2 && !notes.empty() && notes.back().active; }));
    std::lock_guard lock(world->mutex);
    REQUIRE_EQ(world->startTimes.size(), 2u);
    CHECK(world->startTimes[1] - world->startTimes[0] >= 75ms);
}

AETHER_TEST("taskbar controller: after the attempts are exhausted nothing is scheduled and nothing runs") {
    // Regression for the poll-timer finding: the final update tells the application to stop its
    // timer, and the worker no longer wakes by itself.
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->failStarts = 100;
    std::vector<TaskbarUpdate> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { notes.push_back(update); },
                                 FakeOptions(world, 20ms));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 3 && Idle(notes); }));
    for (std::size_t index = 0; index + 1 < notes.size(); ++index) CHECK(notes[index].scheduled);
    CHECK(notes.back().status.find(L"off and on") != std::wstring::npos);
    const auto observed = world->observations.load();
    Settle(dispatcher, 300ms);
    CHECK_EQ(world->starts.load(), 3u); CHECK_EQ(world->observations.load(), observed);
    CHECK(Idle(notes));
}

AETHER_TEST("taskbar controller: an Explorer restart retries sooner, then backs off to the ordinary delay") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    std::vector<TaskbarUpdate> notes;
    auto options = FakeOptions(world, 400ms); options.restartRetryDelay = 50ms;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { notes.push_back(update); }, options);
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return !notes.empty() && notes.back().active; }));
    world->failStarts = 2;
    REQUIRE(controller.Restart().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 4 && notes.back().active; }));
    CHECK_EQ(world->destroyed.load(), 1u);
    std::lock_guard lock(world->mutex);
    REQUIRE_EQ(world->startTimes.size(), 4u);
    const auto first = world->startTimes[2] - world->startTimes[1];
    const auto second = world->startTimes[3] - world->startTimes[2];
    CHECK(first >= 45ms); CHECK(first < 400ms);  // the quick first retry, not the ordinary delay
    CHECK(second >= 95ms);                       // doubled
}

AETHER_TEST("taskbar controller: helper death restores fallback and restart replaces an active session") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    std::vector<bool> active;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { active.push_back(update.active); },
                                 FakeOptions(world, 300ms));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return !active.empty() && active.back(); }));
    world->running = false; REQUIRE(controller.Refresh().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1 && !active.back(); }));
    // The reconnection is timed by the worker; no further Refresh is needed.
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 2 && active.back(); }));
    REQUIRE(controller.Restart().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 3 && world->destroyed == 2 && active.back(); }));
    CHECK_FALSE(world->touchedUi);
}

AETHER_TEST("taskbar controller: restarting cancelled activation cannot report the old session active") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->blockFirstStart = true; world->lateSessionOnCancel = true;
    unsigned activeCallbacks = 0;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { if (update.active) ++activeCallbacks; }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 1; }));
    REQUIRE(controller.Restart().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 2 && activeCallbacks == 1; }));
    CHECK_EQ(world->destroyed.load(), 1u); CHECK_EQ(world->stops.load(), 1u); CHECK(world->cancellationSeen);
}

AETHER_TEST("taskbar controller: failed active callback delivery restores and prevents hidden retries") {
    GateDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->blockFirstStart = true;
    unsigned callbacks = 0;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { if (update.active) ++callbacks; }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!world->starts && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    REQUIRE_EQ(world->starts.load(), 1u);
    dispatcher.accept = false; world->release = true;
    REQUIRE(Pump(dispatcher.queue, [&] { return world->destroyed == 1; }));
    CHECK_EQ(callbacks, 0u); CHECK_EQ(world->stops.load(), 1u); CHECK_FALSE(world->touchedUi);
    CHECK_FALSE(controller.Request(true).ok());
    CHECK_FALSE(controller.Suspend().ok());
    for (int i = 0; i < 5; ++i) REQUIRE(controller.Refresh().ok());
    Settle(dispatcher.queue); CHECK_EQ(world->starts.load(), 1u);
}

AETHER_TEST("taskbar controller: failed restoration retains active ownership until a later worker retry") {
    // The restoration retry is timed by the worker itself (it used to wait for a UI Refresh).
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    std::vector<bool> active;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { active.push_back(update.active); },
                                 FakeOptions(world, 300ms));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return !active.empty() && active.back(); }));
    world->failStops = 1;
    REQUIRE(controller.Request(false).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->stops == 1; }));
    CHECK(active.back()); CHECK_EQ(world->destroyed.load(), 0u);
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1 && !active.back(); }));
    CHECK_EQ(world->stops.load(), 2u); CHECK_FALSE(world->touchedUi);
}

AETHER_TEST("taskbar controller: suspend shows the taskbar through restoration and resume activates again") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    std::vector<TaskbarUpdate> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { notes.push_back(update); },
                                 FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return !notes.empty() && notes.back().active; }));
    CHECK_FALSE(controller.Suspended());
    REQUIRE(controller.Suspend().ok());
    CHECK(controller.Suspended());
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1 && Idle(notes); }));
    CHECK(notes.back().suspended);
    CHECK(notes.back().status.find(L"temporarily") != std::wstring::npos);
    // While suspended: no activation, retry, restart or repeated request reaches a session.
    for (int i = 0; i < 10; ++i) REQUIRE(controller.Refresh().ok());
    REQUIRE(controller.Restart().ok());
    REQUIRE(controller.Request(true).ok());
    REQUIRE(controller.Suspend().ok());
    Settle(dispatcher, 100ms);
    CHECK_EQ(world->starts.load(), 1u); CHECK(controller.Suspended());
    REQUIRE(controller.Resume().ok());
    CHECK_FALSE(controller.Suspended());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 2 && notes.back().active; }));
    CHECK_FALSE(notes.back().suspended);
    REQUIRE(controller.Resume().ok());  // not suspended: no-op
    Settle(dispatcher); CHECK_EQ(world->starts.load(), 2u);
    CHECK_FALSE(world->touchedUi);
}

AETHER_TEST("taskbar controller: suspension skips pending retries and resume grants a fresh budget") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->failStarts = 100;
    std::vector<TaskbarUpdate> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { notes.push_back(update); },
                                 FakeOptions(world, 150ms));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 1 && notes.size() >= 2; }));
    CHECK(notes.back().scheduled);
    REQUIRE(controller.Suspend().ok());
    REQUIRE(Pump(dispatcher, [&] { return Idle(notes) && notes.back().suspended; }));
    Settle(dispatcher, 400ms);
    CHECK_EQ(world->starts.load(), 1u);
    REQUIRE(controller.Resume().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 4 && Idle(notes); }));
    CHECK_FALSE(notes.back().suspended);
}

AETHER_TEST("taskbar controller: suspend is a no-op while disabled and turning off ends a suspension") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    std::vector<TaskbarUpdate> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](const TaskbarUpdate& update) { notes.push_back(update); },
                                 FakeOptions(world));
    REQUIRE(controller.Suspend().ok());
    CHECK_FALSE(controller.Suspended());
    Settle(dispatcher); CHECK(notes.empty()); CHECK_EQ(world->starts.load(), 0u);
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return !notes.empty() && notes.back().active; }));
    REQUIRE(controller.Suspend().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1 && Idle(notes); }));
    REQUIRE(controller.Request(false).ok());
    CHECK_FALSE(controller.Suspended());
    REQUIRE(Pump(dispatcher, [&] { return Idle(notes) && !notes.back().suspended; }));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 2 && notes.back().active; }));
    CHECK_FALSE(notes.back().suspended);
}

AETHER_TEST("taskbar controller: foreign thread requests are rejected without session creation") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    TaskbarController controller(dispatcher, FixtureLauncher(), [](const TaskbarUpdate&) {}, FakeOptions(world));
    bool rejected = false;
    std::thread caller([&] {
        rejected = !controller.Request(true) && !controller.Refresh() && !controller.Restart() &&
                   !controller.Suspend() && !controller.Resume();
    });
    caller.join(); CHECK(rejected); CHECK_EQ(world->starts.load(), 0u);
}
