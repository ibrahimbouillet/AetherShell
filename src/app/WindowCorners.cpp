// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/WindowCorners.h"

#include "core/Cancellation.h"
#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/Handles.h"
#include "platform/windows/MonitorUtilities.h"
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
#include <set>
#include <string>
#include <thread>

namespace aether::app {
namespace {
constexpr wchar_t kLeaseProperty[] = L"AetherShell.WindowCornerGuard.Lease";
constexpr wchar_t kLeaseOwnerProperty[] = L"AetherShell.WindowCornerLeaseOwner";
constexpr UINT_PTR kRefreshTimer = 71;
constexpr std::size_t kMaxLeases = 128;
// An application can re-assert its material without any window event, so leased windows are also
// looked at on this slow schedule (D46). Other windows cost nothing between events (AS4-07).
constexpr ULONGLONG kLeaseCheckMs = 2000;
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
    // Inset by the rim (D45): the ring window beneath fills the band between this clip and the
    // outer rounded edge, with the same rasterized geometry, so there is no gap or overlap.
    const int width = CornerRimWidthPx(observation.dpi);
    const int radius = std::max(0, geometry->radiusPx - width);
    if (rect.right - rect.left <= 2 * width || rect.bottom - rect.top <= 2 * width)
        return MakeError(ErrorCode::Unsupported, "Window corner target is too small for a rim");
    platform::UniqueRegion region(::CreateRoundRectRgn(rect.left + width, rect.top + width, rect.right - width,
        rect.bottom - width, radius * 2, radius * 2));
    if (!region) return NativeError("Window corner rounded region allocation failed");
    if (original) {
        auto prior = DeserializeRegion(*original);
        if (!prior || ::CombineRgn(region.Get(), region.Get(), prior.Get(), RGN_AND) == ERROR)
            return NativeError("Window corner prior rectangular region cannot be intersected");
    }
    return SerializeRegion(region.Get());
}
std::optional<CornerRim> RimFor(const CornerWindowObservation& observation) {
    const auto geometry = settings::ui::experimental::ComputeMaximizedCornerLayout(
        observation.bounds, observation.frame, observation.work, observation.dpi);
    if (!geometry) return std::nullopt;
    CornerRim rim;
    rim.bounds = geometry->clipPx;
    ::OffsetRect(&rim.bounds, observation.bounds.left, observation.bounds.top);
    rim.radiusPx = geometry->radiusPx;
    rim.widthPx = CornerRimWidthPx(observation.dpi);
    return rim;
}

// --- rim windows (D45) ---------------------------------------------------------------------------
// One per clipped window, owned by this guard: a click-through layered popup directly beneath its
// target, shaped to the ring between the target's inset clip and the outer rounded edge and filled
// with one neutral color. It never covers the target, takes input or activation, or has a taskbar entry.
constexpr wchar_t kRimClass[] = L"AetherShell.WindowCornerGuard.Rim";

// A restrained neutral perimeter for the system theme: lighter than dark content, darker than light
// content. High contrast uses the system's window frame color.
COLORREF RimColor() noexcept {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON))
        return ::GetSysColor(COLOR_WINDOWFRAME);
    DWORD light = 1, size = sizeof(light);
    (void)::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                         L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light ? RGB(0xC6, 0xC6, 0xC8) : RGB(0x4D, 0x4D, 0x4F);
}

LRESULT CALLBACK RimProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    switch (message) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: {
            RECT bounds{};
            ::GetClientRect(window, &bounds);
            if (HBRUSH brush = ::CreateSolidBrush(static_cast<COLORREF>(::GetWindowLongPtrW(window, GWLP_USERDATA)))) {
                ::FillRect(reinterpret_cast<HDC>(wParam), &bounds, brush);
                ::DeleteObject(brush);
            }
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            ::BeginPaint(window, &paint);
            ::EndPaint(window, &paint);
            return 0;
        }
        default: return ::DefWindowProcW(window, message, wParam, lParam);
    }
}

class RimWindow {
public:
    RimWindow() = default;
    RimWindow(const RimWindow&) = delete;
    RimWindow& operator=(const RimWindow&) = delete;
    ~RimWindow() { if (window_) ::DestroyWindow(window_); }

