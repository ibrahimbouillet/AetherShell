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
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <thread>

namespace aether::app {
namespace {
// Marks an earlier version's guard left on the windows it clipped (before D72 it gave other applications'
// windows a rounded region). They are only read here, and removed with such a clip once the guard that made
// it is gone.
constexpr wchar_t kLeaseProperty[] = L"AetherShell.WindowCornerGuard.Lease";
constexpr wchar_t kLeaseOwnerProperty[] = L"AetherShell.WindowCornerLeaseOwner";
constexpr wchar_t kRimClass[] = L"AetherShell.WindowCornerGuard.Rim";
constexpr wchar_t kPiecesClass[] = L"AetherShell.WindowCornerGuard.Corners";
// Stacking can change with no event the guard hears, so while it shows anything it also looks on this slow
// schedule. With nothing shown it has no timer at all.
constexpr UINT kCheckMs = 2000;
// An application that keeps putting itself above the guard's windows is not fought: after this many
// restackings within kPlacementWindowMs the guard leaves that monitor alone for kPlacementRestMs.
constexpr int kPlacementBurst = 12;
constexpr ULONGLONG kPlacementWindowMs = 2000;
constexpr ULONGLONG kPlacementRestMs = 2000;

Error NativeError(const char* text) { return MakeError(ErrorCode::Platform, text, ::GetLastError()); }
void Count(std::atomic<std::uint64_t>* counter) noexcept { if (counter) counter->fetch_add(1, std::memory_order_relaxed); }

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

// In a corner whose arc has its centre at (radius, radius): the first pixel of row `y`, counted from the
// outline's edge, whose centre lies within `reach` of that centre. `radius` when the row has none.
int ArcStart(int radius, int y, double reach) noexcept {
    const double dy = static_cast<double>(radius) - (static_cast<double>(y) + 0.5);
    if (!(reach > 0.0) || dy > reach) return radius;
    const double dx = std::sqrt(reach * reach - dy * dy);
    return std::clamp(static_cast<int>(std::ceil(static_cast<double>(radius) - dx - 0.5)), 0, radius);
}

// Rows of equal extent that lie one under the other become one rectangle: fewer pieces to draw.
std::vector<RECT> MergeRows(std::vector<RECT> rects) {
    std::sort(rects.begin(), rects.end(), [](const RECT& a, const RECT& b) {
        return a.left != b.left ? a.left < b.left : a.right != b.right ? a.right < b.right : a.top < b.top;
    });
    std::vector<RECT> merged;
    merged.reserve(rects.size());
    for (const RECT& rect : rects) {
        if (!merged.empty() && merged.back().left == rect.left && merged.back().right == rect.right && merged.back().bottom == rect.top)
            merged.back().bottom = rect.bottom;
        else merged.push_back(rect);
    }
    return merged;
}

LRESULT CALLBACK OverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
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

// A window of the guard's own, shaped by rectangles: click-through, never activated, with no taskbar entry
// and absent from Alt+Tab. It is created hidden; the guard shows it when it stacks it.
class ShapedWindow {
public:
    explicit ShapedWindow(const wchar_t* className) noexcept : class_(className) {}
    ShapedWindow(const ShapedWindow&) = delete;
    ShapedWindow& operator=(const ShapedWindow&) = delete;
    ~ShapedWindow() { if (window_) ::DestroyWindow(window_); }
    [[nodiscard]] HWND Handle() const noexcept { return window_; }
    bool Ensure() {
        if (window_) return true;
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = class_;
        type.lpfnWndProc = OverlayProc;
        if (!platform::RegisterWindowClassOnce(type)) return false;
        window_ = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, class_, L"",
                                    WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, nullptr);
        if (!window_) return false;
        (void)::SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
        return true;
    }
    // Gives the window the shape of `rects` (its own coordinates) at `bounds` (screen pixels).
    bool Shape(const RECT& bounds, const std::vector<RECT>& rects) {
        platform::UniqueRegion combined(::CreateRectRgn(0, 0, 0, 0));
        if (!combined) return false;
        for (const RECT& rect : rects) {
            platform::UniqueRegion part(::CreateRectRgn(rect.left, rect.top, rect.right, rect.bottom));
            if (!part || ::CombineRgn(combined.Get(), combined.Get(), part.Get(), RGN_OR) == ERROR) return false;
        }
        if (!::SetWindowPos(window_, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) return false;
        if (!::SetWindowRgn(window_, combined.Get(), TRUE)) return false;
        (void)combined.Release();  // owned by the window now
        return true;
    }
    void Color(COLORREF color) {
        if (color == color_) return;
        color_ = color;
        (void)::SetWindowLongPtrW(window_, GWLP_USERDATA, static_cast<LONG_PTR>(color));
        ::InvalidateRect(window_, nullptr, TRUE);
    }
    void Hide() noexcept { if (window_ && ::IsWindowVisible(window_)) ::ShowWindow(window_, SW_HIDE); }

private:
    const wchar_t* class_;
    HWND window_ = nullptr;
    COLORREF color_ = CLR_INVALID;
};

// D45: the thin continuous perimeter, the band just inside the rounded outline, in one neutral color.
class RimWindow {
public:
    [[nodiscard]] HWND Handle() const noexcept { return window_.Handle(); }
    bool Sync(const CornerRim& rim, COLORREF color) {
        if (!window_.Ensure()) return false;
        if (!shape_ || !(*shape_ == rim)) {
            shape_.reset();
            if (!window_.Shape(rim.bounds, CornerRimRects(rim.bounds.right - rim.bounds.left, rim.bounds.bottom - rim.bounds.top,
                                                          rim.radiusPx, rim.widthPx))) return false;
            shape_ = rim;
        }
        window_.Color(color);
        return true;
    }
    void Hide() noexcept { window_.Hide(); }

private:
    ShapedWindow window_{kRimClass};
    std::optional<CornerRim> shape_;
};

// D72: the four corners outside the rounded outline. Each rectangle of them shows the desktop that lies at its own
// place, live, through a DWM thumbnail of the desktop window at its natural size: what a cut-out corner would let
// through. One thumbnail per rectangle, because DWM draws a thumbnail over its whole rectangle whatever the shape
// of the window that holds it (measured: one per corner square came out square).
class PiecesWindow {
public:
    PiecesWindow() = default;
    PiecesWindow(const PiecesWindow&) = delete;
    PiecesWindow& operator=(const PiecesWindow&) = delete;
    ~PiecesWindow() { Unmap(); }
    [[nodiscard]] HWND Handle() const noexcept { return window_.Handle(); }
    bool Sync(const CornerRim& rim, HWND source) {
        if (!window_.Ensure()) return false;
        if (!shape_ || !(*shape_ == rim)) {
            shape_.reset();
            Unmap();
            rects_ = CornerPieceRects(rim.bounds.right - rim.bounds.left, rim.bounds.bottom - rim.bounds.top, rim.radiusPx);
            if (rects_.empty() || !window_.Shape(rim.bounds, rects_)) return false;
            shape_ = rim;
        }
        RECT from{};
        if (!source || !::GetWindowRect(source, &from)) { Unmap(); return false; }
        if (source == source_ && mapped_ && ::EqualRect(&from, &sourceBounds_)) return true;
        Unmap();
        thumbnails_.assign(rects_.size(), nullptr);
        for (std::size_t index = 0; index < rects_.size(); ++index) {
            RECT there = rects_[index];
            ::OffsetRect(&there, rim.bounds.left - from.left, rim.bounds.top - from.top);
            // A corner that is not on the desktop window has nothing to show: better square than wrong.
            if (there.left < 0 || there.top < 0 || there.right > from.right - from.left || there.bottom > from.bottom - from.top) { Unmap(); return false; }
            if (FAILED(::DwmRegisterThumbnail(window_.Handle(), source, &thumbnails_[index]))) {
                thumbnails_[index] = nullptr;
                Unmap();
                return false;
            }
            DWM_THUMBNAIL_PROPERTIES properties{};
            properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY | DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY;
            properties.rcDestination = rects_[index];
            properties.rcSource = there;
            properties.opacity = 255;
            properties.fVisible = TRUE;
            properties.fSourceClientAreaOnly = FALSE;
            if (FAILED(::DwmUpdateThumbnailProperties(thumbnails_[index], &properties))) { Unmap(); return false; }
        }
        source_ = source;
        sourceBounds_ = from;
        mapped_ = true;
        return true;
    }
    void Hide() noexcept { window_.Hide(); }

private:
    void Unmap() noexcept {
        for (auto& thumbnail : thumbnails_) {
            if (thumbnail) (void)::DwmUnregisterThumbnail(thumbnail);
            thumbnail = nullptr;
        }
        thumbnails_.clear();
        source_ = nullptr;
        mapped_ = false;
    }
    ShapedWindow window_{kPiecesClass};
    std::optional<CornerRim> shape_;
    std::vector<RECT> rects_;
    std::vector<HTHUMBNAIL> thumbnails_;
    HWND source_ = nullptr;
    RECT sourceBounds_{};
    bool mapped_ = false;
};

// The window whose picture is the desktop. Windows draws the desktop picture and its icons in Progman. Where
// the icons were moved into a WorkerW of their own (what live-wallpaper programs ask Windows to do), the
// picture is the WorkerW right behind that one.
HWND FindDesktopSource() noexcept {
    const HWND progman = ::FindWindowW(L"Progman", nullptr);
    if (progman && ::FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr)) return ::IsWindowVisible(progman) ? progman : nullptr;
    for (HWND worker = ::FindWindowExW(nullptr, nullptr, L"WorkerW", nullptr); worker; worker = ::FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) {
        if (!::FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr)) continue;
        const HWND picture = ::FindWindowExW(nullptr, worker, L"WorkerW", nullptr);
        if (picture && ::IsWindowVisible(picture)) return picture;
        break;
    }
    return progman && ::IsWindowVisible(progman) ? progman : nullptr;
}

// What one monitor shows: the front maximized window there, and the guard's two windows directly above it.
struct Frame {
    HWND target = nullptr;
    CornerRim rim;
    PiecesWindow pieces;
    RimWindow ring;
    int placements = 0;
    ULONGLONG burstStart = 0;
    ULONGLONG restUntil = 0;
    void Hide() noexcept { pieces.Hide(); ring.Hide(); }
};

struct Observer {
    const WindowCornerGuardOptions& options;
    std::map<HMONITOR, std::unique_ptr<Frame>> frames;
    HWND source = nullptr;
    UINT_PTR timer = 0;
    bool pending = true;
    bool stopping = false;
    explicit Observer(const WindowCornerGuardOptions& value) noexcept : options(value) {}
    std::atomic<std::uint64_t>* Statistic(std::atomic<std::uint64_t> WindowCornerGuardStatistics::* field) const noexcept {
        return options.statistics ? &(options.statistics->*field) : nullptr;
    }
    // Tests name the only windows that may be looked at.
    [[nodiscard]] bool Allowed(HWND window) const noexcept {
        return !options.onlyProcess || std::find(options.onlyWindows.begin(), options.onlyWindows.end(), window) != options.onlyWindows.end();
    }
    [[nodiscard]] bool IsOwn(HWND window) const noexcept {
        for (const auto& [monitor, frame] : frames) if (window == frame->pieces.Handle() || window == frame->ring.Handle()) return true;
        return false;
    }
    [[nodiscard]] bool IsTarget(HWND window) const noexcept {
        for (const auto& [monitor, frame] : frames) if (window == frame->target) return true;
        return false;
    }
    // Something changed that may matter: one look once the waiting messages have been read.
    void Heard() noexcept {
        Count(Statistic(&WindowCornerGuardStatistics::events));
        pending = true;
    }
    // Everything the guard learns of a window. A window that is not visible and maximized is asked nothing more.
    [[nodiscard]] CornerWindowFacts Read(HWND window) const noexcept {
        CornerWindowFacts facts;
        facts.visible = ::IsWindowVisible(window) != FALSE;
        facts.maximized = ::IsZoomed(window) != FALSE;
        if (!facts.visible || !facts.maximized) return facts;
        facts.minimized = ::IsIconic(window) != FALSE;
        facts.topLevel = ::GetAncestor(window, GA_ROOT) == window;
        facts.cloaked = platform::IsWindowCloaked(window);
        const auto extended = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
        facts.toolWindow = (extended & WS_EX_TOOLWINDOW) != 0;
        facts.clickThrough = (extended & WS_EX_TRANSPARENT) != 0;
        // The shell's own windows shape themselves (its Settings window rounds itself), and the fixtures of its
        // tests are no application's: outside a test, neither is the guard's business.
        facts.own = IsOwn(window) || (!options.onlyProcess && platform::WindowClassName(window).starts_with(L"AetherShell."));
        MONITORINFO monitor{sizeof(monitor)};
        if (!::GetWindowRect(window, &facts.bounds) ||
            FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &facts.frame, sizeof(facts.frame))) ||
            !::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
            facts.visible = false;
            return facts;
        }
        facts.monitor = monitor.rcMonitor;
        facts.work = monitor.rcWork;
        facts.dpi = ::GetDpiForWindow(window);
        facts.regionKind = ::GetWindowRgnBox(window, &facts.regionBox);
        return facts;
    }
    struct Top { HWND window = nullptr; CornerRim rim; };
    void Consider(HWND window, std::map<HMONITOR, Top>& tops) const {
        if (!Allowed(window)) return;
        const auto observed = ClassifyCornerWindow(Read(window));
        if (!observed.eligible) return;
        const HMONITOR monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (!monitor || tops.contains(monitor)) return;  // windows come front to back: the first is the front one
        if (const auto rim = CornerFrameFor(observed)) tops.emplace(monitor, Top{window, *rim});
    }
    // One look: the front maximized window of each monitor, and the guard's windows shaped, mapped and stacked
    // for it. Only the guard's own windows are ever changed.
    void Refresh() {
        pending = false;
        if (stopping) return;
        LARGE_INTEGER started{}, finished{}, frequency{};
        (void)::QueryPerformanceCounter(&started);
        Count(Statistic(&WindowCornerGuardStatistics::sweeps));
        // A test shows only the private window it names, never the real desktop.
        if (options.onlyProcess) source = options.desktopSource;
        else if (!source || !::IsWindow(source)) source = FindDesktopSource();
        std::map<HMONITOR, Top> tops;
        struct Context { Observer* self; std::map<HMONITOR, Top>* tops; } context{this, &tops};
        (void)::EnumWindows([](HWND window, LPARAM value) noexcept -> BOOL {
            auto& state = *reinterpret_cast<Context*>(value);
            try { state.self->Consider(window, *state.tops); return TRUE; }
            catch (...) { return FALSE; }
        }, reinterpret_cast<LPARAM>(&context));
        for (auto it = frames.begin(); it != frames.end();) {
            if (tops.contains(it->first)) ++it;
            else it = frames.erase(it);
        }
        const COLORREF color = RimColor();
        for (const auto& [monitor, top] : tops) {
            auto& frame = frames[monitor];
            if (!frame) frame = std::make_unique<Frame>();
            frame->target = top.window;
            frame->rim = top.rim;
            // Without the desktop's picture the corners stay square: a rim alone would draw arcs over them.
            if (!source || !frame->pieces.Sync(top.rim, source) || !frame->ring.Sync(top.rim, color)) { frame->Hide(); continue; }
            Place(*frame);
        }
        if (!frames.empty() && !timer) timer = ::SetTimer(nullptr, 0, kCheckMs, nullptr);
        else if (frames.empty() && timer) { ::KillTimer(nullptr, timer); timer = 0; }
        (void)::QueryPerformanceCounter(&finished);
        (void)::QueryPerformanceFrequency(&frequency);
        if (auto* longest = Statistic(&WindowCornerGuardStatistics::longestSweepMicros); longest && frequency.QuadPart > 0) {
            const auto micros = static_cast<std::uint64_t>((finished.QuadPart - started.QuadPart) * 1000000 / frequency.QuadPart);
            auto previous = longest->load(std::memory_order_relaxed);
            while (micros > previous && !longest->compare_exchange_weak(previous, micros, std::memory_order_relaxed)) {}
        }
    }
    // Keeps the frame's two windows directly above its target, which is itself never moved or restacked.
    // A window in front of the target is therefore in front of them too.
    void Place(Frame& frame) noexcept {
        const HWND pieces = frame.pieces.Handle(), ring = frame.ring.Handle();
        HWND above = ::GetWindow(frame.target, GW_HWNDPREV);
        bool piecesThere = false, ringThere = false;
        while (above && (above == pieces || above == ring)) {
            (above == pieces ? piecesThere : ringThere) = true;
            above = ::GetWindow(above, GW_HWNDPREV);
        }
        const auto isTopmost = [](HWND window) noexcept { return (::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; };
        // Always-on-top like the target, and only then: an always-on-top piece over an ordinary window would also
        // lie over whatever comes in front of that window.
        const bool topmost = isTopmost(frame.target);
        if (piecesThere && ringThere && ::IsWindowVisible(pieces) && ::IsWindowVisible(ring) && isTopmost(pieces) == topmost &&
            isTopmost(ring) == topmost) return;
        const ULONGLONG now = ::GetTickCount64();
        if (now < frame.restUntil) return;
        if (now - frame.burstStart > kPlacementWindowMs) { frame.burstStart = now; frame.placements = 0; }
        if (++frame.placements > kPlacementBurst) {
            frame.restUntil = now + kPlacementRestMs;
            frame.placements = 0;
            return;
        }
        // Windows makes a window always-on-top when it is placed right behind an always-on-top one. Above an ordinary
        // target that is the front ordinary window, the place asked for is therefore the top of the ordinary windows.
        const HWND after = !above || (!topmost && isTopmost(above)) ? HWND_TOP : above;
        for (const HWND overlay : {ring, pieces}) {
            if (isTopmost(overlay) != topmost)
                (void)::SetWindowPos(overlay, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            (void)::SetWindowPos(overlay, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
            Count(Statistic(&WindowCornerGuardStatistics::placements));
        }
    }
    // Before D72 the guard clipped other applications' windows with a rounded region and marked them. A guard
    // that was killed left its clips behind, and the window stayed cut after it was restored. The guard that made
    // them is gone and cannot take them back: this one removes them, once, when it starts. A window that still
    // has its guard (an earlier version running beside a trial of this one) is left to it.
    void ReleaseAbandonedClips() noexcept {
        try {
            std::vector<HWND> marked;
            struct Context { const Observer* self; std::vector<HWND>* marked; } context{this, &marked};
            (void)::EnumWindows([](HWND window, LPARAM value) noexcept -> BOOL {
                auto& state = *reinterpret_cast<Context*>(value);
                try { if (state.self->Allowed(window) && ::GetPropW(window, kLeaseProperty)) state.marked->push_back(window); return TRUE; }
                catch (...) { return FALSE; }
            }, reinterpret_cast<LPARAM>(&context));
            for (const HWND window : marked) {
                const auto owner = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(::GetPropW(window, kLeaseOwnerProperty)));
                if (!owner || owner == ::GetCurrentProcessId()) continue;
                if (const HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, owner)) {
                    const bool running = ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
                    ::CloseHandle(process);
                    if (running) continue;
                } else if (::GetLastError() != ERROR_INVALID_PARAMETER) {
                    continue;
                }
                RECT box{};
                // Such a window only ever had no region or a plain rectangle before it was clipped: a shape on a
                // window that still carries the dead guard's mark is that guard's clip.
                const bool clipped = ::GetWindowRgnBox(window, &box) == COMPLEXREGION;
                Count(clipped ? Statistic(&WindowCornerGuardStatistics::abandonedClips) : nullptr);
                // Removing a region sends the window messages: one that does not answer must not hold the guard.
                std::thread([window, clipped] {
                    if (clipped) (void)::SetWindowRgn(window, nullptr, TRUE);
                    (void)::RemovePropW(window, kLeaseProperty);
                    (void)::RemovePropW(window, kLeaseOwnerProperty);
                }).detach();
            }
        } catch (...) {}
    }
};
thread_local Observer* currentObserver = nullptr;
UINT ExplorerStartedMessage() noexcept {
    static const UINT message = ::RegisterWindowMessageW(L"TaskbarCreated");
    return message;
}
LRESULT CALLBACK BroadcastProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    // The display, the work area between the bars, the theme or the desktop window itself (Explorer started again).
    if ((message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE || message == WM_DPICHANGED ||
         (message && message == ExplorerStartedMessage())) && currentObserver) {
        if (message == ExplorerStartedMessage()) currentObserver->source = nullptr;
        currentObserver->Heard();
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}
void CALLBACK EventHook(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) noexcept {
    if (!currentObserver || currentObserver->stopping) return;
    auto& observer = *currentObserver;
    // Stacking: a window came to the front, or the top-level windows changed order.
    if (event == EVENT_SYSTEM_FOREGROUND || (event == EVENT_OBJECT_REORDER && window == ::GetDesktopWindow())) { observer.Heard(); return; }
    if (!window || object != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (observer.IsTarget(window)) { observer.Heard(); return; }
    // Otherwise only a maximized top-level window matters: it may be the next front one. Ordinary moves of
    // other windows are not the guard's concern, since whatever is in front of the target covers its corners.
    if (!observer.Allowed(window) || observer.IsOwn(window)) return;
    if (::IsZoomed(window) && ::GetAncestor(window, GA_ROOT) == window) observer.Heard();
}

struct GuardSession {
    platform::UniqueHandle process;
    platform::UniqueHandle stop;
    ~GuardSession() { Stop(); }
    void Stop() noexcept {
        if (!process) return;
        (void)::SetEvent(stop.Get());
        // The helper has nothing to put back (D72): it only has its own windows, which leave with it.
        // It is still never terminated; one that is slow to leave is noted and left to finish.
        const auto waited = ::WaitForSingleObject(process.Get(), 5000);
        if (waited == WAIT_OBJECT_0) AETHER_LOG_INFO("app", "window corner guard stopped");
        else AETHER_LOG_WARN("app", "window corner guard has not left yet");
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

CornerWindowObservation ClassifyCornerWindow(const CornerWindowFacts& facts) noexcept {
    CornerWindowObservation result;
    result.bounds = facts.bounds;
    result.frame = facts.frame;
    result.work = facts.work;
    result.dpi = facts.dpi;
    if (!facts.visible || facts.minimized || !facts.maximized || !facts.topLevel || facts.cloaked || facts.toolWindow ||
        facts.clickThrough || facts.own) return result;
    // D56, the owner's rule: rounding belongs to a maximized window sitting between the bars. One that covers
    // the whole monitor (both bars auto-hidden) simply takes the screen, as does full screen.
    if (facts.frame.left <= facts.monitor.left && facts.frame.top <= facts.monitor.top &&
        facts.frame.right >= facts.monitor.right && facts.frame.bottom >= facts.monitor.bottom) return result;
    // A shape of its own stays its own. A plain rectangle that still shows the whole frame is no shape: a
    // browser keeps its maximized window to the work area that way.
    if (facts.regionKind == COMPLEXREGION || facts.regionKind == NULLREGION) return result;
    try {
        const auto layout = settings::ui::experimental::ComputeMaximizedCornerLayout(facts.bounds, facts.frame, facts.work, facts.dpi);
        if (!layout) return result;
        if (facts.regionKind == SIMPLEREGION) {
            const auto& clip = layout->clipPx;
            if (facts.regionBox.left > clip.left || facts.regionBox.top > clip.top || facts.regionBox.right < clip.right ||
                facts.regionBox.bottom < clip.bottom) return result;
        }
    } catch (...) {
        return result;
    }
    result.eligible = true;
    return result;
}

std::optional<CornerRim> CornerFrameFor(const CornerWindowObservation& observation) {
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

std::vector<RECT> CornerPieceRects(int widthPx, int heightPx, int radiusPx) {
    std::vector<RECT> rects;
    const int radius = std::min({radiusPx, widthPx / 2, heightPx / 2});
    if (widthPx <= 0 || heightPx <= 0 || radius <= 0) return rects;
    rects.reserve(static_cast<std::size_t>(radius) * 4);
    for (int y = 0; y < radius; ++y) {
        const int outside = ArcStart(radius, y, radius);  // this many pixels of the row lie outside the outline
        if (outside <= 0) continue;
        rects.push_back({0, y, outside, y + 1});
        rects.push_back({widthPx - outside, y, widthPx, y + 1});
        rects.push_back({0, heightPx - 1 - y, outside, heightPx - y});
        rects.push_back({widthPx - outside, heightPx - 1 - y, widthPx, heightPx - y});
    }
    return MergeRows(std::move(rects));
}

std::vector<RECT> CornerRimRects(int widthPx, int heightPx, int radiusPx, int rimPx) {
    std::vector<RECT> rects;
    if (widthPx <= 0 || heightPx <= 0 || rimPx <= 0) return rects;
    const int radius = std::max(0, std::min({radiusPx, widthPx / 2, heightPx / 2}));
    const int rim = std::min({rimPx, (widthPx + 1) / 2, (heightPx + 1) / 2});
    // Rows nearer than this to the top or the bottom are not plain sides: the rim's own rows, and the corners.
    const int band = std::max(radius, rim);
    const int rows = std::min(band, (heightPx + 1) / 2);
    rects.reserve(static_cast<std::size_t>(rows) * 4 + 2);
    const auto both = [&](int y, int left, int right) {
        if (right <= left) return;
        rects.push_back({left, y, right, y + 1});
        if (heightPx - 1 - y != y) rects.push_back({left, heightPx - 1 - y, right, heightPx - y});
    };
    for (int y = 0; y < rows; ++y) {
        const int from = y < radius ? ArcStart(radius, y, radius) : 0;   // where the outline begins on this row
        if (y < rim) { both(y, from, widthPx - from); continue; }       // one of the rim's own rows, edge to edge
        const int to = ArcStart(radius, y, static_cast<double>(radius) - rim);   // where the inside begins
        both(y, from, to);
        both(y, widthPx - to, widthPx - from);
    }
    if (heightPx - 2 * band > 0) {
        rects.push_back({0, band, rim, heightPx - band});
        rects.push_back({widthPx - rim, band, widthPx, heightPx - band});
    }
    return MergeRows(std::move(rects));
}

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
    if (options.desktopSource) {
        DWORD process = 0; (void)::GetWindowThreadProcessId(options.desktopSource, &process);
        if (!options.onlyProcess || process != options.onlyProcess)
            return MakeError(ErrorCode::AccessDenied, "Window corner test desktop must be the test's own window");
    }
    platform::EnsurePerMonitorDpiAwareness();
    Observer observer(options);
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
    std::array<HWINEVENTHOOK, 4> hooks{};
    struct Unhook { std::array<HWINEVENTHOOK, 4>& values; ~Unhook() { for (auto hook : values) if (hook) ::UnhookWinEvent(hook); } } cleanup{hooks};
    constexpr DWORD hookFlags = WINEVENT_OUTOFCONTEXT;
    hooks[0] = ::SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, EventHook, 0, 0, hookFlags);
    hooks[1] = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, EventHook, 0, 0, hookFlags);
    hooks[2] = ::SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, EventHook, 0, 0, hookFlags);
    // A maximized window arriving from, or leaving for, another virtual desktop.
    hooks[3] = ::SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, EventHook, 0, 0, hookFlags);
    if (std::any_of(hooks.begin(), hooks.end(), [](auto hook) { return !hook; }))
        return NativeError("Window corner event observation unavailable");
    if (::WaitForSingleObject(parent, 0) != WAIT_TIMEOUT || ::WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return 0;
    observer.ReleaseAbandonedClips();
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
            if (message.message == WM_TIMER && !message.hwnd && message.wParam == observer.timer) observer.pending = true;
            else { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        }
        // However many events arrived together, the windows are looked at once.
        if (!exit && observer.pending) observer.Refresh();
    }
    // Leaving is all there is to do: the guard's windows go, and no application was ever changed.
    observer.stopping = true;
    if (observer.timer) { ::KillTimer(nullptr, observer.timer); observer.timer = 0; }
    for (auto& hook : hooks) if (hook) { ::UnhookWinEvent(hook); hook = nullptr; }
    observer.frames.clear();
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
            // The helper exited on its own: its windows went with it, and no application was changed (D72).
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
