// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// In-memory sessions only: these tests never enumerate, hide or modify the native taskbar.
#include "AetherTest.h"
#include "app/TaskbarController.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
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

struct Notification { bool active; std::wstring status; };

bool Pump(ManualDispatcher& dispatcher, const std::function<bool()>& ready) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!ready() && std::chrono::steady_clock::now() < end) {
        dispatcher.RunPending(); std::this_thread::sleep_for(1ms);
    }
    dispatcher.RunPending(); return ready();
}

void Settle(ManualDispatcher& dispatcher) {
    const auto end = std::chrono::steady_clock::now() + 30ms;
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

}  // namespace

AETHER_TEST("taskbar controller: activation and restoration never block or touch native state on UI") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->blockFirstStart = true;
    std::vector<Notification> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](bool active, std::wstring status) {
        CHECK(dispatcher.IsUiThread()); notes.push_back({active, std::move(status)});
    }, FakeOptions(world));
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(controller.Request(true).ok());
    CHECK(std::chrono::steady_clock::now() - start < 100ms);
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 1 && !notes.empty(); }));
    CHECK_FALSE(notes.back().active);
    for (int i = 0; i < 100; ++i) REQUIRE(controller.Refresh().ok());
    world->release = true;
    REQUIRE(Pump(dispatcher, [&] { return !notes.empty() && notes.back().active; }));
    REQUIRE(controller.Request(false).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1 && !notes.back().active; }));
    CHECK_FALSE(world->touchedUi);
    controller.Stop();
}

AETHER_TEST("taskbar controller: cancelled late activation is stopped without a stale active callback") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->blockFirstStart = true; world->lateSessionOnCancel = true;
    std::vector<bool> active;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](bool value, std::wstring) { active.push_back(value); }, FakeOptions(world));
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
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](bool, std::wstring) { ++callbacks; }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!world->starts && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    REQUIRE_EQ(world->starts.load(), 1u);
    controller.Stop(); dispatcher.RunPending();
    CHECK_EQ(callbacks, 0u); CHECK(world->cancellationSeen);
    CHECK_EQ(world->destroyed.load(), 1u); CHECK_EQ(world->stops.load(), 1u); CHECK_FALSE(world->touchedUi);
    CHECK_FALSE(controller.Request(true).ok());
}

AETHER_TEST("taskbar controller: refresh retries are bounded and explicit restart resets the budget") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->failStarts = 100;
    std::vector<Notification> notes;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](bool active, std::wstring status) {
        notes.push_back({active, std::move(status)});
    }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    for (unsigned count = 1; count <= 3; ++count) {
        REQUIRE(Pump(dispatcher, [&] { return world->starts >= count && notes.size() >= count + 1; }));
        REQUIRE(controller.Refresh().ok());
    }
    for (int i = 0; i < 20; ++i) REQUIRE(controller.Refresh().ok());
    Settle(dispatcher); CHECK_EQ(world->starts.load(), 3u);
    REQUIRE(controller.Restart().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 4; }));
    REQUIRE(controller.Request(false).ok()); REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 5; }));
    for (const auto& note : notes) {
        CHECK_FALSE(note.active); CHECK(note.status.find(L"private marker") == std::wstring::npos);
    }
}

AETHER_TEST("taskbar controller: retry delay prevents timer bursts from hammering Explorer") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->failStarts = 10;
    TaskbarController controller(dispatcher, FixtureLauncher(), [](bool, std::wstring) {}, FakeOptions(world, 2s));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 1; }));
    for (int i = 0; i < 100; ++i) REQUIRE(controller.Refresh().ok());
    Settle(dispatcher); CHECK_EQ(world->starts.load(), 1u);
}

AETHER_TEST("taskbar controller: helper death restores fallback and restart replaces an active session") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    std::vector<bool> active;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](bool value, std::wstring) { active.push_back(value); }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return !active.empty() && active.back(); }));
    world->running = false; REQUIRE(controller.Refresh().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1 && !active.back(); }));
    REQUIRE(controller.Refresh().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 2 && active.back(); }));
    REQUIRE(controller.Restart().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->starts == 3 && world->destroyed == 2 && active.back(); }));
    CHECK_FALSE(world->touchedUi);
}

AETHER_TEST("taskbar controller: restarting cancelled activation cannot report the old session active") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>(); world->blockFirstStart = true; world->lateSessionOnCancel = true;
    unsigned activeCallbacks = 0;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](bool value, std::wstring) { if (value) ++activeCallbacks; }, FakeOptions(world));
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
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](bool value, std::wstring) { if (value) ++callbacks; }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!world->starts && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    REQUIRE_EQ(world->starts.load(), 1u);
    dispatcher.accept = false; world->release = true;
    REQUIRE(Pump(dispatcher.queue, [&] { return world->destroyed == 1; }));
    CHECK_EQ(callbacks, 0u); CHECK_EQ(world->stops.load(), 1u); CHECK_FALSE(world->touchedUi);
    CHECK_FALSE(controller.Request(true).ok());
    for (int i = 0; i < 5; ++i) REQUIRE(controller.Refresh().ok());
    Settle(dispatcher.queue); CHECK_EQ(world->starts.load(), 1u);
}

AETHER_TEST("taskbar controller: failed restoration retains active ownership until a later worker retry") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    std::vector<bool> active;
    TaskbarController controller(dispatcher, FixtureLauncher(), [&](bool value, std::wstring) { active.push_back(value); }, FakeOptions(world));
    REQUIRE(controller.Request(true).ok());
    REQUIRE(Pump(dispatcher, [&] { return !active.empty() && active.back(); }));
    world->failStops = 1;
    REQUIRE(controller.Request(false).ok());
    REQUIRE(Pump(dispatcher, [&] { return world->stops == 1; }));
    CHECK(active.back()); CHECK_EQ(world->destroyed.load(), 0u);
    REQUIRE(controller.Refresh().ok());
    REQUIRE(Pump(dispatcher, [&] { return world->destroyed == 1 && !active.back(); }));
    CHECK_EQ(world->stops.load(), 2u); CHECK_FALSE(world->touchedUi);
}

AETHER_TEST("taskbar controller: foreign thread requests are rejected without session creation") {
    ManualDispatcher dispatcher;
    auto world = std::make_shared<FakeWorld>();
    TaskbarController controller(dispatcher, FixtureLauncher(), [](bool, std::wstring) {}, FakeOptions(world));
    bool rejected = false;
    std::thread caller([&] { rejected = !controller.Request(true) && !controller.Refresh() && !controller.Restart(); });
    caller.join(); CHECK(rejected); CHECK_EQ(world->starts.load(), 0u);
}
