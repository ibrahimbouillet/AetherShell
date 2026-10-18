// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// A dedicated thread in the multithreaded COM apartment (MTA) that runs posted
// tasks in FIFO order, plus delayed tasks. Each audio endpoint monitor owns one:
// all of its Core Audio objects are created, used and released on it, and the
// Core Audio callbacks (which arrive on arbitrary system threads) only post work
// to it through a Poster.
//
// core/WorkerPool is not used because the providers need delayed tasks (the
// bounded rebinding retry and the health check), a "final task" that is
// guaranteed to run on the thread before it leaves the apartment, and a posting
// handle that COM callbacks can keep after the owner is gone.
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace aether::status::audio {

class MtaWorker {
public:
    using Task = std::function<void()>;

    struct State;

    // Copyable, thread-safe posting handle. It shares the worker's queue, so it stays safe to use
    // after the MtaWorker object is destroyed: posting then simply fails. COM callback objects,
    // whose lifetime is controlled by Windows, hold one of these instead of a pointer to the worker.
    class Poster {
    public:
        Poster() = default;
        bool Post(Task task) const;

    private:
        friend class MtaWorker;
        explicit Poster(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
        std::shared_ptr<State> state_;
    };

    MtaWorker();
    // Stop(). When destroyed from a task on its own thread (not supported by the audio providers, but
    // never undefined behaviour here) the thread is detached and exits after the current task.
    ~MtaWorker();
    MtaWorker(const MtaWorker&) = delete;
    MtaWorker& operator=(const MtaWorker&) = delete;

    // Starts the thread, which enters the MTA (CoInitializeEx) before running any task. A worker can
    // be started once; it cannot be restarted after Stop().
    [[nodiscard]] Result<void> Start(std::wstring threadName);

    // Thread-safe. Return false (and drop the task) when the worker is not running.
    bool Post(Task task);
    bool PostAfter(std::chrono::milliseconds delay, Task task);
    [[nodiscard]] Poster MakePoster() const { return Poster(state_); }

    // Discards queued and delayed tasks (they are destroyed on the calling thread), lets the task in
    // progress finish, runs `finalTask` on the worker, leaves the apartment and joins the thread.
    // Only the first call's `finalTask` is used. Called from a task on the worker itself it cannot
    // join: it returns immediately, the thread exits once the current task and `finalTask` have run,
    // and a later Stop() (or the destructor) on another thread performs the join. Idempotent and
    // thread-safe.
    void Stop(Task finalTask = {}) noexcept;

    [[nodiscard]] bool IsWorkerThread() const noexcept;

    // CoInitializeEx result of the worker thread. Meaningful from tasks (always set before the first
    // task runs); E_PENDING before that.
    [[nodiscard]] HRESULT ComStatus() const noexcept;

private:
    static void ThreadMain(std::shared_ptr<State> state, std::wstring threadName);

    std::shared_ptr<State> state_;
    // Recursive because discarding a control request may synchronously invoke a completion that
    // calls Stop() again on the same thread.
    std::recursive_mutex threadMutex_;
    std::thread thread_;
};

}  // namespace aether::status::audio
