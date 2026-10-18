// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/ProviderThread.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"

#include <exception>
#include <atomic>
#include <system_error>

namespace aether::status {

Result<std::shared_ptr<WakeSignal>> WakeSignal::Create() {
    auto signal = std::make_shared<WakeSignal>();
    signal->event_.Reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!signal->event_) return platform::LastWin32Error("CreateEvent(status wake signal)");
    return signal;
}

struct ProviderThread::State {
    platform::UniqueHandle stopEvent;
    std::atomic<DWORD> threadId{0};
    std::atomic<bool> finished{false};
    bool started = false;  // Start is serialized by the host
};

ProviderThread::ProviderThread() : state_(std::make_shared<State>()) {}

ProviderThread::~ProviderThread() {
    Stop();
    // A self-destroyed session has no external owner left to join. The closure owns the shared
    // state/stop handle and has already finished its body before it destroys this thread wrapper.
    if (thread_.joinable()) thread_.detach();
}

Result<void> ProviderThread::Start(std::wstring name, Body body, std::function<void()> completed) {
    std::lock_guard joinLock(joinMutex_);
    if (state_->started) return MakeError(ErrorCode::InvalidState, "provider thread was already started");
    state_->stopEvent.Reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!state_->stopEvent) return platform::LastWin32Error("CreateEvent(status stop event)");
    try {
        thread_ = std::thread([state = state_, name = std::move(name), body = std::move(body),
                               completed = std::move(completed)]() mutable {
            state->threadId.store(::GetCurrentThreadId());
            ::SetThreadDescription(::GetCurrentThread(), name.c_str());
            try {
                body(state->stopEvent.Get());
            } catch (const std::exception&) {
                AETHER_LOG_ERROR("status", "status provider thread failed");
            } catch (...) {
                AETHER_LOG_ERROR("status", "status provider thread failed with a non-standard exception");
            }
            body = nullptr;  // release retained providers/COM state before announcing completion
            state->finished.store(true);
            try {
                if (completed) completed();
            } catch (...) {
                AETHER_LOG_ERROR("status", "status worker completion failed");
            }
            completed = nullptr;
            state->threadId.store(0);  // Windows can reuse a thread id
        });
        state_->started = true;
    } catch (const std::system_error& ex) {
        state_->stopEvent.Reset();
        state_->finished.store(true);
        return MakeError(ErrorCode::Platform, "cannot start provider thread", ex.code().value());
    } catch (...) {
        state_->stopEvent.Reset();
        state_->finished.store(true);
        return MakeError(ErrorCode::Internal, "cannot create status provider thread");
    }
    return Ok();
}

void ProviderThread::Stop() noexcept {
    RequestStop();
    if (IsCurrentThread()) return;
    std::lock_guard joinLock(joinMutex_);
    if (thread_.joinable()) thread_.join();
}

void ProviderThread::RequestStop() noexcept { if (state_->stopEvent) ::SetEvent(state_->stopEvent.Get()); }

bool ProviderThread::IsCurrentThread() const noexcept { return state_->threadId.load() == ::GetCurrentThreadId(); }

bool ProviderThread::IsFinished() const noexcept { return state_->finished.load(); }

DWORD WaitForAny(std::initializer_list<HANDLE> handles, DWORD timeoutMs) noexcept {
    const DWORD count = static_cast<DWORD>(handles.size());
    const DWORD result = ::WaitForMultipleObjects(count, handles.begin(), FALSE, timeoutMs);
    if (result < WAIT_OBJECT_0 + count) return result - WAIT_OBJECT_0;
    if (result == WAIT_TIMEOUT) return WAIT_TIMEOUT;
    return WAIT_FAILED;
}

}  // namespace aether::status
