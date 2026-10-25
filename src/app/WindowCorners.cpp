// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/WindowCorners.h"

#include "core/Cancellation.h"
#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/Handles.h"
#include "platform/windows/WindowUtilities.h"
#include "settings/ui/MaximizedCornerPrototype.h"

#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cwctype>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace aether::app {
namespace {
constexpr wchar_t kLeaseProperty[] = L"AetherShell.WindowCornerGuard.Lease";
constexpr UINT_PTR kRefreshTimer = 71;
constexpr std::size_t kMaxLeases = 128;
Error NativeError(const char* text) { return MakeError(ErrorCode::Platform, text, ::GetLastError()); }

Result<CornerRegion> SerializeRegion(HRGN region) {
    const DWORD size = ::GetRegionData(region, 0, nullptr);
    if (!size || size > 1024 * 1024) return NativeError("Window corner region cannot be read");
    CornerRegion result(size);
    if (::GetRegionData(region, size, reinterpret_cast<RGNDATA*>(result.data())) != size)
        return NativeError("Window corner region changed while reading");
    return result;
}
platform::UniqueRegion DeserializeRegion(const CornerRegion& bytes) {
    if (bytes.size() < sizeof(RGNDATAHEADER) || bytes.size() > 1024 * 1024) return {};
    RGNDATAHEADER header{}; std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.dwSize != sizeof(header) || header.iType != RDH_RECTANGLES ||
        header.nCount > (bytes.size() - sizeof(header)) / sizeof(RECT)) return {};
    return platform::UniqueRegion(::ExtCreateRegion(nullptr, static_cast<DWORD>(bytes.size()),
        reinterpret_cast<const RGNDATA*>(bytes.data())));
}
bool SimpleRegionContainsClip(const CornerRegion& bytes, const CornerWindowObservation& observation) {
    auto region = DeserializeRegion(bytes); if (!region) return false;
    RECT bounds{};
    if (::GetRgnBox(region.Get(), &bounds) != SIMPLEREGION) return false;
    auto layout = settings::ui::experimental::ComputeMaximizedCornerLayout(
        observation.bounds, observation.frame, observation.work, observation.dpi);
    if (!layout) return false;
    const auto& clip = layout->clipPx;
    return bounds.left <= clip.left && bounds.top <= clip.top &&
        bounds.right >= clip.right && bounds.bottom >= clip.bottom;
}
Result<CornerRegion> RoundedRegion(const CornerWindowObservation& observation,
    const std::optional<CornerRegion>& original) {
    const auto geometry = settings::ui::experimental::ComputeMaximizedCornerLayout(
        observation.bounds, observation.frame, observation.work, observation.dpi);
    if (!geometry) return geometry.error();
    const auto& rect = geometry->clipPx;
    platform::UniqueRegion region(::CreateRoundRectRgn(rect.left, rect.top, rect.right, rect.bottom,
        geometry->radiusPx * 2, geometry->radiusPx * 2));
    if (!region) return NativeError("Window corner rounded region allocation failed");
    if (original) {
        auto prior = DeserializeRegion(*original);
        if (!prior || ::CombineRgn(region.Get(), region.Get(), prior.Get(), RGN_AND) == ERROR)
            return NativeError("Window corner prior rectangular region cannot be intersected");
    }
    return SerializeRegion(region.Get());
}
bool ExcludedClass(HWND window) {
    const auto name = platform::WindowClassName(window);
    return name.empty() || name.starts_with(L"Aether") || name.starts_with(L"Shell_") ||
        name.starts_with(L"Windows.UI.") || name.starts_with(L"Xaml") || name == L"Progman" ||
        name == L"WorkerW" || name == L"#32770" || name == L"TaskManagerWindow";
}
bool ExcludedProcess(HANDLE process) {
    std::array<wchar_t, 32768> image{}; DWORD length = static_cast<DWORD>(image.size());
    if (!::QueryFullProcessImageNameW(process, 0, image.data(), &length)) return true;
    auto name = std::filesystem::path(std::wstring_view(image.data(), length)).filename().native();
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    // Shell/security/infrastructure are excluded even when a window happens to look standard.
    constexpr std::array excluded{L"explorer.exe", L"sihost.exe", L"dwm.exe", L"taskmgr.exe",
        L"logonui.exe", L"winlogon.exe", L"consent.exe", L"credentialuibroker.exe", L"securityhealthsystray.exe",
        L"sechealthui.exe", L"msmpeng.exe", L"searchhost.exe", L"startmenuexperiencehost.exe",
        L"shellexperiencehost.exe", L"applicationframehost.exe", L"aethershell.exe", L"aethershellapp.exe"};
    return std::find(excluded.begin(), excluded.end(), name) != excluded.end();
}
Result<std::vector<std::byte>> TokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS kind) {
    DWORD size = 0; (void)::GetTokenInformation(token, kind, nullptr, 0, &size);
    if (!size || size > 65536) return NativeError("Window corner process token unavailable");
    std::vector<std::byte> bytes(size);
    if (!::GetTokenInformation(token, kind, bytes.data(), size, &size))
        return NativeError("Window corner process token unavailable");
    return bytes;
}
bool SafeUserProcess(DWORD process, bool testOnly) {
    platform::UniqueHandle handle(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process));
    if (!handle || (!testOnly && ExcludedProcess(handle.Get()))) return false;
    DWORD session = 0, ownSession = 0;
    if (!::ProcessIdToSessionId(process, &session) || !::ProcessIdToSessionId(::GetCurrentProcessId(), &ownSession) ||
        session != ownSession) return false;
    PROCESS_PROTECTION_LEVEL_INFORMATION protection{};
    if (!::GetProcessInformation(handle.Get(), ProcessProtectionLevelInfo, &protection, sizeof(protection)) ||
        protection.ProtectionLevel != PROTECTION_LEVEL_NONE) return false;
    platform::UniqueHandle token, ownToken;
    if (!::OpenProcessToken(handle.Get(), TOKEN_QUERY, token.Put()) ||
        !::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, ownToken.Put())) return false;
    TOKEN_ELEVATION elevation{}; DWORD size = 0;
    if (!::GetTokenInformation(token.Get(), TokenElevation, &elevation, sizeof(elevation), &size) ||
        elevation.TokenIsElevated) return false;
    auto user = TokenInformation(token.Get(), TokenUser);
    auto ownUser = TokenInformation(ownToken.Get(), TokenUser);
    auto integrity = TokenInformation(token.Get(), TokenIntegrityLevel);
    if (!user || !ownUser || !integrity) return false;
    const auto sid = reinterpret_cast<const TOKEN_USER*>(user->data())->User.Sid;
    const auto ownSid = reinterpret_cast<const TOKEN_USER*>(ownUser->data())->User.Sid;
    const auto level = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrity->data())->Label.Sid;
    if (!::IsValidSid(sid) || !::IsValidSid(ownSid) || !::EqualSid(sid, ownSid) || !::IsValidSid(level)) return false;
    const auto count = *::GetSidSubAuthorityCount(level);
    return count && *::GetSidSubAuthority(level, count - 1) <= SECURITY_MANDATORY_MEDIUM_RID;
}

