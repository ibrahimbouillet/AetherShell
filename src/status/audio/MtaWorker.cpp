// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/audio/MtaWorker.h"

#include "diagnostics/Logger.h"

#include <objbase.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <system_error>
#include <utility>

namespace aether::status::audio {

// Shared between the owning MtaWorker, its thread and every Poster, so posting stays safe after the
// owner is gone and the thread can finish on its own when it could not be joined.
struct MtaWorker::State {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Task> queue;
    std::multimap<std::chrono::steady_clock::time_point, Task> timers;
    Task finalTask;
    bool running = false;        // Start() succeeded
    bool stopRequested = false;  // Stop() was called; no new work is accepted
    std::atomic<DWORD> threadId{0};
    std::atomic<HRESULT> comStatus{E_PENDING};

    bool Enqueue(Task task) {
        if (!task) return false;
        {
            std::lock_guard lock(mutex);
            if (!running || stopRequested) return false;
            queue.push_back(std::move(task));
        }
        wake.notify_one();
        return true;
    }
};

namespace {

void RunTask(MtaWorker::Task& task) noexcept {
    try {
        task();
    } catch (const std::exception&) {
        AETHER_LOG_ERROR("status.audio", "COM worker task threw an exception");
    } catch (...) {
        AETHER_LOG_ERROR("status.audio", "COM worker task threw a non-standard exception");
    }
}

}  // namespace

bool MtaWorker::Poster::Post(Task task) const { return state_ && state_->Enqueue(std::move(task)); }

MtaWorker::MtaWorker() : state_(std::make_shared<State>()) {}

MtaWorker::~MtaWorker() {
    Stop();
    std::lock_guard lock(threadMutex_);
    // Still joinable only when Stop() ran on the worker itself; the thread owns everything it needs
    // (the shared State), so letting it finish on its own is safe.
    if (thread_.joinable()) thread_.detach();
}

Result<void> MtaWorker::Start(std::wstring threadName) {
    std::lock_guard threadLock(threadMutex_);
    {
        std::lock_guard lock(state_->mutex);
        if (state_->running || state_->stopRequested) {
            return MakeError(ErrorCode::InvalidState, "the audio COM worker was already started");
        }
        state_->running = true;
    }
    try {
        thread_ = std::thread(&MtaWorker::ThreadMain, state_, std::move(threadName));
    } catch (const std::system_error& ex) {
        std::lock_guard lock(state_->mutex);
        state_->running = false;
        state_->stopRequested = true;
        return MakeError(ErrorCode::Platform, std::string("could not start the audio COM worker thread: ") + ex.what());
    }
    return Ok();
}

bool MtaWorker::Post(Task task) { return state_->Enqueue(std::move(task)); }

bool MtaWorker::PostAfter(std::chrono::milliseconds delay, Task task) {
    if (!task) return false;
    const auto due = std::chrono::steady_clock::now() + delay;
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->running || state_->stopRequested) return false;
        state_->timers.emplace(due, std::move(task));
    }
    state_->wake.notify_one();
    return true;
}

void MtaWorker::Stop(Task finalTask) noexcept {
    const bool onWorker = IsWorkerThread();
    // Serialize the whole external shutdown, including cancelled control completions. Worker-side
    // Stop must skip this lock because an external caller may already hold it while joining us.
    std::unique_lock<std::recursive_mutex> threadLock(threadMutex_, std::defer_lock);
    if (!onWorker) threadLock.lock();
    std::deque<Task> discardedQueue;
    std::multimap<std::chrono::steady_clock::time_point, Task> discardedTimers;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->running && !state_->stopRequested) {
            state_->finalTask = std::move(finalTask);
            discardedQueue.swap(state_->queue);
            discardedTimers.swap(state_->timers);
        }
        state_->stopRequested = true;
    }
    state_->wake.notify_all();
    // Destroy discarded work outside the lock: destructors may complete pending control requests.
    discardedQueue.clear();
    discardedTimers.clear();

    // Never self-join. The thread finishes the current task and `finalTask` on its own; a later
    // Stop() or the destructor on another thread joins it.
    if (onWorker) return;

    // Serialize joins as well as ownership of the thread. Moving it out and unlocking would let a
    // concurrent Stop() return while the first caller was still waiting for a running callback.
    if (!thread_.joinable()) return;
    try {
        thread_.join();
    } catch (const std::system_error&) {
        AETHER_LOG_ERROR("status.audio", "could not join the audio COM worker");
        thread_.detach();
    }
}

bool MtaWorker::IsWorkerThread() const noexcept { return state_->threadId.load() == ::GetCurrentThreadId(); }

HRESULT MtaWorker::ComStatus() const noexcept { return state_->comStatus.load(); }

void MtaWorker::ThreadMain(std::shared_ptr<State> state, std::wstring threadName) {
    state->threadId.store(::GetCurrentThreadId());
    ::SetThreadDescription(::GetCurrentThread(), threadName.c_str());
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    state->comStatus.store(com);

    for (bool last = false; !last;) {
        Task task;
        {
            std::unique_lock lock(state->mutex);
            for (;;) {
                if (state->stopRequested) {
                    task = std::exchange(state->finalTask, nullptr);
                    last = true;
                    break;
                }
                if (!state->queue.empty()) {
                    task = std::move(state->queue.front());
                    state->queue.pop_front();
                    break;
                }
                if (state->timers.empty()) {
                    state->wake.wait(lock);
                    continue;
                }
                const auto next = state->timers.begin();
                if (next->first <= std::chrono::steady_clock::now()) {
                    task = std::move(next->second);
                    state->timers.erase(next);
                    break;
                }
                state->wake.wait_until(lock, next->first);
            }
        }
        if (task) RunTask(task);
        task = nullptr;  // release captured objects (COM pointers included) before leaving the MTA
    }

    if (SUCCEEDED(com)) ::CoUninitialize();
    // Thread ids are recycled: forget ours so a future thread is never mistaken for this worker.
    state->threadId.store(0);
}

}  // namespace aether::status::audio
