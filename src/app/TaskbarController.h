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

class TaskbarController {
public:
    using Callback = std::function<void(bool active, std::wstring status)>;
    using SessionFactory = std::function<Result<std::unique_ptr<ITaskbarSession>>(
        const std::filesystem::path&, const CancellationToken&)>;
    struct Options {
        SessionFactory createSession;
        std::chrono::milliseconds retryDelay{2000};
        unsigned maxAttempts = 3;
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
    // or Restart, resets the retry budget; Refresh retries only after its delay.
    Result<void> Request(bool enabled) noexcept;
    Result<void> Refresh() noexcept;
    Result<void> Restart() noexcept;  // e.g. TaskbarCreated; no-op while replacement is disabled

    // Blocking shutdown only, never from a controller worker. Cancels activation, suppresses every
    // queued UI callback, then joins after restoration/session destruction on the worker.
    void Stop() noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;
    std::unique_ptr<WorkerPool> worker_;
};

}  // namespace aether::app