class NativeBackend final : public IWindowCornerBackend {
public:
    explicit NativeBackend(DWORD excluded, const WindowCornerGuardOptions& options)
        : excluded_(excluded), options_(options) {}
    Result<CornerWindowObservation> Observe(HWND window) override {
        CornerWindowObservation result;
        result.thread = ::GetWindowThreadProcessId(window, &result.process);
        if (!result.thread) return MakeError(ErrorCode::NotFound, "Window corner target no longer exists");
        if (options_.onlyProcess && (result.process != options_.onlyProcess ||
            std::find(options_.onlyWindows.begin(), options_.onlyWindows.end(), window) == options_.onlyWindows.end()))
            return result;
        if ((!options_.onlyProcess && (result.process == excluded_ || result.process == ::GetCurrentProcessId())) ||
            !::IsWindowVisible(window) || ::IsIconic(window) || !::IsZoomed(window) ||
            ::GetAncestor(window, GA_ROOT) != window || ::GetWindow(window, GW_OWNER) ||
            (!options_.onlyProcess && ExcludedClass(window))) return result;
        const auto style = ::GetWindowLongPtrW(window, GWL_STYLE);
        const auto extended = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
        const auto excludedExtended = WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_LAYOUTRTL |
            (options_.onlyProcess ? 0 : WS_EX_NOACTIVATE);
        if ((style & WS_CAPTION) != WS_CAPTION || !(style & WS_THICKFRAME) || (style & WS_CHILD) ||
            (extended & excludedExtended) ||
            platform::IsWindowCloaked(window) || !SafeUserProcess(result.process, options_.onlyProcess != 0) ||
            ::IsHungAppWindow(window)) return result;
        // WM_NULL checks responsiveness without invoking an application command. SetWindowRgn
        // itself is synchronous and can still stall if the target hangs after this check.
        DWORD_PTR ignored = 0;
        if (!::SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &ignored)) return result;
        if (!::GetWindowRect(window, &result.bounds) || FAILED(::DwmGetWindowAttribute(window,
            DWMWA_EXTENDED_FRAME_BOUNDS, &result.frame, sizeof(result.frame)))) return result;
        MONITORINFO monitor{sizeof(monitor)};
        if (!::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return result;
        result.work = monitor.rcWork;
        // Full-monitor/borderless presentations are not altered, even if an app retains MAXIMIZE.
        if (result.frame.left <= monitor.rcMonitor.left && result.frame.top <= monitor.rcMonitor.top &&
            result.frame.right >= monitor.rcMonitor.right && result.frame.bottom >= monitor.rcMonitor.bottom) return result;
        result.dpi = ::GetDpiForWindow(window);
        result.eligible = true;
        return result;
    }
    Result<std::optional<CornerRegion>> ReadRegion(HWND window) override {
        platform::UniqueRegion region(::CreateRectRgn(0, 0, 0, 0));
        if (!region) return NativeError("Window corner region allocation failed");
        if (::GetWindowRgn(window, region.Get()) == ERROR) return std::optional<CornerRegion>{};
        auto data = SerializeRegion(region.Get()); if (!data) return data.error();
        return std::optional<CornerRegion>{std::move(*data)};
    }
    Result<void> WriteRegion(HWND window, const std::optional<CornerRegion>& region) override {
        platform::UniqueRegion native;
        if (region) {
            native = DeserializeRegion(*region);
            if (!native) return NativeError("Window corner region reconstruction failed");
        }
        if (!::SetWindowRgn(window, native.Get(), TRUE)) return NativeError("Window corner region update failed");
        (void)native.Release(); return Ok();
    }
    Result<std::optional<DWORD>> ReadBackdrop(HWND window) override {
        DWORD value = 0;
        const auto hr = ::DwmGetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &value, sizeof(value));
        // SYSTEMBACKDROP_TYPE is documented only on Windows 11 build 22621 and later.
        if (hr == E_INVALIDARG || hr == E_NOTIMPL) return std::optional<DWORD>{};
        if (FAILED(hr)) return MakeError(ErrorCode::Platform, "Window corner backdrop cannot be read", hr);
        if (value > DWMSBT_TABBEDWINDOW)
            return MakeError(ErrorCode::Unsupported, "Window corner backdrop type is not supported");
        return std::optional<DWORD>{value};
    }
    Result<void> WriteBackdrop(HWND window, DWORD value) override {
        const auto hr = ::DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &value, sizeof(value));
        if (FAILED(hr)) return MakeError(ErrorCode::Platform, "Window corner backdrop update failed", hr);
        return Ok();
    }
    bool Owns(HWND window, std::uintptr_t token) noexcept override {
        return ::GetPropW(window, kLeaseProperty) == reinterpret_cast<HANDLE>(token);
    }
    Result<void> Claim(HWND window, std::uintptr_t token) override {
        if (::GetPropW(window, kLeaseProperty)) return MakeError(ErrorCode::AlreadyExists, "Window corner lease already exists");
        if (!::SetPropW(window, kLeaseProperty, reinterpret_cast<HANDLE>(token)))
            return NativeError("Window corner lifetime marker could not be installed");
        return Ok();
    }
    void Release(HWND window, std::uintptr_t token) noexcept override {
        if (Owns(window, token)) (void)::RemovePropW(window, kLeaseProperty);
    }
