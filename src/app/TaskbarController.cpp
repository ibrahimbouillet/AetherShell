// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/TaskbarController.h"

#include "platform/windows/TaskbarReplacement.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace aether::app {
namespace {

class NativeSession final : public ITaskbarSession {
public:
    explicit NativeSession(std::unique_ptr<platform::TaskbarGuardSession> session) : session_(std::move(session)) {}
    Result<bool> Running() override { return session_->Running(); }
    Result<void> Stop() override { return session_->Stop(); }
private:
    std::unique_ptr<platform::TaskbarGuardSession> session_;
};

Result<std::unique_ptr<ITaskbarSession>> StartNative(const std::filesystem::path& launcher,
                                                   const std::filesystem::path& baselineFile,
                                                   const CancellationToken& cancellation) {
    auto started = platform::TaskbarGuardSession::Start(launcher, baselineFile, cancellation);
    if (!started) return started.error();
    return std::unique_ptr<ITaskbarSession>(std::make_unique<NativeSession>(std::move(*started)));
}

// A timed worker wake waits at least this long, even when the retry time has already passed, so a
// logic slip can never turn the worker into a busy loop.
constexpr std::chrono::milliseconds kMinimumTimedWake{10};

}  // namespace

struct TaskbarController::State : std::enable_shared_from_this<TaskbarController::State> {
    using Clock = std::chrono::steady_clock;
    IUiDispatcher& dispatcher;
    std::filesystem::path launcher;
    Callback changed;
    Options options;
    std::atomic<bool> alive{true}, active{false}, deliveryAvailable{true}, suspended{false};
    std::atomic<std::uint64_t> generation{0};
    std::mutex mutex;
    std::condition_variable wake;
    // The mailbox below and `suspended` are written under `mutex` on the UI thread.
    bool stopping = false, enabled = false, reconcile = false, refresh = false, restart = false;
    CancellationSource cancellation;
    // Everything below is accessed and destroyed on the single worker only.
    std::unique_ptr<ITaskbarSession> session;
    std::uint64_t sessionGeneration = 0;
    bool mustStop = false;
    bool restartSeries = false;  // the retry budget was last reset by Restart (Explorer restarted)
    unsigned attempts = 0, stopAttempts = 0;
    Clock::time_point retryAt{};

    State(IUiDispatcher& ui, std::filesystem::path path, Callback callback, Options settings)
        : dispatcher(ui), launcher(std::move(path)), changed(std::move(callback)), options(std::move(settings)) {
        if (!options.createSession) {
            options.createSession = [file = options.baselineFile](const std::filesystem::path& path,
                                                                  const CancellationToken& token) {
                return StartNative(path, file, token);
            };
        }
        options.retryDelay = std::clamp(options.retryDelay, std::chrono::milliseconds(0),
                                        std::chrono::milliseconds(60000));
        options.maxAttempts = std::clamp(options.maxAttempts, 1u, 8u);
        options.restartRetryDelay = std::clamp(options.restartRetryDelay, std::chrono::milliseconds(0),
                                               options.retryDelay);
        options.restartExtraAttempts = std::min(options.restartExtraAttempts, 4u);
    }

    bool Current(std::uint64_t value) const noexcept {
        return alive.load() && generation.load() == value;
    }

    // Worker only.
    unsigned Budget() const noexcept {
        return options.maxAttempts + (restartSeries ? options.restartExtraAttempts : 0u);
    }
    // Worker only, after at least one attempt of the current series. An Explorer-restart series
    // starts with the short delay and doubles it, up to the ordinary delay.
    std::chrono::milliseconds RetryDelay() const noexcept {
        if (!restartSeries) return options.retryDelay;
        auto delay = options.restartRetryDelay;
        for (unsigned retry = 1; retry < attempts && delay < options.retryDelay; ++retry) delay *= 2;
        return std::min(delay, options.retryDelay);
    }
    // Worker only, under `mutex`. When the worker has to act again without a UI request: a pending
    // restoration or activation retry. A running session is observed through Refresh instead, and
    // after the budget is exhausted the worker sleeps until the next request.
    std::optional<Clock::time_point> NextRetry(bool wanted) const {
        const bool pending = session ? (mustStop && stopAttempts < options.maxAttempts)
                                     : (wanted && deliveryAvailable.load() && attempts < Budget());
        if (!pending) return std::nullopt;
        return std::max(retryAt, Clock::now() + kMinimumTimedWake);
    }