    // Places the ring for `rim` directly beneath `target`; false if the ring cannot be shown.
    bool Sync(HWND target, const CornerRim& rim, COLORREF color) {
        if (!window_ && !Create()) return false;
        const int width = rim.bounds.right - rim.bounds.left, height = rim.bounds.bottom - rim.bounds.top;
        if (!shape_ || !(*shape_ == rim)) {
            const int inner = std::max(0, rim.radiusPx - rim.widthPx);
            platform::UniqueRegion outer(::CreateRoundRectRgn(0, 0, width, height, rim.radiusPx * 2, rim.radiusPx * 2));
            platform::UniqueRegion hole(::CreateRoundRectRgn(rim.widthPx, rim.widthPx, width - rim.widthPx, height - rim.widthPx,
                                                             inner * 2, inner * 2));
            if (!outer || !hole || ::CombineRgn(outer.Get(), outer.Get(), hole.Get(), RGN_DIFF) == ERROR) return false;
            if (!::SetWindowRgn(window_, outer.Get(), TRUE)) return false;
            (void)outer.Release();  // owned by the window now
            shape_ = rim;
        }
        if (color != color_) {
            color_ = color;
            (void)::SetWindowLongPtrW(window_, GWLP_USERDATA, static_cast<LONG_PTR>(color));
            ::InvalidateRect(window_, nullptr, TRUE);
        }
        RECT placed{};
        const bool moved = !::GetWindowRect(window_, &placed) || !::EqualRect(&placed, &rim.bounds);
        if (moved || !::IsWindowVisible(window_) || ::GetWindow(target, GW_HWNDNEXT) != window_) {
            if (!::SetWindowPos(window_, target, rim.bounds.left, rim.bounds.top, width, height,
                                SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW))
                return false;
        }
        return true;
    }

private:
    bool Create() {
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = kRimClass;
        type.lpfnWndProc = RimProc;
        if (!platform::RegisterWindowClassOnce(type)) return false;
        window_ = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kRimClass, L"",
                                    WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, nullptr);
        if (!window_) return false;
        (void)::SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
        return true;
    }
    HWND window_ = nullptr;
    std::optional<CornerRim> shape_;
    COLORREF color_ = CLR_INVALID;
};

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

// A region write runs on its own short-lived thread: SetWindowRgn sends the target synchronous
// position messages, so a target that stops answering inside the write must cost the guard at most
// kRegionWriteWaitMs, never every other lease (AS4-03). The record outlives a pending write.
constexpr DWORD kRegionWriteWaitMs = 250;
struct RegionWrite {
    platform::UniqueHandle done{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> finished{false};
    BOOL result = FALSE;
    DWORD error = 0;
};
void Count(std::atomic<std::uint64_t>* counter) noexcept { if (counter) counter->fetch_add(1, std::memory_order_relaxed); }

class NativeBackend final : public IWindowCornerBackend {
public:
    explicit NativeBackend(DWORD excluded, const WindowCornerGuardOptions& options)
        : excluded_(excluded), options_(options) {}
    // AS4-07: a process's identity and token do not change during its life, so one validation per
    // process instance is enough. The creation time guards against a reused process id.
    bool SafeProcess(DWORD process, bool testOnly) {
        platform::UniqueHandle handle(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process));
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!handle || !::GetProcessTimes(handle.Get(), &created, &exited, &kernel, &user)) return false;
        const auto stamp = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        if (const auto known = processes_.find(process); known != processes_.end() && known->second.created == stamp) return known->second.safe;
        const bool safe = SafeUserProcess(process, testOnly);
        if (processes_.size() >= 512) processes_.clear();
        processes_[process] = KnownProcess{stamp, safe};
        return safe;
    }
    bool Responsive(HWND window) noexcept override {
        if (const auto pending = writes_.find(window); pending != writes_.end()) {
            if (!pending->second->finished.load(std::memory_order_acquire)) return false;
            writes_.erase(pending);
        }
        if (::IsHungAppWindow(window)) return false;
        // WM_NULL checks responsiveness without invoking an application command.
        DWORD_PTR ignored = 0;
        return ::SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &ignored) != 0;
    }
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
        // D45: a layered window that is also captioned, resizable and genuinely maximized is an ordinary
        // application window (Chromium/Electron use WS_EX_LAYERED for transparency); click-through
        // overlays stay excluded through WS_EX_TRANSPARENT and the caption/frame requirement.
        const auto excludedExtended = WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_LAYOUTRTL |
            (options_.onlyProcess ? 0 : WS_EX_NOACTIVATE);
        // Responsiveness is not eligibility (AS4-03): a busy page stays clipped; only mutations wait.
        if ((style & WS_CAPTION) != WS_CAPTION || !(style & WS_THICKFRAME) || (style & WS_CHILD) ||
            (extended & excludedExtended) ||
            platform::IsWindowCloaked(window) || !SafeProcess(result.process, options_.onlyProcess != 0)) return result;
        if (!::GetWindowRect(window, &result.bounds) || FAILED(::DwmGetWindowAttribute(window,
            DWMWA_EXTENDED_FRAME_BOUNDS, &result.frame, sizeof(result.frame)))) return result;
        MONITORINFO monitor{sizeof(monitor)};
        if (!::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return result;
        result.work = monitor.rcWork;
        // D56, the owner's rule: rounding belongs to a maximized window sitting between the bars. One that
        // covers the whole monitor (both bars auto-hidden) simply takes the screen, as does full screen.
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
    Result<void> WriteRegion(HWND window, const std::optional<CornerRegion>& region) override try {
        std::erase_if(writes_, [](const auto& entry) { return entry.second->finished.load(std::memory_order_acquire); });
        if (writes_.contains(window)) return MakeError(ErrorCode::Timeout, "Window corner region update is still pending");
        platform::UniqueRegion native;
        if (region) {
            native = DeserializeRegion(*region);
            if (!native) return NativeError("Window corner region reconstruction failed");
        }
        auto write = std::make_shared<RegionWrite>();
        if (!write->done) return NativeError("Window corner region update event unavailable");
        HRGN handle = native.Get();
        std::thread([write, window, handle] {
            const BOOL set = ::SetWindowRgn(window, handle, TRUE);
            write->error = set ? 0 : ::GetLastError();
            if (!set && handle) ::DeleteObject(handle);  // the system owns only an installed region
            write->result = set;
            write->finished.store(true, std::memory_order_release);
            (void)::SetEvent(write->done.Get());
        }).detach();
        (void)native.Release();  // now the writer thread's
        if (::WaitForSingleObject(write->done.Get(), kRegionWriteWaitMs) == WAIT_OBJECT_0) {
            if (!write->result) return MakeError(ErrorCode::Platform, "Window corner region update failed", write->error);
            Count(options_.statistics ? &options_.statistics->regionWrites : nullptr);
            return Ok();
        }
        writes_[window] = std::move(write);
        Count(options_.statistics ? &options_.statistics->pendingWrites : nullptr);
        return MakeError(ErrorCode::Timeout, "Window corner region update is still pending");
    } catch (...) { return MakeError(ErrorCode::Internal, "Window corner region writer unavailable"); }
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
    bool ClearAbandonedClaim(HWND window) noexcept override {
        if (!::GetPropW(window, kLeaseProperty)) return false;
        const auto owner = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(::GetPropW(window, kLeaseOwnerProperty)));
        if (!owner || owner == ::GetCurrentProcessId()) return false;
        // Only a guard that is really gone: a process of that id that still runs keeps its claim.
        if (const HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, owner)) {
            const bool running = ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            ::CloseHandle(process);
            if (running) return false;
        } else if (::GetLastError() != ERROR_INVALID_PARAMETER) {
            return false;
        }
        (void)::RemovePropW(window, kLeaseProperty);
        (void)::RemovePropW(window, kLeaseOwnerProperty);
        return true;
    }
    Result<void> Claim(HWND window, std::uintptr_t token) override {
        if (::GetPropW(window, kLeaseProperty)) return MakeError(ErrorCode::AlreadyExists, "Window corner lease already exists");
        (void)::SetPropW(window, kLeaseOwnerProperty, reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(::GetCurrentProcessId())));
        if (!::SetPropW(window, kLeaseProperty, reinterpret_cast<HANDLE>(token)))
            return NativeError("Window corner lifetime marker could not be installed");
        return Ok();
    }
    void Release(HWND window, std::uintptr_t token) noexcept override {
        if (Owns(window, token)) { (void)::RemovePropW(window, kLeaseProperty); (void)::RemovePropW(window, kLeaseOwnerProperty); }
    }