private:
    DWORD excluded_;
    const WindowCornerGuardOptions& options_;
};

struct Observer {
    NativeBackend backend;
    const WindowCornerGuardOptions& options;
    std::map<HWND, std::unique_ptr<WindowCornerLease>> leases;
    std::map<HWND, std::pair<DWORD, DWORD>> abandoned;
    std::uintptr_t nextToken = 1;
    UINT_PTR timer = 0;
    bool stopping = false;
    Observer(DWORD excluded, const WindowCornerGuardOptions& value) : backend(excluded, value), options(value) {}
    void Queue(UINT delay = 40) noexcept { if (!stopping && !timer) timer = ::SetTimer(nullptr, kRefreshTimer, delay, nullptr); }
    void Refresh() {
        if (timer) { ::KillTimer(nullptr, timer); timer = 0; }
        bool retry = false;
        for (auto it = leases.begin(); it != leases.end();) {
            auto refreshed = it->second->Refresh();
            if (refreshed && *refreshed) { ++it; continue; }
            if (!refreshed) {
                // A temporary observation/allocation/native-write failure must not discard the
                // only prior-region snapshot while this guard still owns a clipped window.
                if (it->second->HasOwnership()) { retry = true; ++it; continue; }
                DWORD process = 0; const auto thread = ::GetWindowThreadProcessId(it->first, &process);
                if (thread) abandoned[it->first] = {process, thread};
            }
            it = leases.erase(it);
        }
        auto visit = [&](HWND window) {
            if (leases.size() >= kMaxLeases || leases.contains(window)) return;
            if (const auto old = abandoned.find(window); old != abandoned.end()) {
                DWORD process = 0; const auto thread = ::GetWindowThreadProcessId(window, &process);
                if (old->second == std::pair{process, thread}) return;
                abandoned.erase(old);
            }
            auto lease = WindowCornerLease::Acquire(backend, window, ++nextToken);
            if (lease) {
                leases.emplace(window, std::move(*lease));
                // Also revisit a first application that retained a material snapshot after a
                // failed region write. Do not rely on that failed write raising a WinEvent.
                retry = true;
            } else if (lease.error().code == ErrorCode::InvalidState) {
                // A synchronous application replacement during first application wins too.
                // Its own WinEvents must not cause repeated acquisition/material writes.
                DWORD process = 0;
                const auto thread = ::GetWindowThreadProcessId(window, &process);
                if (thread) abandoned[window] = {process, thread};
            }
        };
        if (options.onlyProcess) { for (const auto window : options.onlyWindows) visit(window); }
        else {
            using Visitor = decltype(visit);
            (void)::EnumWindows([](HWND window, LPARAM context) noexcept -> BOOL {
                try { (*reinterpret_cast<Visitor*>(context))(window); return TRUE; }
                catch (...) { return FALSE; }
            }, reinterpret_cast<LPARAM>(&visit));
        }
        if (retry) Queue(250);
    }
    bool RestoreAll() {
        for (auto it = leases.begin(); it != leases.end();) {
            auto restored = it->second->Restore();
            if (restored || !it->second->HasOwnership()) it = leases.erase(it);
            else ++it;
        }
        return leases.empty();
    }
    ~Observer() {
        stopping = true;
        if (timer) ::KillTimer(nullptr, timer);
        // Covers every error/exception exit too. This process may outlive the UI parent while a
        // target is hung or native recovery is temporarily unavailable; never drop its snapshot.
        for (;;) {
            try { if (RestoreAll()) break; } catch (...) {}
            ::Sleep(250);
        }
    }
};
thread_local Observer* currentObserver = nullptr;
LRESULT CALLBACK BroadcastProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    if ((message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE || message == WM_DPICHANGED) && currentObserver)
        currentObserver->Queue();
    return ::DefWindowProcW(window, message, wParam, lParam);
}
void CALLBACK EventHook(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) noexcept {
    if (!currentObserver) return;
    if (event == EVENT_OBJECT_DESTROY && object == OBJID_WINDOW && child == CHILDID_SELF && !::IsWindow(window)) {
        currentObserver->abandoned.erase(window);
    }
    if (event < EVENT_OBJECT_CREATE || (object == OBJID_WINDOW && child == CHILDID_SELF)) currentObserver->Queue();
}