    TaskbarUpdate MakeUpdate(bool isActive, bool scheduled, const wchar_t* message) const {
        TaskbarUpdate update;
        update.active = isActive; update.scheduled = scheduled; update.suspended = suspended.load();
        update.status = message;
        return update;
    }

    bool Emit(std::uint64_t value, TaskbarUpdate update) noexcept {
        if (!Current(value)) return true;  // obsolete work needs no notification
        try {
            return dispatcher.Post([self = shared_from_this(), value, update = std::move(update)]() mutable {
                if (!self->Current(value)) return;
                try { if (self->changed) self->changed(update); } catch (...) {}
            });
        } catch (...) { return false; }
    }

    Result<void> StopSession() noexcept {
        if (!session) { active = false; mustStop = false; return Ok(); }
        try {
            auto stopped = session->Stop();
            if (!stopped) { mustStop = true; return stopped; }
            session.reset(); active = false; mustStop = false; stopAttempts = 0;
            return Ok();
        } catch (...) {
            mustStop = true;
            return MakeError(ErrorCode::Internal, "Windows taskbar restoration failed");
        }
    }

    void FailClosed() noexcept {
        deliveryAvailable = false;
        try {
            auto previous = [&] {
                std::lock_guard lock(mutex); enabled = false; ++generation; return cancellation;
            }();
            previous.Cancel();
            (void)StopSession();
        } catch (...) {}
    }

    void Publish(std::uint64_t value, bool isActive, bool scheduled, const wchar_t* message) noexcept {
        bool delivered = false;
        try { delivered = Emit(value, MakeUpdate(isActive, scheduled, message)); } catch (...) {}
        if (!delivered) FailClosed();
    }

    void Restore(std::uint64_t value, bool wanted) {
        ++stopAttempts;
        if (!StopSession()) {
            retryAt = Clock::now() + options.retryDelay;
            Publish(value, active.load(), stopAttempts < options.maxAttempts,
                    L"Windows taskbar restoration is pending. Retry or close Aether Shell.");
        } else {
            Publish(value, false, wanted && deliveryAvailable.load() && attempts < Budget(),
                    suspended.load() ? L"The Windows taskbar is shown temporarily."
                                     : L"Windows taskbar restored.");
        }
    }

    void Activate(std::uint64_t value, const CancellationToken& token) {
        if (!Current(value) || token.IsCancelled()) return;
        ++attempts;
        Result<std::unique_ptr<ITaskbarSession>> started = MakeError(ErrorCode::Internal, "taskbar guard failed");
        try { started = options.createSession(launcher, token); } catch (...) {}
        if (started && *started) {
            session = std::move(*started); sessionGeneration = value; active = true;
            if (!Current(value) || token.IsCancelled()) { (void)StopSession(); return; }
            Publish(value, true, true, L"Windows taskbar replacement is active.");
        } else if (Current(value)) {
            retryAt = Clock::now() + RetryDelay();
            const bool again = attempts < Budget();
            Publish(value, false, again, again
                ? L"Windows taskbar remains available. Replacement will retry shortly."
                : L"Windows taskbar remains available. Turn replacement off and on to retry.");
        }
    }

