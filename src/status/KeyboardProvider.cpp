// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/KeyboardProvider.h"

#include "diagnostics/Logger.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/Handles.h"
#include "platform/windows/Win32Error.h"
#include "status/KeyboardLayouts.h"
#include "status/ProviderHost.h"
#include "status/SnapshotUtilities.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace aether::status {
namespace {

// Thread messages understood by the provider loop (posted to the thread, hwnd == nullptr).
constexpr UINT kRefreshMessage = WM_APP + 1;     // Refresh(): drop cached names and re-read
constexpr UINT kForegroundMessage = WM_APP + 2;  // EVENT_SYSTEM_FOREGROUND was received

// True while a kForegroundMessage is queued, so a burst of focus changes costs one re-read. Out-of-context
// WinEvent callbacks run on the thread that installed the hook, so a thread-local needs no locking and keeps
// several providers (each on its own thread) independent.
thread_local bool t_foregroundPending = false;

void CALLBACK OnForegroundChanged(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
    // Runs inside GetMessage on the provider thread. Posting instead of reading here keeps the listener out
    // of a hook callback and coalesces bursts.
    if (event != EVENT_SYSTEM_FOREGROUND || t_foregroundPending) return;
    t_foregroundPending = ::PostThreadMessageW(::GetCurrentThreadId(), kForegroundMessage, 0, 0) != FALSE;
}

struct WinEventHookTraits {
    using Type = HWINEVENTHOOK;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type hook) noexcept { ::UnhookWinEvent(hook); }
};
using UniqueWinEventHook = platform::UniqueResource<WinEventHookTraits>;

// Turns a kernel event into a thread message. The loop must block in GetMessage: out-of-context WinEvent
// callbacks are delivered by GetMessage, while MsgWaitForMultipleObjectsEx(QS_ALLINPUT) does not return for
// them and a PeekMessage poll did not deliver them either (measured on Windows 11 build 26300). GetMessage
// cannot wait on handles, so thread-pool waits forward the stop and wake events as messages.
class EventToMessage {
public:
    EventToMessage(HANDLE event, DWORD threadId, UINT message, bool once) noexcept
        : threadId_(threadId), message_(message) {
        const ULONG flags = WT_EXECUTEINWAITTHREAD | (once ? WT_EXECUTEONLYONCE : 0);
        if (!::RegisterWaitForSingleObject(&wait_, event, &OnSignalled, this, INFINITE, flags)) wait_ = nullptr;
    }
    ~EventToMessage() {
        // INVALID_HANDLE_VALUE: also waits for a callback in progress, so `this` outlives every callback.
        if (wait_ != nullptr) ::UnregisterWaitEx(wait_, INVALID_HANDLE_VALUE);
    }
    EventToMessage(const EventToMessage&) = delete;
    EventToMessage& operator=(const EventToMessage&) = delete;

    [[nodiscard]] bool IsValid() const noexcept { return wait_ != nullptr; }

private:
    static void CALLBACK OnSignalled(PVOID context, BOOLEAN /*timedOut*/) {
        const auto* self = static_cast<const EventToMessage*>(context);
        ::PostThreadMessageW(self->threadId_, self->message_, 0, 0);
    }

    DWORD threadId_;
    UINT message_;
    HANDLE wait_ = nullptr;
};

KeyboardSnapshot ErrorSnapshot(std::string detail) {
    KeyboardSnapshot snapshot;
    snapshot.state = ProviderState::Error;
    snapshot.detail = std::move(detail);
    return snapshot;
}

}  // namespace

InputLocaleReading ReadForegroundInputLocale() noexcept {
    InputLocaleReading reading;
    if (const HWND foreground = ::GetForegroundWindow()) {
        if (const DWORD threadId = ::GetWindowThreadProcessId(foreground, nullptr); threadId != 0) {
            // The thread can exit between the two calls; GetKeyboardLayout then returns null.
            if (const HKL layout = ::GetKeyboardLayout(threadId)) {
                reading.hkl = reinterpret_cast<std::uintptr_t>(layout);
                reading.fromForegroundWindow = true;
                return reading;
            }
        }
    }
    // No foreground window (secure desktop, focus in transition): use the calling thread's own layout.
    reading.hkl = reinterpret_cast<std::uintptr_t>(::GetKeyboardLayout(0));
    return reading;
}

struct KeyboardProvider::Impl {
    explicit Impl(KeyboardProviderOptions o) : options(std::move(o)) {}

    void Run(HANDLE stopEvent, HANDLE wakeEvent);
    void Check(bool force);
    [[nodiscard]] InputLocaleReading Read() const {
        return options.readInputLocale ? options.readInputLocale() : ReadForegroundInputLocale();
    }

    const KeyboardProviderOptions options;

    // Provider thread only.
    std::unordered_map<std::uintptr_t, KeyboardSnapshot> descriptions;
    std::optional<InputLocaleReading> last;

    ProviderHost<KeyboardSnapshot> host;  // last: joined first
};

void KeyboardProvider::Impl::Check(bool force) {
    const InputLocaleReading reading = Read();
    if (!force && last && *last == reading) return;  // the cheap path taken by every timer tick
    last = reading;

    if (reading.hkl == 0) {
        host.Publish(ErrorSnapshot("GetKeyboardLayout returned no input locale"), &SameContent);
        return;
    }

    auto cached = descriptions.find(reading.hkl);
    if (cached == descriptions.end()) {
        cached = descriptions.emplace(reading.hkl, DescribeKeyboardLayout(reading.hkl)).first;
        if (cached->second.state != ProviderState::Available) {
            AETHER_LOG_WARN("status.keyboard", "{}", cached->second.detail);
        }
    }
    KeyboardSnapshot snapshot = cached->second;
    snapshot.fromForegroundWindow = reading.fromForegroundWindow;
    if (host.Publish(snapshot, &SameContent)) AETHER_LOG_DEBUG("status.keyboard", "{}", Describe(snapshot));
}

void KeyboardProvider::Impl::Run(HANDLE stopEvent, HANDLE wakeEvent) {
    // SHLoadIndirectString (layout names) may need COM for some resource forms.
    platform::ComApartment apartment(COINIT_MULTITHREADED);
    descriptions.clear();
    last.reset();
    t_foregroundPending = false;

    // Create the message queue before anything can post to it.
    MSG message{};
    ::PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    const DWORD threadId = ::GetCurrentThreadId();

    // WM_QUIT ends GetMessage. If the stop event is already set, the wait fires immediately.
    const EventToMessage stopBridge(stopEvent, threadId, WM_QUIT, true);
    if (!stopBridge.IsValid()) {
        const auto error = platform::LastWin32Error("RegisterWaitForSingleObject(stop)");
        AETHER_LOG_ERROR("status.keyboard", "{}", error.Describe());
        host.Publish(ErrorSnapshot("cannot start the input language monitor: " + error.Describe()));
        ::WaitForSingleObject(stopEvent, INFINITE);
        return;
    }
    const EventToMessage wakeBridge(wakeEvent, threadId, kRefreshMessage, false);
    if (!wakeBridge.IsValid()) {
        AETHER_LOG_WARN("status.keyboard", "Refresh() unavailable: {}",
                        platform::LastWin32Error("RegisterWaitForSingleObject(wake)").Describe());
    }

    const UniqueWinEventHook hook(::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                                    &OnForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT));
    if (!hook) {
        AETHER_LOG_WARN("status.keyboard", "SetWinEventHook(EVENT_SYSTEM_FOREGROUND) failed; relying on the timer");
    }

    // Thread timer (WM_TIMER with hwnd == nullptr); coalescable because a late comparison is harmless.
    const auto periodMs = static_cast<UINT>(std::clamp<std::int64_t>(options.checkInterval.count(), 50, 60'000));
    const UINT_PTR timer = ::SetCoalescableTimer(nullptr, 0, periodMs, nullptr, periodMs / 4);
    if (timer == 0) {
        AETHER_LOG_WARN("status.keyboard", "periodic comparison unavailable: {}",
                        platform::LastWin32Error("SetCoalescableTimer").Describe());
    }

    Check(true);

    for (;;) {
        const BOOL got = ::GetMessageW(&message, nullptr, 0, 0);
        if (got == 0) break;  // WM_QUIT from the stop bridge
        if (got == -1) {
            const auto error = platform::LastWin32Error("GetMessage");
            AETHER_LOG_ERROR("status.keyboard", "{}", error.Describe());
            host.Publish(ErrorSnapshot("input language monitor failed: " + error.Describe()), &SameContent);
            ::WaitForSingleObject(stopEvent, INFINITE);
            break;
        }
        if (message.hwnd == nullptr) {
            if (message.message == WM_TIMER && timer != 0 && message.wParam == timer) {
                // Belt and braces: if the WM_QUIT could not be posted, the next tick still ends the loop.
                if (::WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) break;
                Check(false);
                continue;
            }
            if (message.message == kForegroundMessage) {
                t_foregroundPending = false;
                Check(false);
                continue;
            }
            if (message.message == kRefreshMessage) {
                descriptions.clear();  // names may have changed (display language, installed layouts)
                Check(true);
                continue;
            }
        }
        ::DispatchMessageW(&message);
    }
    if (timer != 0) ::KillTimer(nullptr, timer);
    // The hook and the bridges are released here, on this thread, in reverse order of creation.
}

KeyboardProvider::KeyboardProvider(KeyboardProviderOptions options) : impl_(std::make_shared<Impl>(std::move(options))) {}

KeyboardProvider::~KeyboardProvider() { Stop(); }

Result<void> KeyboardProvider::Start(Listener listener) {
    const auto impl = impl_;
    return impl->host.Start(std::move(listener), L"Aether status: keyboard",
                            [impl](HANDLE stop, const std::shared_ptr<WakeSignal>& wake) {
                                impl->Run(stop, wake->Handle());
                            });
}

void KeyboardProvider::Stop() noexcept { impl_->host.Stop(); }

KeyboardSnapshot KeyboardProvider::Current() const { return impl_->host.Current(); }

void KeyboardProvider::Refresh() noexcept { impl_->host.Wake(); }

}  // namespace aether::status