struct GuardSession {
    platform::UniqueHandle process;
    platform::UniqueHandle stop;
    ~GuardSession() { Stop(); }
    void Stop() noexcept {
        if (!process) return;
        (void)::SetEvent(stop.Get());
        // If a target became hung inside SetWindowRgn the helper is deliberately left alive to
        // restore when that application responds. Terminating it would abandon foreign regions.
        const auto waited = ::WaitForSingleObject(process.Get(), 5000);
        if (waited == WAIT_OBJECT_0) AETHER_LOG_INFO("app", "window corner guard stopped");
        else AETHER_LOG_WARN("app", "window corner guard is still restoring a nonresponsive application");
        process.Reset(); stop.Reset();
    }
};
std::wstring Quote(std::wstring_view value) {
    std::wstring result = L"\""; std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { result.append(slashes * 2 + 1, L'\\'); result += c; }
        else { result.append(slashes, L'\\'); result += c; }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\'); result += L'\"'; return result;
}
Result<std::unique_ptr<GuardSession>> StartGuard(const std::filesystem::path& executable,
    const std::atomic<bool>& cancelled) {
    if (!executable.is_absolute()) return MakeError(ErrorCode::InvalidArgument, "Window corner host path must be absolute");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    platform::UniqueHandle parent(::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, ::GetCurrentProcessId()));
    platform::UniqueHandle ready(::CreateEventW(&security, TRUE, FALSE, nullptr));
    auto result = std::make_unique<GuardSession>();
    result->stop.Reset(::CreateEventW(&security, TRUE, FALSE, nullptr));
    if (!parent || !ready || !result->stop) return NativeError("Window corner guard handles unavailable");
    SIZE_T bytes = 0; (void)::InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<std::byte> storage(bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) return NativeError("Window corner guard inheritance unavailable");
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Cleanup() { ::DeleteProcThreadAttributeList(value); } } cleanup{attributes};
    HANDLE inherited[]{parent.Get(), ready.Get(), result->stop.Get()};
    if (!::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr)) return NativeError("Window corner guard handle list unavailable");
    auto command = Quote(executable.native()) + L" --window-corner-guard";
    for (const auto handle : inherited) command += L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
    PROCESS_INFORMATION child{};
    if (!::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup.StartupInfo, &child))
        return NativeError("Window corner guard could not start");
    platform::UniqueHandle thread(child.hThread); result->process.Reset(child.hProcess);
    const HANDLE wait[]{ready.Get(), result->process.Get()};
    for (unsigned attempt = 0; attempt < 100 && !cancelled.load(); ++attempt) {
        const auto status = ::WaitForMultipleObjects(2, wait, FALSE, 50);
        if (status == WAIT_OBJECT_0) return result;
        if (status != WAIT_TIMEOUT) return MakeError(ErrorCode::Unavailable, "Window corner guard did not become ready");
    }
    return MakeError(cancelled.load() ? ErrorCode::Cancelled : ErrorCode::Timeout, "Window corner guard startup interrupted");
}
} // namespace