private:
    DWORD excluded_;
    const WindowCornerGuardOptions& options_;
    std::map<HWND, std::shared_ptr<RegionWrite>> writes_;
    struct KnownProcess { std::uint64_t created = 0; bool safe = false; };
    std::map<DWORD, KnownProcess> processes_;
};

struct Observer {
    NativeBackend backend;
    const WindowCornerGuardOptions& options;
    std::map<HWND, std::unique_ptr<WindowCornerLease>> leases;
    // A window given back to its application: its identity, and when a re-asserted material may be
    // clipped again (0: not before the window is maximized anew).
    struct Abandoned { DWORD process = 0; DWORD thread = 0; ULONGLONG retryAt = 0; };
    std::map<HWND, Abandoned> abandoned;
    struct Retries { int attempts = 0; ULONGLONG last = 0; };
    std::map<HWND, Retries> materialRetries;  // re-assertions of a maximized window (D46, D56)
    std::map<HWND, std::unique_ptr<RimWindow>> rims;  // D45: one perimeter ring per clipped window
    // AS4-03: a target that did not answer waits out a growing delay while the others proceed.
    struct Deferral { ULONGLONG until = 0; int attempts = 0; };
    std::map<HWND, Deferral> deferred;
    // AS4-07: events mark the windows they concern; only display/settings broadcasts (and the
    // first pass) enumerate every top-level window.
    std::set<HWND> dirty;
    bool fullSweep = true;
    bool allLeases = false;           // activation can change every leased window's stacking and material
    ULONGLONG nextLeaseCheck = 0;
    std::uintptr_t nextToken = 1;
    UINT_PTR timer = 0;
    ULONGLONG timerDue = 0;
    bool stopping = false;
    Observer(DWORD excluded, const WindowCornerGuardOptions& value) : backend(excluded, value), options(value) {}
    std::atomic<std::uint64_t>* Statistic(std::atomic<std::uint64_t> WindowCornerGuardStatistics::* field) const noexcept {
        return options.statistics ? &(options.statistics->*field) : nullptr;
    }
    // One pending refresh; an earlier request replaces a later one.
    void Queue(UINT delay = 40) noexcept {
        if (stopping) return;
        const ULONGLONG due = ::GetTickCount64() + delay;
        if (timer && due >= timerDue) return;
        if (timer) ::KillTimer(nullptr, timer);
        timer = ::SetTimer(nullptr, kRefreshTimer, delay, nullptr);
        timerDue = due;
    }
    void MarkDirty(HWND window, UINT delay = 40) noexcept {
        try { dirty.insert(window); } catch (...) { fullSweep = true; }
        Queue(delay);
    }
    void MarkAll() noexcept { fullSweep = true; Queue(); }
    static UINT DeferralDelay(int attempts) noexcept { return static_cast<UINT>(std::min(4000, 250 << std::min(attempts, 4))); }
    // Acquires a lease for a new candidate, or honours the application's own choice (D46).
    void Visit(HWND window, std::set<HWND>& again) {
        const bool presented = ::IsZoomed(window) && !::IsIconic(window) && ::IsWindowVisible(window);
        if (!presented) materialRetries.erase(window);
        if (leases.size() >= kMaxLeases || leases.contains(window)) return;
        if (const auto old = abandoned.find(window); old != abandoned.end()) {
            DWORD process = 0; const auto thread = ::GetWindowThreadProcessId(window, &process);
            if (old->second.process == process && old->second.thread == thread) {
                // The application's own region/material wins for as long as this maximized presentation
                // lasts. Once the window leaves it (restored or minimized), its next maximize is a new
                // presentation and gets one fresh attempt. D46: a merely re-asserted previous material
                // is clipped again once its bounded delay has passed; never immediately.
                if (!presented) { abandoned.erase(old); return; }
                if (!old->second.retryAt) return;
                const ULONGLONG now = ::GetTickCount64();
                if (now < old->second.retryAt) { MarkDirty(window, static_cast<UINT>(old->second.retryAt - now)); return; }
            }
            abandoned.erase(old);
        }
        if (!presented) return;
        auto lease = WindowCornerLease::Acquire(backend, window, ++nextToken);
        if (lease) {
            leases.emplace(window, std::move(*lease));
            // Also revisit a first application that retained a material snapshot after a
            // failed region write. Do not rely on that failed write raising a WinEvent.
            again.insert(window);
        } else if (lease.error().code == ErrorCode::InvalidState) {
            // A synchronous application replacement during first application wins too.
            // Its own WinEvents must not cause repeated acquisition/material writes.
            DWORD process = 0;
            const auto thread = ::GetWindowThreadProcessId(window, &process);
            if (thread) abandoned[window] = Abandoned{process, thread, 0};
        } else if (lease.error().code == ErrorCode::Timeout) {
            again.insert(window);  // the target did not answer before its first clip; try again later
        }
    }
    void Refresh() {
        if (timer) { ::KillTimer(nullptr, timer); timer = 0; }
        LARGE_INTEGER started{}, finished{}, frequency{};
        (void)::QueryPerformanceCounter(&started);
        Count(Statistic(&WindowCornerGuardStatistics::sweeps));
        const bool full = fullSweep;
        fullSweep = false;
        std::set<HWND> targets;
        targets.swap(dirty);
        std::set<HWND> again;
        const ULONGLONG now = ::GetTickCount64();
        const bool everyLease = full || allLeases || now >= nextLeaseCheck;
        allLeases = false;
        if (everyLease) nextLeaseCheck = now + kLeaseCheckMs;
        for (auto it = leases.begin(); it != leases.end();) {
            const auto window = it->first;
            if (!everyLease && !targets.contains(window)) { ++it; continue; }
            if (const auto wait = deferred.find(window); wait != deferred.end() && now < wait->second.until) {
                MarkDirty(window, static_cast<UINT>(wait->second.until - now));
                ++it; continue;
            }
            Count(Statistic(&WindowCornerGuardStatistics::leaseRefreshes));
            auto refreshed = it->second->Refresh();
            if (refreshed && *refreshed) { deferred.erase(window); ++it; continue; }
            if (!refreshed) {
                // A temporary observation/allocation/native-write failure must not discard the
                // only prior-region snapshot while this guard still owns a clipped window.
                if (it->second->HasOwnership()) {
                    if (refreshed.error().code == ErrorCode::Timeout) {
                        auto& wait = deferred[window];
                        const UINT delay = DeferralDelay(wait.attempts++);
                        wait.until = ::GetTickCount64() + delay;
                        Count(Statistic(&WindowCornerGuardStatistics::deferrals));
                        MarkDirty(window, delay);
                    } else again.insert(window);
                    ++it; continue;
                }
                DWORD process = 0; const auto thread = ::GetWindowThreadProcessId(window, &process);
                if (thread) {
                    Abandoned entry{process, thread, 0};
                    auto& retry = materialRetries[window];
                    const ULONGLONG at = ::GetTickCount64();  // from now: the restoration itself takes time
                    // D56: the budget limits a burst, not a whole session. A browser re-asserts on every
                    // activation; left at one budget per maximize it ends up square for good.
                    if (at - retry.last >= kCornerRetryRestMs) retry.attempts = 0;
                    retry.last = at;
                    const auto replacement = it->second->Replacement();
                    if (const auto delay = CornerRetryDelay(replacement, retry.attempts)) {
                        ++retry.attempts;
                        entry.retryAt = at + static_cast<ULONGLONG>(delay->count());
                        MarkDirty(window, static_cast<UINT>(delay->count()));
                    } else if (CornerRetryDelay(replacement, 0)) {
                        entry.retryAt = at + kCornerRetryRestMs;
                        MarkDirty(window, kCornerRetryRestMs);
                    }
                    abandoned[window] = entry;
                }
            }
            deferred.erase(window);
            it = leases.erase(it);
        }
        if (full) {
            Count(Statistic(&WindowCornerGuardStatistics::fullSweeps));
            if (options.onlyProcess) { for (const auto window : options.onlyWindows) Visit(window, again); }
            else {
                struct Context { Observer* self; std::set<HWND>* again; } context{this, &again};
                (void)::EnumWindows([](HWND window, LPARAM value) noexcept -> BOOL {
                    auto& state = *reinterpret_cast<Context*>(value);
                    try { state.self->Visit(window, *state.again); return TRUE; }
                    catch (...) { return FALSE; }
                }, reinterpret_cast<LPARAM>(&context));
            }
        } else {
            for (const auto window : targets) {
                if (leases.contains(window) || !::IsWindow(window)) continue;
                // The test restriction also bounds dirty-only passes to the fixture windows.
                if (options.onlyProcess && std::find(options.onlyWindows.begin(), options.onlyWindows.end(), window) == options.onlyWindows.end()) continue;
                Visit(window, again);
            }
        }
        for (const auto window : again) MarkDirty(window, 250);
        SyncRims();
        if (!leases.empty()) {
            const ULONGLONG current = ::GetTickCount64();
            Queue(static_cast<UINT>(nextLeaseCheck > current ? nextLeaseCheck - current : 1));
        }
        (void)::QueryPerformanceCounter(&finished);
        (void)::QueryPerformanceFrequency(&frequency);
        if (auto* longest = Statistic(&WindowCornerGuardStatistics::longestSweepMicros); longest && frequency.QuadPart > 0) {
            const auto micros = static_cast<std::uint64_t>((finished.QuadPart - started.QuadPart) * 1000000 / frequency.QuadPart);
            auto previous = longest->load(std::memory_order_relaxed);
            while (micros > previous && !longest->compare_exchange_weak(previous, micros, std::memory_order_relaxed)) {}
        }
    }
    // Rings follow their leases: shown, reshaped, recolored and kept directly beneath each clipped
    // window; removed with the clip. A ring that cannot be shown leaves the clip as it is.
    void SyncRims() noexcept {
        try {
            for (auto it = rims.begin(); it != rims.end();) {
                const auto lease = leases.find(it->first);
                if (stopping || lease == leases.end() || !lease->second->Rim()) it = rims.erase(it);
                else ++it;
            }
            if (stopping) return;
            const COLORREF color = RimColor();
            for (const auto& [window, lease] : leases) {
                const auto& rim = lease->Rim();
                if (!rim) continue;
                auto& ring = rims[window];
                if (!ring) ring = std::make_unique<RimWindow>();
                if (!ring->Sync(window, *rim, color)) rims.erase(window);
            }
        } catch (...) {
            rims.clear();
        }
    }
    bool RestoreAll() {
        for (auto it = leases.begin(); it != leases.end();) {
            auto restored = it->second->Restore();
            if (restored || !it->second->HasOwnership()) it = leases.erase(it);
            else ++it;
        }
        SyncRims();
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
    if ((message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE || message == WM_DPICHANGED) && currentObserver) {
        Count(currentObserver->Statistic(&WindowCornerGuardStatistics::events));
        currentObserver->MarkAll();
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}
void CALLBACK EventHook(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) noexcept {
    if (!currentObserver || !window) return;
    auto& observer = *currentObserver;
    if (event >= EVENT_OBJECT_CREATE && (object != OBJID_WINDOW || child != CHILDID_SELF)) return;
    Count(observer.Statistic(&WindowCornerGuardStatistics::events));
    if (event == EVENT_OBJECT_DESTROY && !::IsWindow(window)) {
        observer.abandoned.erase(window);
        observer.materialRetries.erase(window);
        observer.deferred.erase(window);
        if (observer.leases.contains(window)) observer.MarkDirty(window);
        return;
    }
    if (observer.leases.contains(window)) {
        if (event == EVENT_SYSTEM_FOREGROUND) observer.allLeases = true;
        observer.MarkDirty(window);
        return;
    }
    // Activation restacks windows: the rings must stay directly beneath their targets. It is also
    // when applications tend to re-assert their material.
    if (event == EVENT_SYSTEM_FOREGROUND && !observer.leases.empty()) { observer.allLeases = true; observer.Queue(); }
    // Child windows never qualify, and the guard's own rings move with their targets.
    if (::GetAncestor(window, GA_ROOT) != window || platform::WindowClassName(window) == kRimClass) return;
    // A maximized top-level window may be a new candidate; a window the guard gave back must be
    // seen leaving its presentation. Ordinary moves of restored windows are not the guard's concern.
    if (::IsZoomed(window) || observer.abandoned.contains(window) || observer.materialRetries.contains(window))
        observer.MarkDirty(window);
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

int CornerRimWidthPx(UINT dpi) noexcept { return std::max(1, ::MulDiv(1, static_cast<int>(dpi ? dpi : 96), 96)); }

std::optional<std::chrono::milliseconds> CornerRetryDelay(CornerReplacement replacement, int attempt) noexcept {
    const bool reasserted = replacement == CornerReplacement::MaterialReasserted || replacement == CornerReplacement::RegionReasserted;
    if (!reasserted || attempt < 0 || attempt >= kCornerMaterialRetries) return std::nullopt;
    return std::chrono::milliseconds{std::min(8000, 250 << std::min(attempt, 5))};  // 0.25, 0.5, 1, 2, 4, 8 s, ...
}

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
    // AS4-05: a guard that died took its memory of what it replaced with it. If the window still has exactly
    // the clip that guard would have left, this one takes it over, with no region to put back; eligible windows
    // only ever had none or a plain rectangle, so that is the fail-open choice. Anything else is the
    // application's and stays untouched.
    bool adopted = false;
    if (backend.ClearAbandonedClaim(window) && *original) {
        const auto rounded = RoundedRegion(*observed, std::nullopt);
        adopted = rounded && *rounded == **original;
    }
    if (*original && !adopted && !SimpleRegionContainsClip(**original, *observed))
        return MakeError(ErrorCode::Unsupported, "Window owns a nonstandard custom region");
    auto lease = std::unique_ptr<WindowCornerLease>(new WindowCornerLease(backend, window, token,
        observed->process, observed->thread));
    lease->original_ = adopted ? std::optional<CornerRegion>{} : *original;
    lease->installed_ = *original;
    lease->applied_ = adopted;
    auto backdrop = backend.ReadBackdrop(window); if (!backdrop) return backdrop.error();
    lease->originalBackdrop_ = *backdrop;
    if (auto claim = backend.Claim(window, token); !claim) return claim.error();
    lease->claimed_ = true;
    auto applied = lease->Refresh();
    if (!applied) {
        // A material write may have succeeded before the region write failed. Keep that lease
        // in the observer so a failed rollback cannot discard its only recovery snapshot.
        if (lease->claimed_ && (lease->applied_ || lease->backdropApplied_ || lease->uncertain_)) return lease;
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
namespace {
Error Deferred() { return MakeError(ErrorCode::Timeout, "Window corner target is not responding; change deferred"); }
}
// A region write that outlived its bounded wait may still land. Its outcome is read back once the
// target answers again: the requested value, the previous one (it failed), or the application's own.
Result<void> WindowCornerLease::Reconcile() {
    if (!uncertain_) return Ok();
    if (!backend_.Responsive(window_)) return Deferred();
    auto current = backend_.ReadRegion(window_); if (!current) return current.error();
    if (*current == *uncertain_) {
        installed_ = std::move(*uncertain_);
        applied_ = installed_ != original_;
    }
    uncertain_.reset();
    return Ok();
}
// Every region mutation goes through here: never into a target that is not answering (AS4-03). The
// result is read back, because a successful call on another thread's window has been observed not to
// have landed yet: ownership must not end on a clip that is still there. Anything other than the old
// value (the wanted one, or a shape the application or Windows gave the window) means it did land.
Result<void> WindowCornerLease::WriteOwnedRegion(const std::optional<CornerRegion>& value) {
    if (!backend_.Responsive(window_)) return Deferred();
    if (auto written = backend_.WriteRegion(window_, value); !written) {
        if (written.error().code == ErrorCode::Timeout) uncertain_ = value;
        return written;
    }
    auto landed = backend_.ReadRegion(window_);
    if (!landed || (*landed != value && *landed == installed_)) {
        uncertain_ = value;
        return Deferred();
    }
    return Ok();
}
Result<bool> WindowCornerLease::Refresh() try {
    auto observed = ObserveOwned(); if (!observed) return observed.error();
    if (auto reconciled = Reconcile(); !reconciled) return reconciled.error();
    auto current = backend_.ReadRegion(window_); if (!current) return current.error();
    if (*current != installed_) {
        // The app's region wins, but our separately leased material still needs restoration. D56: no region,
        // or a plain rectangle around the clip (Chromium keeps a maximized window to the work area this
        // way), is no shape of its own: like a re-asserted material, it does not end the rounding.
        const bool plain = !*current || SimpleRegionContainsClip(**current, *observed);
        auto restored = Restore(); if (!restored) return restored.error();
        replacement_ = plain ? CornerReplacement::RegionReasserted : CornerReplacement::Region;
        return MakeError(ErrorCode::InvalidState, "Application replaced its window region");
    }
    if (!observed->eligible) { auto restored = Restore(); if (!restored) return restored.error(); return false; }
    if (originalBackdrop_) {
        auto backdrop = backend_.ReadBackdrop(window_); if (!backdrop) return backdrop.error();
        if (!*backdrop) return MakeError(ErrorCode::Unavailable, "Window corner backdrop temporarily unavailable");
        const DWORD expected = backdropApplied_ ? DWMSBT_NONE : *originalBackdrop_;
        if (**backdrop != expected) {
            const auto chosen = **backdrop;
            auto restored = Restore(); if (!restored) return restored.error();
            replacement_ = chosen == *originalBackdrop_ ? CornerReplacement::MaterialReasserted : CornerReplacement::Material;
            return MakeError(ErrorCode::InvalidState, "Application replaced its system backdrop");
        }
        if (*originalBackdrop_ >= DWMSBT_MAINWINDOW && !backdropApplied_) {
            // DWM's system material fills the full rectangular bounds independently of the
            // window region. Lease NONE while clipped; do not disable non-client rendering.
            if (!backend_.Responsive(window_)) return Deferred();
            if (auto written = backend_.WriteBackdrop(window_, DWMSBT_NONE); !written) return written.error();
            backdropApplied_ = true;
            // The material change may recalculate the visible frame; use a fresh observation.
            observed = ObserveOwned(); if (!observed) return observed.error();
            if (!observed->eligible) { auto restored = Restore(); if (!restored) return restored.error(); return false; }
            backdrop = backend_.ReadBackdrop(window_); if (!backdrop) return backdrop.error();
            if (!*backdrop) return MakeError(ErrorCode::Unavailable, "Window corner backdrop temporarily unavailable");
            if (**backdrop != DWMSBT_NONE) {
                const auto chosen = **backdrop;
                auto restored = Restore(); if (!restored) return restored.error();
                replacement_ = chosen == *originalBackdrop_ ? CornerReplacement::MaterialReasserted : CornerReplacement::Material;
                return MakeError(ErrorCode::InvalidState, "Application replaced its backdrop during update");
            }
            current = backend_.ReadRegion(window_); if (!current) return current.error();
            if (*current != installed_) {
                auto restored = Restore(); if (!restored) return restored.error();
                replacement_ = CornerReplacement::Region;
                return MakeError(ErrorCode::InvalidState, "Application replaced its region during backdrop update");
            }
        }
    }
    auto rounded = RoundedRegion(*observed, original_); if (!rounded) return rounded.error();
    std::optional<CornerRegion> next{std::move(*rounded)};
    rim_ = RimFor(*observed);  // also follows a same-size move to another position or monitor
    if (next == installed_) return true;
    if (auto written = WriteOwnedRegion(next); !written) return written.error();
    installed_ = std::move(next); applied_ = true;
    if (!backend_.Owns(window_, token_)) {
        claimed_ = false;
        return MakeError(ErrorCode::InvalidState, "Window corner lifetime changed during application");
    }
    return true;
} catch (...) { return MakeError(ErrorCode::Internal, "Window corner lease refresh failed"); }
Result<void> WindowCornerLease::Restore() try {
    rim_.reset();  // the rim leaves with the clip, never stays around an unclipped window
    if (!claimed_) return Ok();
    // Before a successful clip there is no region mutation to undo. This also releases a claim
    // if allocation or the first native write failed during Acquire. A write still in flight is
    // a mutation that may yet land, so it keeps the claim until it is reconciled.
    if (!applied_ && !backdropApplied_ && !uncertain_) { backend_.Release(window_, token_); claimed_ = false; return Ok(); }
    auto observed = ObserveOwned();
    if (!observed) return observed.error();
    if (auto reconciled = Reconcile(); !reconciled) return reconciled.error();
    if (applied_) {
        auto current = backend_.ReadRegion(window_); if (!current) return current.error();
        if (*current == installed_) {
            if (auto written = WriteOwnedRegion(original_); !written) return written;
            installed_ = original_;
        }
        applied_ = false;
    }
    if (backdropApplied_) {
        auto backdrop = backend_.ReadBackdrop(window_); if (!backdrop) return backdrop.error();
        if (!*backdrop) return MakeError(ErrorCode::Unavailable, "Window corner backdrop restoration unavailable");
        if (**backdrop == DWMSBT_NONE) {
            if (!backend_.Responsive(window_)) return Deferred();
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
    WindowCornersOptions options;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> wanted{false};
    std::atomic<CornerGuardState> state{CornerGuardState::Off};
    std::atomic<unsigned> failures{0};
    std::atomic<ULONGLONG> retryAt{0};
    std::atomic<bool> scheduled{false};
    std::unique_ptr<GuardSession> session; // worker only
    ULONGLONG readyAt = 0;                 // worker only
    WorkerPool worker{{1, L"Aether window corner recovery", ComApartment::None}};
    Impl(bool value, std::filesystem::path path, WindowCornersOptions chosen)
        : allowed(value), executable(std::move(path)), options(chosen) {}
    bool Exhausted() const noexcept { return failures.load() >= std::max(1u, options.maximumAttempts); }
    void Fail(const char* what) {
        const auto count = failures.fetch_add(1) + 1;
        const auto shift = std::min(count - 1, 6u);
        retryAt = ::GetTickCount64() + static_cast<ULONGLONG>(options.firstRetryDelay.count()) * (1ull << shift);
        state = CornerGuardState::Failed;
        if (Exhausted()) AETHER_LOG_WARN("app", "window corner guard {}; giving up after {} attempts", what, count);
        else AETHER_LOG_WARN("app", "window corner guard {}; retrying", what);
    }
    // Worker only. Brings the helper in line with the wanted state, one bounded step at a time.
    void Reconcile() {
        if (cancelled.load() || !wanted.load()) {
            session.reset();
            state = CornerGuardState::Off;
            return;
        }
        if (session) {
            if (::WaitForSingleObject(session->process.Get(), 0) == WAIT_TIMEOUT) { state = CornerGuardState::Ready; return; }
            // The helper exited on its own: its leases restored themselves unless it crashed (AS4-05).
            session.reset();
            if (::GetTickCount64() - readyAt > 5 * 60 * 1000) failures = 0;  // a long healthy run starts a new budget
            Fail("exited unexpectedly");
            return;
        }
        if (Exhausted() || ::GetTickCount64() < retryAt.load()) { if (failures.load()) state = CornerGuardState::Failed; return; }
        state = CornerGuardState::Starting;
        auto created = StartGuard(executable, cancelled);
        if (created && !cancelled.load() && wanted.load()) {
            session = std::move(*created);
            readyAt = ::GetTickCount64();
            state = CornerGuardState::Ready;
            AETHER_LOG_INFO("app", "window corner guard started");
        } else if (!created && !cancelled.load()) {
            Fail("unavailable");
        } else {
            state = CornerGuardState::Off;  // cancelled or no longer wanted while starting
        }
    }
    bool Schedule() {
        if (scheduled.exchange(true)) return true;
        const bool posted = worker.Post([this] {
            scheduled = false;
            try { Reconcile(); } catch (...) { state = CornerGuardState::Failed; AETHER_LOG_WARN("app", "window corner guard unavailable"); }
        });
        if (!posted) scheduled = false;
        return posted;
    }
};
WindowCorners::WindowCorners(bool allowNativeAccess, std::filesystem::path hostExecutable, WindowCornersOptions options)
    : impl_(std::make_unique<Impl>(allowNativeAccess, std::move(hostExecutable), options)) {}
WindowCorners::~WindowCorners() { Stop(); }
Result<void> WindowCorners::Request(bool enabled) noexcept try {
    if (impl_->cancelled.load()) return MakeError(ErrorCode::InvalidState, "Window corner controller has stopped");
    if (enabled && !impl_->allowed) return MakeError(ErrorCode::AccessDenied, "Window corners are disabled in isolated sessions");
    const bool previous = impl_->wanted.exchange(enabled);
    if (enabled && !previous) { impl_->failures = 0; impl_->retryAt = 0; }  // a fresh request gets a fresh budget
    // Identical requests are cheap; a failed guard with budget left is retried by Poll's back-off.
    if (previous == enabled && !(enabled && impl_->state.load() == CornerGuardState::Failed && !impl_->Exhausted())) return Ok();
    if (!impl_->Schedule()) {
        impl_->wanted.store(false);
        return MakeError(ErrorCode::Unavailable, "Window corner worker is stopping");
    }
    return Ok();
} catch (...) { return MakeError(ErrorCode::Internal, "Window corner request failed"); }
void WindowCorners::Poll() noexcept {
    try {
        if (!impl_ || impl_->cancelled.load() || !impl_->wanted.load()) return;
        const auto state = impl_->state.load();
        // Ready: the worker checks that the helper still runs. Failed: retry once the back-off elapsed.
        if (state == CornerGuardState::Ready ||
            (state == CornerGuardState::Failed && !impl_->Exhausted() && ::GetTickCount64() >= impl_->retryAt.load()))
            (void)impl_->Schedule();
    } catch (...) {}
}
CornerGuardHealth WindowCorners::Health() const noexcept {
    if (!impl_) return {};
    return CornerGuardHealth{impl_->wanted.load(), impl_->state.load(), impl_->failures.load()};
}
void WindowCorners::Stop() noexcept {
    if (!impl_ || impl_->cancelled.exchange(true)) return;
    impl_->wanted.store(false);
    impl_->worker.Shutdown();
    impl_->session.reset();
    impl_->state = CornerGuardState::Off;
}

} // namespace aether::app
