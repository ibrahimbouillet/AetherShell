// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// One shared session per Start: listener, wake signal and worker remain alive until that worker has
// completely unwound. Lifecycle locks only protect session selection; listener closure and joining
// always happen after unlocking. Concurrent Stop calls quiesce the same session. Start never waits
// for a stopping worker: it returns InvalidState until all Stop calls and the old body have finished.
#pragma once

#include "core/Result.h"
#include "diagnostics/Logger.h"
#include "status/ProviderThread.h"
#include "status/SnapshotPublisher.h"
#include "status/StatusTypes.h"

#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace aether::status {

template <class Snapshot>
class ProviderHost {
public:
    using Listener = typename SnapshotPublisher<Snapshot>::Listener;
    using Equivalent = typename SnapshotPublisher<Snapshot>::Equivalent;
    using Body = std::function<void(HANDLE stopEvent, const std::shared_ptr<WakeSignal>& wake)>;

    ProviderHost() : state_(std::make_shared<State>()) {}
    ~ProviderHost() { Stop(); }
    ProviderHost(const ProviderHost&) = delete;
    ProviderHost& operator=(const ProviderHost&) = delete;

    [[nodiscard]] Result<void> Start(Listener listener, std::wstring threadName, Body body) {
        if (!listener || !body) return MakeError(ErrorCode::InvalidArgument, "status listener/body must not be empty");
        try {
            auto session = std::make_shared<Session>();
            auto wake = WakeSignal::Create();
            if (!wake) return wake.error();
            session->wake = std::move(wake.value());
            session->publisher->Open(std::move(listener));
            const auto state = state_;
            std::shared_ptr<Session> retired;
            {
                std::lock_guard lock(state->mutex);
                if (state->active || state->stopping != 0 ||
                    (state->retired && !state->retired->thread.IsFinished())) {
                    return MakeError(ErrorCode::InvalidState, "status provider is running or stopping");
                }
                state->active = session;
                auto started = session->thread.Start(std::move(threadName),
                    [session, body = std::move(body)](HANDLE stop) { RunBody(session, body, stop); },
                    [state, weak = std::weak_ptr<Session>(session)] {
                        const auto completed = weak.lock();
                        if (!completed) return;
                        std::lock_guard lock(state->mutex);
                        if (state->retired == completed && state->stopping == 0) state->retired.reset();
                    });
                if (!started) {
                    state->active.reset();
                    return started.error();
                }
                state->latest = session->publisher;
                retired = std::move(state->retired);
            }
            // The old thread is already finished; release/join it outside the lifecycle mutex.
            return Ok();
        } catch (...) {
            return MakeError(ErrorCode::Internal, "cannot start status provider");
        }
    }

    void Stop() noexcept {
        const auto state = state_;
        std::shared_ptr<Session> session;
        {
            std::lock_guard lock(state->mutex);
            if (state->active) state->retired = std::move(state->active);
            session = state->retired;
            if (!session) return;
            ++state->stopping;
        }
        // No lifecycle lock is held while waiting for a callback or joining. A shared retired
        // session lets every concurrent Stop wait for the same worker instead of returning early.
        session->thread.RequestStop();
        session->publisher->Close();
        session->thread.Stop();
        {
            std::lock_guard lock(state->mutex);
            --state->stopping;
            if (state->stopping == 0 && state->retired == session && session->thread.IsFinished()) {
                state->retired.reset();
            }
        }
    }

    void Wake() const noexcept {
        if (auto session = Active()) session->wake->Signal();
    }

    bool Publish(const Snapshot& snapshot, Equivalent equivalent = nullptr) {
        if (auto publisher = Latest()) return publisher->Publish(snapshot, equivalent);
        return false;
    }

    [[nodiscard]] Snapshot Current() const {
        if (auto publisher = Latest()) return publisher->Current();
        return {};
    }

private:
    struct Session {
        std::shared_ptr<SnapshotPublisher<Snapshot>> publisher = std::make_shared<SnapshotPublisher<Snapshot>>();
        std::shared_ptr<WakeSignal> wake;
        ProviderThread thread;
    };
    struct State {
        std::mutex mutex;
        std::shared_ptr<Session> active;
        std::shared_ptr<Session> retired;
        std::shared_ptr<SnapshotPublisher<Snapshot>> latest;
        std::size_t stopping = 0;
    };

    [[nodiscard]] std::shared_ptr<Session> Active() const {
        std::lock_guard lock(state_->mutex);
        return state_->active;
    }
    [[nodiscard]] std::shared_ptr<SnapshotPublisher<Snapshot>> Latest() const {
        std::lock_guard lock(state_->mutex);
        return state_->latest;
    }

    static void RunBody(const std::shared_ptr<Session>& session, const Body& body, HANDLE stop) noexcept {
        try {
            body(stop, session->wake);
            return;
        } catch (...) {
            // Platform exception text can contain private paths or identifiers.
            AETHER_LOG_ERROR("status", "status provider body failed");
        }
        try {
            Snapshot failed{};
            failed.state = ProviderState::Error;
            if constexpr (requires { failed.detail; }) failed.detail = "status provider failed";
            session->publisher->Publish(failed);
        } catch (...) {
            // Allocation failure while reporting must not escape the worker boundary.
        }
        ::WaitForSingleObject(stop, INFINITE);
    }

    std::shared_ptr<State> state_;
};

}  // namespace aether::status