WindowCornerLease::WindowCornerLease(IWindowCornerBackend& backend, HWND window, std::uintptr_t token,
    DWORD process, DWORD thread) noexcept : backend_(backend), window_(window), token_(token), process_(process), thread_(thread) {}
WindowCornerLease::~WindowCornerLease() { try { (void)Restore(); } catch (...) {} }
Result<std::unique_ptr<WindowCornerLease>> WindowCornerLease::Acquire(IWindowCornerBackend& backend,
    HWND window, std::uintptr_t token) try {
    if (!window || !token) return MakeError(ErrorCode::InvalidArgument, "Window corner identity is invalid");
    auto observed = backend.Observe(window); if (!observed) return observed.error();
    if (!observed->eligible || !observed->process || !observed->thread)
        return MakeError(ErrorCode::Unsupported, "Window corner target is not eligible");
    auto original = backend.ReadRegion(window); if (!original) return original.error();
    if (*original && !SimpleRegionContainsClip(**original, *observed))
        return MakeError(ErrorCode::Unsupported, "Window owns a nonstandard custom region");
    auto lease = std::unique_ptr<WindowCornerLease>(new WindowCornerLease(backend, window, token,
        observed->process, observed->thread));
    lease->original_ = *original; lease->installed_ = *original;
    auto backdrop = backend.ReadBackdrop(window); if (!backdrop) return backdrop.error();
    lease->originalBackdrop_ = *backdrop;
    if (auto claim = backend.Claim(window, token); !claim) return claim.error();
    lease->claimed_ = true;
    auto applied = lease->Refresh();
    if (!applied) {
        // A material write may have succeeded before the region write failed. Keep that lease
        // in the observer so a failed rollback cannot discard its only recovery snapshot.
        if (lease->claimed_ && (lease->applied_ || lease->backdropApplied_)) return lease;
        return applied.error();
    }
    if (!*applied) return MakeError(ErrorCode::Unavailable, "Window corner target changed before application");
    return lease;
} catch (...) { return MakeError(ErrorCode::Internal, "Window corner lease allocation failed"); }
Result<CornerWindowObservation> WindowCornerLease::ObserveOwned() {
    if (!claimed_ || !backend_.Owns(window_, token_)) {
        claimed_ = false;
        return MakeError(ErrorCode::InvalidState, "Window corner lifetime is no longer owned");
    }
    auto observed = backend_.Observe(window_);
    if (!observed) {
        if (!backend_.Owns(window_, token_)) claimed_ = false;
        return observed.error();
    }
    if (observed->process != process_ || observed->thread != thread_) {
        claimed_ = false;
        return MakeError(ErrorCode::InvalidState, "Window corner process identity changed");
    }
    return observed;
}
Result<bool> WindowCornerLease::Refresh() try {
    auto observed = ObserveOwned(); if (!observed) return observed.error();
    auto current = backend_.ReadRegion(window_); if (!current) return current.error();
    if (*current != installed_) {
        // The app's region wins, but our separately leased material still needs restoration.
        auto restored = Restore(); if (!restored) return restored.error();
        return MakeError(ErrorCode::InvalidState, "Application replaced its window region");
    }
    if (!observed->eligible) { auto restored = Restore(); if (!restored) return restored.error(); return false; }
    if (originalBackdrop_) {
        auto backdrop = backend_.ReadBackdrop(window_); if (!backdrop) return backdrop.error();
        if (!*backdrop) return MakeError(ErrorCode::Unavailable, "Window corner backdrop temporarily unavailable");
        const DWORD expected = backdropApplied_ ? DWMSBT_NONE : *originalBackdrop_;
        if (**backdrop != expected) {
            auto restored = Restore(); if (!restored) return restored.error();
            return MakeError(ErrorCode::InvalidState, "Application replaced its system backdrop");
        }
        if (*originalBackdrop_ >= DWMSBT_MAINWINDOW && !backdropApplied_) {
            // DWM's system material fills the full rectangular bounds independently of the
            // window region. Lease NONE while clipped; do not disable non-client rendering.
            if (auto written = backend_.WriteBackdrop(window_, DWMSBT_NONE); !written) return written.error();
            backdropApplied_ = true;
            // The material change may recalculate the visible frame; use a fresh observation.
            observed = ObserveOwned(); if (!observed) return observed.error();
            if (!observed->eligible) { auto restored = Restore(); if (!restored) return restored.error(); return false; }
            backdrop = backend_.ReadBackdrop(window_); if (!backdrop) return backdrop.error();
            if (!*backdrop) return MakeError(ErrorCode::Unavailable, "Window corner backdrop temporarily unavailable");
            if (**backdrop != DWMSBT_NONE) {
                auto restored = Restore(); if (!restored) return restored.error();
                return MakeError(ErrorCode::InvalidState, "Application replaced its backdrop during update");
            }
            current = backend_.ReadRegion(window_); if (!current) return current.error();
            if (*current != installed_) {
                auto restored = Restore(); if (!restored) return restored.error();
                return MakeError(ErrorCode::InvalidState, "Application replaced its region during backdrop update");
            }
        }
    }
    auto rounded = RoundedRegion(*observed, original_); if (!rounded) return rounded.error();
    std::optional<CornerRegion> next{std::move(*rounded)};
    if (next == installed_) return true;
    if (auto written = backend_.WriteRegion(window_, next); !written) return written.error();
    installed_ = std::move(next); applied_ = true;
    if (!backend_.Owns(window_, token_)) {
        claimed_ = false;
        return MakeError(ErrorCode::InvalidState, "Window corner lifetime changed during application");
    }
    return true;
} catch (...) { return MakeError(ErrorCode::Internal, "Window corner lease refresh failed"); }
Result<void> WindowCornerLease::Restore() try {
    if (!claimed_) return Ok();
    // Before a successful clip there is no region mutation to undo. This also releases a claim
    // if allocation or the first native write failed during Acquire.
    if (!applied_ && !backdropApplied_) { backend_.Release(window_, token_); claimed_ = false; return Ok(); }
    auto observed = ObserveOwned();
    if (!observed) return observed.error();
    if (applied_) {
        auto current = backend_.ReadRegion(window_); if (!current) return current.error();
        if (*current == installed_) {
            if (auto written = backend_.WriteRegion(window_, original_); !written) return written;
            installed_ = original_;
        }
        applied_ = false;
    }
    if (backdropApplied_) {
        auto backdrop = backend_.ReadBackdrop(window_); if (!backdrop) return backdrop.error();
        if (!*backdrop) return MakeError(ErrorCode::Unavailable, "Window corner backdrop restoration unavailable");
        if (**backdrop == DWMSBT_NONE) {
            if (auto written = backend_.WriteBackdrop(window_, *originalBackdrop_); !written) return written;
        }
        backdropApplied_ = false;
    }
    backend_.Release(window_, token_); claimed_ = false; return Ok();
} catch (...) { return MakeError(ErrorCode::Internal, "Window corner lease restoration failed"); }

