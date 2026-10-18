// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/TaskbarReplacement.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "platform/windows/WindowUtilities.h"
#include "platform/windows/AppPaths.h"
#include "platform/windows/Win32Error.h"

#include <shellapi.h>
#include <algorithm>
#include <cstdint>

namespace aether::platform {
namespace {
class WindowsTaskbars final : public ITaskbarBackend {
public:
    static bool Native(HWND window) {
        DWORD shell = 0, candidate = 0;
        ::GetWindowThreadProcessId(::GetShellWindow(), &shell);
        ::GetWindowThreadProcessId(window, &candidate);
        if (!shell || candidate != shell) return false;
        const auto name = WindowClassName(window);
        return name == L"Shell_TrayWnd" || name == L"Shell_SecondaryTrayWnd";
    }
    Result<unsigned> State() override {
        const auto window = ::FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!Native(window)) return MakeError(ErrorCode::Unavailable, "Windows taskbar is unavailable");
        APPBARDATA data{sizeof(data)}; data.hWnd = window;
        return static_cast<unsigned>(::SHAppBarMessage(ABM_GETSTATE, &data));
    }
    Result<void> SetState(unsigned state) override {
        const auto window = ::FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!Native(window)) return MakeError(ErrorCode::Unavailable, "Windows taskbar is unavailable");
        APPBARDATA data{sizeof(data)}; data.hWnd = window; data.lParam = state;
        ::SHAppBarMessage(ABM_SETSTATE, &data);
        auto observed = State();
        if (!observed || (*observed & ABS_AUTOHIDE) != (state & ABS_AUTOHIDE))
            return MakeError(ErrorCode::Unavailable, "Windows refused the taskbar auto-hide state");
        return Ok();
    }
    Result<std::vector<TaskbarWindowState>> Windows() override {
        struct Enumeration { std::vector<TaskbarWindowState> windows; bool failed = false; } result;
        ::EnumWindows([](HWND window, LPARAM pointer) -> BOOL {
            auto& result = *reinterpret_cast<Enumeration*>(pointer);
            try { if (Native(window)) result.windows.push_back({window, ::IsWindowVisible(window) != FALSE}); return TRUE; }
            catch (...) { result.failed = true; return FALSE; }
        }, reinterpret_cast<LPARAM>(&result));
        if (result.failed) return MakeError(ErrorCode::Internal, "taskbar enumeration failed");
        return std::move(result.windows);
    }
    Result<void> SetVisible(HWND window, bool visible) override {
        if (!Native(window)) return Ok(); // Explorer may have restarted; never touch a recycled HWND.
        ::ShowWindow(window, visible ? SW_SHOWNA : SW_HIDE);
        if (Native(window) && (::IsWindowVisible(window) != FALSE) != visible)
            return MakeError(ErrorCode::Unavailable, "Windows refused taskbar visibility");
        return Ok();
    }
};
thread_local TaskbarVisibilityLease* watchedLease = nullptr;
thread_local bool enforceFailed = false;
void CALLBACK Shown(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) noexcept {
    if (event != EVENT_OBJECT_SHOW || object != OBJID_WINDOW || child != CHILDID_SELF || !watchedLease) return;
    // Enforce ignores every non-shell HWND. No foreign titles or content are inspected.
    try { if (WindowsTaskbars::Native(window) && !watchedLease->Enforce()) { enforceFailed = true; ::PostQuitMessage(1); } }
    catch (...) { enforceFailed = true; ::PostQuitMessage(1); }
}
}

