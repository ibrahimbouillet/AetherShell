// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/TaskbarController.h"

#include "platform/windows/TaskbarReplacement.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
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

}  // namespace

struct TaskbarController::State : std::enable_shared_from_this<TaskbarController::State> {
    using Clock = std::chrono::steady_clock;
    IUiDispatcher& dispatcher;
    std::filesystem::path launcher;
    Callback changed;
    Options options;
    std::atomic<bool> alive{true}, active{false}, deliveryAvailable{true};
    std::atomic<std::uint64_t> generation{0};
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, enabled = false, reconcile = false, refresh = false, restart = false;
    CancellationSource cancellation;
    // Everything below is accessed and destroyed on the single worker only.
    std::unique_ptr<ITaskbarSession> session;
    std::uint64_t sessionGeneration = 0;
    bool mustStop = false;
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
    }

    bool Current(std::uint64_t value) const noexcept {
        return alive.load() && generation.load() == value;
    }

    bool Emit(std::uint64_t value, bool isActive, std::wstring status) noexcept {
        if (!Current(value)) return true;  // obsolete work needs no notification
        try {
            return dispatcher.Post([self = shared_from_this(), value, isActive, status = std::move(status)]() mutable {
                if (!self->Current(value)) return;
                try { if (self->changed) self->changed(isActive, std::move(status)); } catch (...) {}
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

    void Publish(std::uint64_t value, bool isActive, const wchar_t* message) noexcept {
        if (!Emit(value, isActive, message)) FailClosed();
    }

    void Restore(std::uint64_t value) {
        ++stopAttempts;
        if (!StopSession()) {
            retryAt = Clock::now() + options.retryDelay;
            Publish(value, active.load(), L"Windows taskbar restoration is pending. Retry or close Aether Shell.");
        } else {
            Publish(value, false, L"Windows taskbar restored.");
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
            Publish(value, true, L"Windows taskbar replacement is active.");
        } else if (Current(value)) {
            retryAt = Clock::now() + options.retryDelay;
            Publish(value, false, attempts < options.maxAttempts
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
                    wake.wait(lock, [&] { return stopping || reconcile || refresh || restart; });
                    if (stopping) break;
                    wanted = enabled; changedRequest = reconcile; restartRequest = restart;
                    value = generation.load(); token = cancellation.Token();
                    reconcile = false; refresh = false; restart = false;
                }
                if (!Current(value)) continue;
                if (changedRequest || restartRequest) { attempts = 0; stopAttempts = 0; retryAt = {}; }
                if (session && (restartRequest || sessionGeneration != value)) mustStop = true;
                if (!wanted || mustStop) {
                    if (session && (!changedRequest && !restartRequest) &&
                        (Clock::now() < retryAt || stopAttempts >= options.maxAttempts)) continue;
                    Restore(value);
                    if (session || !wanted || !Current(value)) continue;
                }
                if (session) {
                    Result<bool> running = MakeError(ErrorCode::Internal, "taskbar guard observation failed");
                    try { running = session->Running(); } catch (...) {}
                    if (running && *running) continue;
                    mustStop = true;
                    Restore(value);
                    retryAt = Clock::now() + options.retryDelay;
                    if (!session && Current(value)) Publish(value, false, attempts < options.maxAttempts
                        ? L"Windows taskbar restored. Replacement will reconnect shortly."
                        : L"Windows taskbar restored. Turn replacement off and on to retry.");
                    continue;
                }
                if (deliveryAvailable.load() && attempts < options.maxAttempts && Clock::now() >= retryAt)
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
            Publish(generation.load(), false, restored ? L"Taskbar replacement stopped. Windows taskbar restored."
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
            if (state_->enabled == enabled && (enabled || !state_->active)) return Ok();
            previous = state_->cancellation; state_->cancellation = CancellationSource{};
            const auto value = ++state_->generation;
            accepted = state_->Emit(value, state_->active.load(), enabled
                ? L"Activating taskbar replacement..." : L"Restoring Windows taskbar...");
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
          if (!state_->enabled && !state_->active) return Ok();
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
          if (!state_->enabled) return Ok();
          previous = state_->cancellation; state_->cancellation = CancellationSource{};
          const auto value = ++state_->generation;
          accepted = state_->Emit(value, state_->active.load(), L"Reconnecting taskbar replacement...");
          state_->enabled = accepted; state_->reconcile = true; state_->restart = accepted;
          if (!accepted) state_->deliveryAvailable = false; }
        previous.Cancel(); state_->wake.notify_one();
        return accepted ? Ok() : Result<void>(MakeError(ErrorCode::Unavailable, "taskbar feedback is unavailable"));
    } catch (...) { return MakeError(ErrorCode::Internal, "taskbar restart failed"); }
}

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