Result<int> RunWindowCornerGuard(HANDLE parent, HANDLE ready, HANDLE stop,
    const WindowCornerGuardOptions& options) try {
    DWORD flags = 0;
    if (!parent || !ready || !stop || !::GetHandleInformation(parent, &flags) ||
        !::GetHandleInformation(ready, &flags) || !::GetHandleInformation(stop, &flags) || !::GetProcessId(parent))
        return MakeError(ErrorCode::InvalidArgument, "Window corner guard handles are invalid");
    if ((options.onlyProcess == 0) != options.onlyWindows.empty())
        return MakeError(ErrorCode::InvalidArgument, "Window corner test restriction is incomplete");
    // An explicit whitelist is test-only and must belong entirely to this test process.
    if (options.onlyProcess) for (const auto window : options.onlyWindows) {
        DWORD process = 0; (void)::GetWindowThreadProcessId(window, &process);
        if (options.onlyProcess != ::GetCurrentProcessId() || process != options.onlyProcess)
            return MakeError(ErrorCode::AccessDenied, "Window corner test restriction must own every window");
    }
    platform::EnsurePerMonitorDpiAwareness();
    Observer observer(::GetProcessId(parent), options);
    if (currentObserver) return MakeError(ErrorCode::InvalidState, "Window corner observer already runs on this thread");
    currentObserver = &observer;
    struct Reset { ~Reset() { currentObserver = nullptr; } } reset;
    WNDCLASSEXW broadcastClass{sizeof(broadcastClass)};
    broadcastClass.hInstance = ::GetModuleHandleW(nullptr);
    broadcastClass.lpszClassName = L"AetherShell.WindowCornerGuard.Broadcast";
    broadcastClass.lpfnWndProc = BroadcastProc;
    if (auto registered = platform::RegisterWindowClassOnce(broadcastClass); !registered) return registered.error();
    const auto broadcastWindow = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        broadcastClass.lpszClassName, L"Aether window corner geometry observer", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, broadcastClass.hInstance, nullptr);
    if (!broadcastWindow) return NativeError("Window corner broadcast observer unavailable");
    struct Destroy { HWND value; ~Destroy() { ::DestroyWindow(value); } } destroy{broadcastWindow};
    std::array<HWINEVENTHOOK, 3> hooks{};
    struct Unhook { std::array<HWINEVENTHOOK, 3>& values; ~Unhook() { for (auto hook : values) if (hook) ::UnhookWinEvent(hook); } } cleanup{hooks};
    constexpr DWORD hookFlags = WINEVENT_OUTOFCONTEXT;
    hooks[0] = ::SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, EventHook, 0, 0, hookFlags);
    hooks[1] = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, EventHook, 0, 0, hookFlags);
    hooks[2] = ::SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, EventHook, 0, 0, hookFlags);
    if (std::any_of(hooks.begin(), hooks.end(), [](auto hook) { return !hook; }))
        return NativeError("Window corner event observation unavailable");
    if (::WaitForSingleObject(parent, 0) != WAIT_TIMEOUT || ::WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return 0;
    observer.Refresh();
    if (!::SetEvent(ready)) return NativeError("Window corner readiness notification failed");
    const HANDLE waiting[]{parent, stop};
    bool exit = false;
    while (!exit) {
        const auto status = ::MsgWaitForMultipleObjectsEx(2, waiting, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (status == WAIT_OBJECT_0 || status == WAIT_OBJECT_0 + 1) break;
        if (status != WAIT_OBJECT_0 + 2) return NativeError("Window corner event wait failed");
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { exit = true; break; }
            if (message.message == WM_TIMER && message.wParam == observer.timer) observer.Refresh();
            else { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        }
    }
    // No new acquisitions while stopping. Exit is the restoration acknowledgement, so retain
    // recoverable failures here rather than dropping the lease snapshot in a destructor.
    observer.stopping = true;
    if (observer.timer) { ::KillTimer(nullptr, observer.timer); observer.timer = 0; }
    for (auto& hook : hooks) if (hook) { ::UnhookWinEvent(hook); hook = nullptr; }
    while (!observer.RestoreAll()) {
        (void)::MsgWaitForMultipleObjectsEx(0, nullptr, 250, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message); ::DispatchMessageW(&message);
        }
    }
    return 0;
} catch (...) { return MakeError(ErrorCode::Internal, "Window corner guard stopped unexpectedly"); }