Result<std::unique_ptr<ITaskbarBackend>> MakeWindowsTaskbarBackend() try {
    return std::unique_ptr<ITaskbarBackend>(std::make_unique<WindowsTaskbars>());
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar backend unavailable"); }

Result<TaskbarSnapshot> CaptureTaskbars(ITaskbarBackend& backend) try {
    auto state = backend.State(); if (!state) return state.error();
    auto windows = backend.Windows(); if (!windows) return windows.error();
    if (windows->empty()) return MakeError(ErrorCode::Unavailable, "no native taskbar was found");
    return TaskbarSnapshot{*state, std::move(*windows)};
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar snapshot failed"); }

Result<void> RestoreTaskbars(ITaskbarBackend& backend, const TaskbarSnapshot& snapshot) try {
    auto state = backend.SetState(snapshot.state);
    // A restored taskbar is a visible taskbar: every Explorer taskbar window is shown and none is
    // hidden. A saved "not visible" flag can be a transient Explorer re-layout state (observed on
    // the real desktop when a capture coincided with an AppBar change, 2026-10-18), and hiding a
    // native taskbar on restore could leave the user without one. Aether Shell only ever hides.
    std::vector<HWND> windows;
    for (const auto& window : snapshot.windows) windows.push_back(window.window);
    // A restarted Explorer may have different HWNDs; its new taskbar is shown as well.
    if (auto current = backend.Windows(); current) for (const auto& window : *current) {
        if (std::find(windows.begin(), windows.end(), window.window) == windows.end()) windows.push_back(window.window);
    }
    for (const auto window : windows) {
        auto shown = backend.SetVisible(window, true); if (!shown && state) state = shown;
    }
    return state;
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar restoration failed"); }

namespace {
constexpr std::uint64_t kMaxBaselineBytes = 64 * 1024;
constexpr std::size_t kMaxBaselineWindows = 64;

// Used when the original auto-hide state is unknown: only visibility is changed.
Result<void> ShowTaskbars(ITaskbarBackend& backend) {
    auto windows = backend.Windows(); if (!windows) return windows.error();
    if (windows->empty()) return MakeError(ErrorCode::Unavailable, "no native taskbar was found");
    Result<void> result = Ok();
    for (const auto& window : *windows) {
        auto shown = backend.SetVisible(window.window, true); if (!shown && result) result = shown;
    }
    return result;
}
}  // namespace

std::filesystem::path TaskbarBaselineFile(const std::filesystem::path& configDirectory) {
    return configDirectory / L"taskbar-baseline.json";
}

Result<void> SaveTaskbarBaseline(const std::filesystem::path& file, const TaskbarSnapshot& snapshot) try {
    if (file.empty()) return MakeError(ErrorCode::InvalidArgument, "taskbar baseline location is missing");
    if (snapshot.windows.size() > kMaxBaselineWindows) return MakeError(ErrorCode::InvalidArgument, "too many taskbar windows");
    auto root = json::Value::MakeObject();
    root.Set("schemaVersion", 1);
    root.Set("state", static_cast<std::int64_t>(snapshot.state));
    auto windows = json::Value::MakeArray();
    for (const auto& window : snapshot.windows) {
        auto item = json::Value::MakeObject();
        item.Set("handle", static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(window.window)));
        item.Set("visible", window.visible);
        windows.Append(std::move(item));
    }
    root.Set("windows", std::move(windows));
    if (auto directory = fs::EnsureDirectory(file.parent_path()); !directory) return directory;
    return fs::WriteFileAtomic(file, json::Serialize(root));
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar baseline could not be saved"); }

Result<std::optional<TaskbarSnapshot>> LoadTaskbarBaseline(const std::filesystem::path& file) try {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        if (error) return MakeError(ErrorCode::Io, "taskbar baseline is not accessible", error.value());
        return std::optional<TaskbarSnapshot>{};
    }
    auto text = fs::ReadFile(file, kMaxBaselineBytes); if (!text) return text.error();
    auto root = json::Parse(*text); if (!root) return root.error();
    const auto invalid = [] { return MakeError(ErrorCode::Validation, "taskbar baseline is malformed"); };
    if (!root->IsObject() || root->GetInt64("schemaVersion", 0) != 1) return invalid();
    const auto* state = root->Find("state");
    const auto* windows = root->Find("windows");
    const auto stateValue = state ? state->AsInt64() : std::nullopt;
    if (!stateValue || *stateValue < 0 || *stateValue > 0xFFFFFFFFll || !windows || !windows->AsArray() ||
        windows->AsArray()->size() > kMaxBaselineWindows) return invalid();
    TaskbarSnapshot snapshot; snapshot.state = static_cast<unsigned>(*stateValue);
    for (const auto& item : *windows->AsArray()) {
        const auto* handle = item.Find("handle");
        const auto* visible = item.Find("visible");
        const auto handleValue = handle ? handle->AsInt64() : std::nullopt;
        const auto visibleValue = visible ? visible->AsBool() : std::nullopt;
        if (!handleValue || *handleValue <= 0 || !visibleValue) return invalid();
        snapshot.windows.push_back({reinterpret_cast<HWND>(static_cast<std::uintptr_t>(*handleValue)), *visibleValue});
    }
    return std::optional<TaskbarSnapshot>(std::move(snapshot));
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar baseline could not be read"); }

Result<void> RemoveTaskbarBaseline(const std::filesystem::path& file) try {
    std::error_code error;
    std::filesystem::remove(file, error);
    if (error) return MakeError(ErrorCode::Io, "taskbar baseline could not be removed", error.value());
    return Ok();
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar baseline could not be removed"); }

Result<TaskbarRecovery> RecoverTaskbars(const std::filesystem::path& file, bool showWithoutBaseline,
                                        const TaskbarBackendFactory& makeBackend) try {
    if (!file.is_absolute()) return MakeError(ErrorCode::InvalidArgument, "taskbar baseline location is invalid");
    auto baseline = LoadTaskbarBaseline(file);
    const bool saved = baseline && baseline->has_value();
    const bool unreadable = !baseline;
    if (!saved && !unreadable && !showWithoutBaseline) return TaskbarRecovery::NothingToRestore;
    if (!makeBackend) return MakeError(ErrorCode::InvalidArgument, "taskbar backend is missing");
    auto backend = makeBackend(); if (!backend) return backend.error();
    if (!*backend) return MakeError(ErrorCode::Unavailable, "taskbar backend unavailable");
    auto restored = saved ? RestoreTaskbars(**backend, **baseline) : ShowTaskbars(**backend);
    if (!restored) return restored.error();
    // The taskbar is back. A file that cannot be removed would only re-apply this same baseline.
    if (saved || unreadable) (void)RemoveTaskbarBaseline(file);
    return saved ? TaskbarRecovery::RestoredFromBaseline : TaskbarRecovery::ShownWithoutBaseline;
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar recovery failed"); }

Result<TaskbarSnapshot> PrepareTaskbarBaseline(ITaskbarBackend& backend, const std::filesystem::path& file) try {
    if (!file.is_absolute()) return MakeError(ErrorCode::InvalidArgument, "taskbar baseline location is invalid");
    auto previous = LoadTaskbarBaseline(file);
    if (previous && *previous) {
        auto restored = RestoreTaskbars(backend, **previous); if (!restored) return restored.error();
    } else if (!previous) {
        auto shown = ShowTaskbars(backend); if (!shown) return shown.error();
    }
    auto snapshot = CaptureTaskbars(backend); if (!snapshot) return snapshot.error();
    if (auto stored = SaveTaskbarBaseline(file, *snapshot); !stored) return stored.error();
    return snapshot;
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar baseline preparation failed"); }

TaskbarVisibilityLease::~TaskbarVisibilityLease() { (void)End(); }
Result<void> TaskbarVisibilityLease::Begin() try {
    if (active_) return MakeError(ErrorCode::InvalidState, "taskbar lease already active");
    auto snapshot = CaptureTaskbars(backend_); if (!snapshot) return snapshot.error();
    original_ = std::move(*snapshot); lastApplied_ = original_.state; active_ = true;
    auto result = Enforce(); if (!result) (void)End(); return result;
} catch (...) { (void)End(); return MakeError(ErrorCode::Internal, "taskbar replacement failed"); }

Result<void> TaskbarVisibilityLease::Enforce() try {
    if (!active_) return MakeError(ErrorCode::InvalidState, "taskbar lease is inactive");
    auto state = backend_.State(); if (!state) return state.error();
    // Preserve an explicit native Settings change as the eventual restoration baseline.
    if (*state != lastApplied_) original_.state = *state;
    auto windows = backend_.Windows(); if (!windows) return windows.error();
    for (const auto& window : *windows) if (std::none_of(original_.windows.begin(), original_.windows.end(),
        [&](const auto& old) { return old.window == window.window; })) original_.windows.push_back(window);
    const unsigned desired = *state | ABS_AUTOHIDE;
    if (desired != *state) { auto changed = backend_.SetState(desired); if (!changed) return changed; }
    lastApplied_ = desired;
    for (const auto& window : *windows) if (window.visible) {
        auto hidden = backend_.SetVisible(window.window, false); if (!hidden) return hidden;
    }
    return Ok();
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar replacement refresh failed"); }

Result<void> TaskbarVisibilityLease::End() try {
    if (!active_) return Ok();
    if (auto state = backend_.State(); state && *state != lastApplied_) original_.state = *state;
    auto restored = RestoreTaskbars(backend_, original_); if (restored) active_ = false; return restored;
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar restoration failed"); }

Result<int> RunTaskbarGuard(HANDLE rawParent, HANDLE rawReady, HANDLE rawStop, HANDLE rawRestored) try {
    UniqueHandle parent(rawParent), ready(rawReady), stop(rawStop), restoredEvent(rawRestored);
    if (!::SetEvent(restoredEvent.Get())) return LastWin32Error("taskbar restoration acknowledgement unavailable");
    const DWORD parentId = ::GetProcessId(parent.Get());
    if (!parentId || parentId == ::GetCurrentProcessId()) return MakeError(ErrorCode::InvalidArgument, "invalid taskbar guard parent");
    DWORD parentSession = 0, ownSession = 0;
    if (!::ProcessIdToSessionId(parentId, &parentSession) || !::ProcessIdToSessionId(::GetCurrentProcessId(), &ownSession) || parentSession != ownSession)
        return MakeError(ErrorCode::AccessDenied, "taskbar guard session differs");
    wchar_t image[32768]{}; DWORD length = 32768;
    if (!::QueryFullProcessImageNameW(parent.Get(), 0, image, &length)) return LastWin32Error("taskbar guard parent unavailable");
    const std::filesystem::path parentImage(std::wstring(image, length));
    const auto own = CurrentExecutablePath();
    std::error_code error;
    if (parentImage.filename() != L"AetherShellApp.exe" || !std::filesystem::equivalent(parentImage.parent_path(), own.parent_path(), error) || error)
        return MakeError(ErrorCode::AccessDenied, "taskbar guard parent image differs");
    if (::WaitForSingleObject(parent.Get(), 0) != WAIT_TIMEOUT || ::WaitForSingleObject(stop.Get(), 0) != WAIT_TIMEOUT) return 0;
    auto backend = MakeWindowsTaskbarBackend(); if (!backend) return backend.error();
    TaskbarVisibilityLease lease(**backend);
    HWINEVENTHOOK hook = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, Shown, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!hook) return LastWin32Error("taskbar observation unavailable");
    struct Unhook { HWINEVENTHOOK hook; ~Unhook() { watchedLease = nullptr; ::UnhookWinEvent(hook); } } unhook{hook};
    struct RestoreAcknowledgement {
        TaskbarVisibilityLease& lease; HANDLE event;
        ~RestoreAcknowledgement() { watchedLease = nullptr; if (lease.End()) ::SetEvent(event); }
    } acknowledgement{lease, restoredEvent.Get()};
    enforceFailed = false;
    if (!::ResetEvent(restoredEvent.Get())) return LastWin32Error("taskbar restoration acknowledgement unavailable");
    if (auto started = lease.Begin(); !started) return started.error();
    watchedLease = &lease;
    if (!::SetEvent(ready.Get())) { (void)lease.End(); return LastWin32Error("taskbar guard readiness failed"); }
    const HANDLE handles[]{parent.Get(), stop.Get()};
    bool done = false;
    while (!done) {
        const auto result = ::MsgWaitForMultipleObjects(2, handles, FALSE, INFINITE, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + 1 || result == WAIT_FAILED) break;
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { done = true; break; }
            ::TranslateMessage(&message); ::DispatchMessageW(&message);
        }
    }
    watchedLease = nullptr; ::UnhookWinEvent(unhook.hook); unhook.hook = nullptr;
    auto restored = lease.End(); if (!restored) return restored.error();
    if (enforceFailed) return MakeError(ErrorCode::Unavailable, "taskbar ownership was interrupted");
    return 0;
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar guard failed"); }

}  // namespace aether::platform