    void Run() noexcept {
        bool failed = false;
        try {
            for (;;) {
                bool wanted = false, changedRequest = false, restartRequest = false;
                std::uint64_t value = 0;
                CancellationToken token;
                {
                    std::unique_lock lock(mutex);
                    const auto requested = [&] { return stopping || reconcile || refresh || restart; };
                    // A timeout is handled like Refresh: retries are timed here, not by a UI timer.
                    if (const auto deadline = NextRetry(enabled && !suspended.load())) (void)wake.wait_until(lock, *deadline, requested);
                    else wake.wait(lock, requested);
                    if (stopping) break;
                    wanted = enabled && !suspended.load(); changedRequest = reconcile; restartRequest = restart;
                    value = generation.load(); token = cancellation.Token();
                    reconcile = false; refresh = false; restart = false;
                }
                if (!Current(value)) continue;
                if (changedRequest || restartRequest) {
                    attempts = 0; stopAttempts = 0; retryAt = {}; restartSeries = restartRequest;
                }
                if (session && (restartRequest || sessionGeneration != value)) mustStop = true;
                if (!wanted || mustStop) {
                    if (session && (!changedRequest && !restartRequest) &&
                        (Clock::now() < retryAt || stopAttempts >= options.maxAttempts)) continue;
                    Restore(value, wanted);
                    if (session || !wanted || !Current(value)) continue;
                }
                if (session) {
                    Result<bool> running = MakeError(ErrorCode::Internal, "taskbar guard observation failed");
                    try { running = session->Running(); } catch (...) {}
                    if (running && *running) continue;
                    mustStop = true;
                    Restore(value, wanted);
                    retryAt = Clock::now() + RetryDelay();
                    if (!session && Current(value)) {
                        const bool again = deliveryAvailable.load() && attempts < Budget();
                        Publish(value, false, again, again
                            ? L"Windows taskbar restored. Replacement will reconnect shortly."
                            : L"Windows taskbar restored. Turn replacement off and on to retry.");
                    }
                    continue;
                }
                if (deliveryAvailable.load() && attempts < Budget() && Clock::now() >= retryAt)
                    Activate(value, token);
            }
        } catch (...) {
            failed = true;
        }
        // Pool shutdown discards queued tasks, but this persistent task owns its restoration before
        // returning. No native session or guard handle is destroyed on the caller/UI thread.
        const bool restored = StopSession().ok();
        session.reset();
        active = false;
        if (failed) {
            deliveryAvailable = false;
            { std::lock_guard lock(mutex); stopping = true; }
            Publish(generation.load(), false, false, restored ? L"Taskbar replacement stopped. Windows taskbar restored."
                : L"Taskbar replacement stopped. Windows taskbar restoration could not be confirmed.");
        }
    }
};

TaskbarController::TaskbarController(IUiDispatcher& dispatcher, std::filesystem::path launcher, Callback changed)
    : TaskbarController(dispatcher, std::move(launcher), std::move(changed), Options{}) {}

TaskbarController::TaskbarController(IUiDispatcher& dispatcher, std::filesystem::path launcher, Callback changed,
                                   Options options)
    : state_(std::make_shared<State>(dispatcher, std::move(launcher), std::move(changed), std::move(options))),
      worker_(std::make_unique<WorkerPool>(WorkerPool::Options{1, L"Aether taskbar controller", ComApartment::None})) {
    if (!worker_->Post([state = state_] { state->Run(); })) throw std::runtime_error("taskbar worker unavailable");
}

TaskbarController::~TaskbarController() { Stop(); }

Result<void> TaskbarController::Request(bool enabled) noexcept {
    if (!state_->dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "taskbar requests need the UI thread");
    try {
        CancellationSource previous;
        bool accepted = true;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->stopping) return MakeError(ErrorCode::InvalidState, "taskbar controller is stopped");
            if (!state_->deliveryAvailable) return MakeError(ErrorCode::Unavailable, "taskbar feedback is unavailable");
            // Requesting the current state again keeps a suspension (settings are re-applied often).
            if (state_->enabled == enabled && (enabled || !state_->active)) return Ok();
            previous = state_->cancellation; state_->cancellation = CancellationSource{};
            const auto value = ++state_->generation;
            if (!enabled) state_->suspended = false;  // turning replacement off ends a suspension
            accepted = state_->Emit(value, state_->MakeUpdate(state_->active.load(), true, enabled
                ? L"Activating taskbar replacement..." : L"Restoring Windows taskbar..."));
            state_->enabled = enabled && accepted; state_->reconcile = true; state_->restart = false;
            if (!accepted) state_->deliveryAvailable = false;
        }
        previous.Cancel(); state_->wake.notify_one();
        return accepted ? Ok() : Result<void>(MakeError(ErrorCode::Unavailable, "taskbar feedback is unavailable"));
    } catch (...) { return MakeError(ErrorCode::Internal, "taskbar request failed"); }
}

Result<void> TaskbarController::Refresh() noexcept {
    if (!state_->dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "taskbar refresh needs the UI thread");
    try {
        { std::lock_guard lock(state_->mutex);
          if (state_->stopping) return MakeError(ErrorCode::InvalidState, "taskbar controller is stopped");
          if ((!state_->enabled || state_->suspended) && !state_->active) return Ok();
          state_->refresh = true; }
        state_->wake.notify_one(); return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "taskbar refresh failed"); }
}