Result<std::optional<int>> DispatchWindowCornerGuard(const std::vector<std::wstring>& arguments) try {
    if (arguments.empty() || arguments.front() != L"--window-corner-guard") return std::optional<int>{};
    if (arguments.size() != 4) return MakeError(ErrorCode::InvalidArgument, "Window corner guard requires inherited handles");
    std::array<HANDLE, 3> handles{};
    for (std::size_t i = 0; i < handles.size(); ++i) {
        std::string digits;
        for (const auto c : arguments[i + 1]) {
            if (c < L'0' || c > L'9') return MakeError(ErrorCode::InvalidArgument, "Window corner guard handle is invalid");
            digits.push_back(static_cast<char>(c));
        }
        std::uintptr_t value = 0;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || !value)
            return MakeError(ErrorCode::InvalidArgument, "Window corner guard handle is invalid");
        handles[i] = reinterpret_cast<HANDLE>(value);
    }
    auto guarded = RunWindowCornerGuard(handles[0], handles[1], handles[2]);
    if (!guarded) return guarded.error();
    return std::optional<int>{*guarded};
} catch (...) { return MakeError(ErrorCode::Internal, "Window corner guard dispatch failed"); }

struct WindowCorners::Impl {
    bool allowed;
    std::filesystem::path executable;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> wanted{false};
    std::unique_ptr<GuardSession> session; // worker only
    WorkerPool worker{{1, L"Aether window corner recovery", ComApartment::None}};
    Impl(bool value, std::filesystem::path path) : allowed(value), executable(std::move(path)) {}
};
WindowCorners::WindowCorners(bool allowNativeAccess, std::filesystem::path hostExecutable)
    : impl_(std::make_unique<Impl>(allowNativeAccess, std::move(hostExecutable))) {}
