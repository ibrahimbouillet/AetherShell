// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Cancellation.h"
#include "core/Dispatcher.h"
#include "core/Result.h"
#include "core/WorkerPool.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace aether::app {

// Created, observed, stopped and destroyed exclusively on the controller worker. Tests inject
// in-memory sessions; the production adapter owns platform::TaskbarGuardSession.
class ITaskbarSession {
public:
    virtual ~ITaskbarSession() = default;
    virtual Result<bool> Running() = 0;
    virtual Result<void> Stop() = 0;
};

// One status report, delivered on the UI thread.
struct TaskbarUpdate {
    bool active = false;     // a guard session runs: the Windows taskbar is hidden by Aether Shell
    // The controller acts again by itself, without a new UI request: a request is being carried
    // out, an activation or restoration retry is pending, or a running session is being observed.
    // false while inactive means nothing more will happen until Request/Restart/Resume.
    bool scheduled = false;
    bool suspended = false;  // replacement is wanted but temporarily suspended (Suspend)
    std::wstring status;     // plain language for Settings > Desktop; never contains error details
};

class TaskbarController {
public:
    using Callback = std::function<void(const TaskbarUpdate& update)>;
    using SessionFactory = std::function<Result<std::unique_ptr<ITaskbarSession>>(
        const std::filesystem::path&, const CancellationToken&)>;
    struct Options {
        SessionFactory createSession;
        std::chrono::milliseconds retryDelay{2000};
        unsigned maxAttempts = 3;
        // After Restart (Explorer's TaskbarCreated) the shell may still be starting. Retries in that
        // series start at restartRetryDelay and double up to retryDelay, with restartExtraAttempts
        // more attempts, so the first retry comes sooner without shortening the overall window.
        std::chrono::milliseconds restartRetryDelay{500};
        unsigned restartExtraAttempts = 2;
        // Used by the production session (D21): the baseline that recovery without Aether Shell
        // running restores. Ignored when createSession is injected.
        std::filesystem::path baselineFile;
    };

    TaskbarController(IUiDispatcher& dispatcher, std::filesystem::path versionedLauncher, Callback changed);
    TaskbarController(IUiDispatcher& dispatcher, std::filesystem::path versionedLauncher, Callback changed,
                      Options options);
    ~TaskbarController();
    TaskbarController(const TaskbarController&) = delete;
    TaskbarController& operator=(const TaskbarController&) = delete;

    // UI thread. These methods only update a worker mailbox and post UI feedback; they never wait
    // for Explorer or a guard process. Duplicate Refresh calls coalesce. Enabling after disabling,
    // Restart and Resume reset the retry budget. Pending retries are timed by the worker itself;
    // Refresh only asks it to observe a running session now (the application's poll timer).
    Result<void> Request(bool enabled) noexcept;
    Result<void> Refresh() noexcept;
    // e.g. TaskbarCreated: replaces a running session. No-op while disabled or suspended.
    Result<void> Restart() noexcept;
    // Runtime only, never persisted: shows the Windows taskbar through the normal restoration path
    // while replacement stays requested. Activation, retries and Restart are skipped until Resume
    // (or Request(false), which also ends the suspension). Both are no-ops when not applicable.
    Result<void> Suspend() noexcept;
    Result<void> Resume() noexcept;
    [[nodiscard]] bool Suspended() const noexcept;

    // Blocking shutdown only, never from a controller worker. Cancels activation, suppresses every
    // queued UI callback, then joins after restoration/session destruction on the worker.
    void Stop() noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;
    std::unique_ptr<WorkerPool> worker_;
};

}  // namespace aether::app