Result<void> TaskbarController::Restart() noexcept {
    if (!state_->dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "taskbar restart needs the UI thread");
    try {
        CancellationSource previous;
        bool accepted = true;
        { std::lock_guard lock(state_->mutex);
          if (state_->stopping) return MakeError(ErrorCode::InvalidState, "taskbar controller is stopped");
          if (!state_->enabled || state_->suspended) return Ok();
          previous = state_->cancellation; state_->cancellation = CancellationSource{};
          const auto value = ++state_->generation;
          accepted = state_->Emit(value, state_->MakeUpdate(state_->active.load(), true, L"Reconnecting taskbar replacement..."));
          state_->enabled = accepted; state_->reconcile = true; state_->restart = accepted;
          if (!accepted) state_->deliveryAvailable = false; }
        previous.Cancel(); state_->wake.notify_one();
        return accepted ? Ok() : Result<void>(MakeError(ErrorCode::Unavailable, "taskbar feedback is unavailable"));
    } catch (...) { return MakeError(ErrorCode::Internal, "taskbar restart failed"); }
}

Result<void> TaskbarController::Suspend() noexcept {
    if (!state_->dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "taskbar suspension needs the UI thread");
    try {
        CancellationSource previous;
        bool accepted = true;
        { std::lock_guard lock(state_->mutex);
          if (state_->stopping) return MakeError(ErrorCode::InvalidState, "taskbar controller is stopped");
          if (!state_->deliveryAvailable) return MakeError(ErrorCode::Unavailable, "taskbar feedback is unavailable");
          if (!state_->enabled || state_->suspended) return Ok();
          previous = state_->cancellation; state_->cancellation = CancellationSource{};
          const auto value = ++state_->generation;
          state_->suspended = true;
          accepted = state_->Emit(value, state_->MakeUpdate(state_->active.load(), true,
                                                            L"Showing the Windows taskbar temporarily..."));
          // Without feedback the controller fails closed: the worker restores and stays disabled.
          if (!accepted) { state_->enabled = false; state_->suspended = false; state_->deliveryAvailable = false; }
          state_->reconcile = true; state_->restart = false; }
        previous.Cancel(); state_->wake.notify_one();
        return accepted ? Ok() : Result<void>(MakeError(ErrorCode::Unavailable, "taskbar feedback is unavailable"));
    } catch (...) { return MakeError(ErrorCode::Internal, "taskbar suspension failed"); }
}

Result<void> TaskbarController::Resume() noexcept {
    if (!state_->dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "taskbar resumption needs the UI thread");
    try {
        CancellationSource previous;
        bool accepted = true;
        { std::lock_guard lock(state_->mutex);
          if (state_->stopping) return MakeError(ErrorCode::InvalidState, "taskbar controller is stopped");
          if (!state_->deliveryAvailable) return MakeError(ErrorCode::Unavailable, "taskbar feedback is unavailable");
          if (!state_->suspended) return Ok();
          previous = state_->cancellation; state_->cancellation = CancellationSource{};
          const auto value = ++state_->generation;
          state_->suspended = false;
          accepted = state_->Emit(value, state_->MakeUpdate(state_->active.load(), true, L"Activating taskbar replacement..."));
          state_->enabled = state_->enabled && accepted; state_->reconcile = true; state_->restart = false;
          if (!accepted) state_->deliveryAvailable = false; }
        previous.Cancel(); state_->wake.notify_one();
        return accepted ? Ok() : Result<void>(MakeError(ErrorCode::Unavailable, "taskbar feedback is unavailable"));
    } catch (...) { return MakeError(ErrorCode::Internal, "taskbar resumption failed"); }
}

bool TaskbarController::Suspended() const noexcept { return state_ && state_->suspended.load(); }

void TaskbarController::Stop() noexcept {
    if (!state_ || !worker_) return;
    state_->alive = false;
    try {
        // Copy the existing shared cancellation state; allocating a fresh unused source during
        // shutdown could fail before setting stopping, leaving the persistent worker asleep.
        auto previous = [&] {
            std::lock_guard lock(state_->mutex); state_->stopping = true; ++state_->generation;
            return state_->cancellation;
        }();
        previous.Cancel(); state_->wake.notify_one(); worker_->Shutdown();
    } catch (...) { state_->wake.notify_one(); worker_->Shutdown(); }
}

}  // namespace aether::app
