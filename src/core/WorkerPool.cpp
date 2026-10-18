// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/WorkerPool.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aether {

struct WorkerPool::Impl {
    Options options;
    std::mutex mutex;
    std::deque<std::function<void()>> queue;
    bool stopping = false;
    HANDLE wakeEvent = nullptr;  // manual reset; signalled while the queue is non-empty or stopping
    std::vector<std::thread> threads;
    std::vector<std::thread::id> threadIds;

    std::function<void()> TryPop(bool& stop) {
        std::lock_guard lock(mutex);
        if (stopping) {
            stop = true;
            return {};
        }
        if (queue.empty()) {
            ::ResetEvent(wakeEvent);
            return {};
        }
        auto task = std::move(queue.front());
        queue.pop_front();
        if (queue.empty()) ::ResetEvent(wakeEvent);
        return task;
    }

    void ThreadMain(std::size_t index) {
        std::wstring threadName = options.name;
        if (options.threadCount > 1) threadName += L" #" + std::to_wstring(index + 1);
        ::SetThreadDescription(::GetCurrentThread(), threadName.c_str());

        HRESULT comResult = S_FALSE;
        bool comInitialized = false;
        if (options.apartment != ComApartment::None) {
            const DWORD flags = options.apartment == ComApartment::MultiThreaded ? COINIT_MULTITHREADED
                                                                                   : COINIT_APARTMENTTHREADED;
            comResult = ::CoInitializeEx(nullptr, flags | COINIT_DISABLE_OLE1DDE);
            comInitialized = SUCCEEDED(comResult);
        }
        const bool pumpMessages = options.apartment == ComApartment::SingleThreaded;

        for (;;) {
            bool stop = false;
            auto task = TryPop(stop);
            if (stop) break;
            if (task) {
                try {
                    task();
                } catch (const std::exception&) {
                    ::OutputDebugStringA("Aether worker task threw an exception\n");
                } catch (...) {
                    ::OutputDebugStringA("Aether worker task threw a non-standard exception\n");
                }
                continue;
            }
            if (pumpMessages) {
                const DWORD result =
                    ::MsgWaitForMultipleObjectsEx(1, &wakeEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (result == WAIT_OBJECT_0 + 1) {
                    MSG msg;
                    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                        ::TranslateMessage(&msg);
                        ::DispatchMessageW(&msg);
                    }
                }
            } else {
                ::WaitForSingleObject(wakeEvent, INFINITE);
            }
        }

        if (comInitialized) ::CoUninitialize();
    }
};

WorkerPool::WorkerPool(Options options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
    impl_->options.threadCount = std::max<std::size_t>(1, impl_->options.threadCount);
    impl_->wakeEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!impl_->wakeEvent) std::terminate();
    impl_->threads.reserve(impl_->options.threadCount);
    for (std::size_t i = 0; i < impl_->options.threadCount; ++i) {
        impl_->threads.emplace_back([impl = impl_.get(), i] { impl->ThreadMain(i); });
        impl_->threadIds.push_back(impl_->threads.back().get_id());
    }
}

WorkerPool::~WorkerPool() {
    Shutdown();
    if (impl_->wakeEvent) ::CloseHandle(impl_->wakeEvent);
}

bool WorkerPool::Post(std::function<void()> task) {
    if (!task) return false;
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping) return false;
    impl_->queue.push_back(std::move(task));
    ::SetEvent(impl_->wakeEvent);
    return true;
}

void WorkerPool::Shutdown() noexcept {
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopping && impl_->threads.empty()) return;
        impl_->stopping = true;
        impl_->queue.clear();
        ::SetEvent(impl_->wakeEvent);
    }
    for (auto& thread : impl_->threads) {
        if (thread.joinable()) {
            if (thread.get_id() == std::this_thread::get_id()) {
                thread.detach();  // Defensive: never self-join.
            } else {
                thread.join();
            }
        }
    }
    impl_->threads.clear();
}

bool WorkerPool::IsShuttingDown() const noexcept {
    std::lock_guard lock(impl_->mutex);
    return impl_->stopping;
}

bool WorkerPool::IsWorkerThread() const noexcept {
    const auto self = std::this_thread::get_id();
    return std::find(impl_->threadIds.begin(), impl_->threadIds.end(), self) != impl_->threadIds.end();
}

}  // namespace aether