WindowCorners::~WindowCorners() { Stop(); }
Result<void> WindowCorners::Request(bool enabled) noexcept try {
    if (impl_->cancelled.load()) return MakeError(ErrorCode::InvalidState, "Window corner controller has stopped");
    if (enabled && !impl_->allowed) return MakeError(ErrorCode::AccessDenied, "Window corners are disabled in isolated sessions");
    if (impl_->wanted.exchange(enabled) == enabled) return Ok();
    if (!impl_->worker.Post([state = impl_.get()] {
        try {
            if (state->cancelled.load() || !state->wanted.load()) { state->session.reset(); return; }
            if (state->session && ::WaitForSingleObject(state->session->process.Get(), 0) == WAIT_TIMEOUT) return;
            state->session.reset();
            auto created = StartGuard(state->executable, state->cancelled);
            if (created && !state->cancelled.load() && state->wanted.load()) {
                state->session = std::move(*created);
                AETHER_LOG_INFO("app", "window corner guard started");
            } else if (!created && !state->cancelled.load()) AETHER_LOG_WARN("app", "window corner guard unavailable");
        } catch (...) { AETHER_LOG_WARN("app", "window corner guard unavailable"); }
    })) {
        impl_->wanted.store(false);
        return MakeError(ErrorCode::Unavailable, "Window corner worker is stopping");
    }
    return Ok();
} catch (...) { return MakeError(ErrorCode::Internal, "Window corner request failed"); }
void WindowCorners::Stop() noexcept {
    if (!impl_ || impl_->cancelled.exchange(true)) return;
    impl_->wanted.store(false);
    impl_->worker.Shutdown();
    impl_->session.reset();
}

} // namespace aether::app
