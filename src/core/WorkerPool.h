// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Fixed-size pool of background threads. Used for every blocking operation
// (network, application discovery, icon extraction, update downloads) so the
// UI thread never blocks.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace aether {

enum class ComApartment {
    None,            // the thread does not initialize COM
    MultiThreaded,   // CoInitializeEx(COINIT_MULTITHREADED)
    SingleThreaded,  // CoInitializeEx(COINIT_APARTMENTTHREADED); the thread pumps messages while idle
};

class WorkerPool {
public:
    struct Options {
        std::size_t threadCount = 1;
        std::wstring name = L"Aether worker";
        ComApartment apartment = ComApartment::None;
    };

    explicit WorkerPool(Options options);
    ~WorkerPool();  // Calls Shutdown().

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    // Queues a task. Returns false (and drops the task) once shutdown has begun.
    // Exceptions escaping a task are caught and logged through OutputDebugString.
    bool Post(std::function<void()> task);

    // Stops accepting work, discards tasks that have not started, and joins all threads after the
    // running tasks return. Long tasks must observe a CancellationToken to keep shutdown prompt.
    // Must not be called from one of the pool's own threads.
    void Shutdown() noexcept;

    [[nodiscard]] bool IsShuttingDown() const noexcept;
    [[nodiscard]] bool IsWorkerThread() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether
