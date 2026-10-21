// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// IRunningApplicationTracker built on WinEvent hooks.
//
//  * Start() enumerates the existing top-level windows (EnumWindows), then installs out-of-context
//    WinEvent hooks (show, hide, destroy, name change, foreground, minimize start/end, cloak/uncloak;
//    our own process is skipped). Events only mark the state dirty; a ~100 ms UI-thread timer
//    coalesces bursts into one re-enumeration, and the listener runs only when the result changed.
//  * Eligibility: see WindowEligibility.h.
//  * Identity of a window (pure rule: WindowIdentityRules.h), in order: its own AppUserModelID
//    (SHGetPropertyStoreForWindow, see WindowProperties.h), the packaged AUMID of its process
//    (GetApplicationUserModelId), the process image path (QueryFullProcessImageNameW). Windows of the
//    system's explorer.exe without an AppUserModelID take the File Explorer identity
//    (KnownApplications.h), so all File Explorer windows form one group. ApplicationFrameHost windows (class
//    ApplicationFrameWindow) are identified through the process of their hosted
//    Windows.UI.Core.CoreWindow; the identity seen while it was attached is remembered because Windows
//    detaches the CoreWindow while the app is minimized or suspended. Processes that cannot be opened
//    even with PROCESS_QUERY_LIMITED_INFORMATION (rare: some protected or system processes) are still
//    listed, with id "window:<class>" and the window title as name.
//  * Display names are resolved off the UI thread (Apps-folder name for AUMIDs, FileDescription for
//    executables) on a small STA worker; until then the executable's file name (or, for AUMID-only
//    identities, the window title) is used, and the listener runs again once the name is known.
//    A group whose windows declare a relaunch display name (browsers set one per profile) uses it
//    instead, so profile groups of one executable get distinct names.
//  * Relaunch presentation: each group carries the relaunch icon resource and display name of its
//    most recent window that declares them (RunningApplication). They are never logged.
//  * Optional registered Shell notifications latch HSHELL_FLASH requests; foreground/removal clears
//    them. Normal title changes/REDRAW never create attention. No window is raised by the tracker.
//    RegisterShellHookWindow is a documented Shell-specific compatibility API that Microsoft may
//    change/remove; if registration fails, attention remains unavailable and WinEvent tracking works.
//
// Threading: Start, Stop, Snapshot and the destructor must be called on the UI thread, which must
// pump messages (hooks and the timer are delivered through its message loop) and should be a COM
// STA: measured on Windows 11 build 26300, SHGetPropertyStoreForWindow on a thread without COM
// returns a store from which no AppUserModelID can be read, so pinned PWAs and apps with explicit
// per-window AUMIDs would be grouped by process instead (Start logs a warning in that case). The
// listener runs on the UI thread.
#pragma once

#include "applications/ApplicationTypes.h"
#include "applications/WindowIdentityRules.h"
#include "applications/WindowProperties.h"
#include "applications/WindowAttention.h"
#include "core/Dispatcher.h"
#include "core/WorkerPool.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aether::apps {

class WindowTracker final : public IRunningApplicationTracker {
public:
    struct Options {
        std::chrono::milliseconds coalesceDelay{100};
    };

    explicit WindowTracker(IUiDispatcher& dispatcher);
    WindowTracker(IUiDispatcher& dispatcher, Options options);
    ~WindowTracker() override;
    WindowTracker(const WindowTracker&) = delete;
    WindowTracker& operator=(const WindowTracker&) = delete;

    // InvalidState when called off the UI thread or while already started. The first notification
    // (the initial window list) is delivered asynchronously from the message loop.
    Result<void> Start(Listener listener) override;
    // Unhooks, kills the timer, drops pending name resolutions and forgets the window list (Snapshot()
    // is empty until the tracker is started again: a stopped tracker no longer knows what is running).
    // Idempotent.
    void Stop() noexcept override;
    // The latest window list (initially available right after Start() returns); empty while stopped.
    [[nodiscard]] std::vector<RunningApplication> Snapshot() const override;

private:
    struct ProcessIdentity {
        bool opened = false;
        FILETIME creationTime{};
        std::wstring appUserModelId;  // packaged processes only
        std::wstring executablePath;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                      DWORD eventThread, DWORD eventTime);

    void OnWinEvent(DWORD event, HWND hwnd);
    void OnShellEvent(WPARAM code, HWND hwnd);
    void MarkDirty();
    void Refresh();
    WindowIdentity Identify(HWND hwnd, const std::wstring& className, WindowShellProperties& properties);
    const ProcessIdentity& LookupProcess(DWORD processId);
    std::wstring DisplayNameFor(const WindowIdentity& identity, const std::wstring& firstTitle,
                                const std::wstring& relaunchDisplayName);
    void RequestDisplayName(const WindowIdentity& identity);
    void ReleaseResources() noexcept;

    IUiDispatcher& dispatcher_;
    Options options_;
    std::wstring windowsDirectory_;  // system Windows directory, for File Explorer windows
    Listener listener_;
    bool running_ = false;
    bool timerArmed_ = false;
    bool notifyPending_ = false;  // the next refresh notifies even without a change (first snapshot)
    HWND messageWindow_ = nullptr;
    UINT shellHookMessage_ = 0;
    bool shellHookRegistered_ = false;
    WindowAttentionState attention_;
    std::vector<HWINEVENTHOOK> hooks_;
    std::shared_ptr<bool> alive_;  // false once stopped; checked by posted name-resolution results
    std::unique_ptr<WorkerPool> namePool_;

    mutable std::mutex snapshotMutex_;
    std::vector<RunningApplication> snapshot_;

    std::unordered_set<HWND> trackedWindows_;
    std::unordered_map<DWORD, ProcessIdentity> processes_;
    std::unordered_map<HWND, WindowIdentity> frameIdentities_;  // ApplicationFrameWindow -> hosted app
    std::unordered_map<std::wstring, std::wstring> displayNames_;  // id -> resolved name ("" = failed)
    std::unordered_set<std::wstring> pendingNames_;
    std::unordered_map<std::wstring, std::uint64_t> firstSeen_;  // id -> order of appearance
    std::uint64_t nextOrder_ = 1;
};

}  // namespace aether::apps
