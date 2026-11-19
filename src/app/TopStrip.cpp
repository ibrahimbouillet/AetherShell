// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/TopStrip.h"

#include "app/AutoHide.h"
#include "diagnostics/Logger.h"
#include "platform/windows/WindowUtilities.h"

#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace aether::app {
namespace {

constexpr wchar_t kWindowClass[] = L"AetherShell.TopStripKeeper";
constexpr UINT_PTR kLookTimer = 1;
constexpr UINT kLookDelayMs = 300;       // applications place a window just after showing it
constexpr std::size_t kMaxPending = 64;
constexpr int kMinFrameHeightPx = 160;   // a window is never made shorter than this to fit

// The desktop, the shell's taskbars and flyouts, and the system's own surfaces are never moved.
constexpr std::array<std::wstring_view, 10> kShellClasses{
    L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"TopLevelWindowForOverflowXamlIsland",
    L"NotifyIconOverflowWindow", L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow", L"ApplicationFrameInputSinkWindow",
    L"ForegroundStaging"};

[[nodiscard]] bool Empty(const RECT& r) noexcept { return r.right <= r.left || r.bottom <= r.top; }
[[nodiscard]] bool Same(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

thread_local TopStripKeeper* t_keeper = nullptr;

}  // namespace

std::optional<RECT> FitBelowStrip(const StripWindowFacts& facts, const RECT& strip) noexcept {
    if (Empty(strip) || Empty(facts.frame) || Empty(facts.monitor) || Empty(facts.outer)) return std::nullopt;
    RECT onMonitor{}, reached{};
    if (!::IntersectRect(&onMonitor, &strip, &facts.monitor)) return std::nullopt;     // another monitor's window
    if (!::IntersectRect(&reached, &facts.frame, &strip)) return std::nullopt;         // clear of the strip
    // Full screen (a game, a video, a presentation): the bar steps aside for it, not the other way round.
    if (facts.frame.left <= facts.monitor.left && facts.frame.top <= facts.monitor.top && facts.frame.right >= facts.monitor.right &&
        facts.frame.bottom >= facts.monitor.bottom)
        return std::nullopt;
    const int floor = strip.bottom;
    // The invisible borders around the visible frame, kept as they are.
    const LONG left = facts.frame.left - facts.outer.left, top = facts.frame.top - facts.outer.top;
    const LONG right = facts.outer.right - facts.frame.right, bottom = facts.outer.bottom - facts.frame.bottom;
    if (facts.maximized) {
        // Windows lays maximized windows out in the work area: once that leaves the strip out, the window is put
        // back exactly where Windows would put it. Before that, moving it would only fight Windows.
        if (facts.work.top < floor || Empty(facts.work)) return std::nullopt;
        const RECT fitted{facts.work.left - left, facts.work.top - top, facts.work.right + right, facts.work.bottom + bottom};
        if (Same(fitted, facts.outer)) return std::nullopt;
        return fitted;
    }
    const LONG down = floor - facts.frame.top;
    if (down <= 0) return std::nullopt;
    RECT moved = facts.outer;
    ::OffsetRect(&moved, 0, down);
    const LONG frameBottom = facts.frame.bottom + down;
    if (facts.resizable && !Empty(facts.work) && facts.work.bottom > floor && frameBottom > facts.work.bottom) {
        // Shorter, so it still ends at the bottom of the work area; never shorter than a usable height.
        const LONG height = std::max<LONG>(facts.work.bottom - floor, std::min<LONG>(kMinFrameHeightPx, facts.frame.bottom - facts.frame.top));
        moved.bottom = floor + height + bottom;
    }
    return moved;
}

bool StripRetryBudget::Take(HWND window, std::uint64_t now) {
    std::erase_if(entries_, [&](const Entry& e) { return now < e.at || now - e.at >= windowMs_; });
    const auto taken = std::count_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.window == window; });
    if (static_cast<unsigned>(taken) >= limit_) return false;
    entries_.push_back({window, now});
    return true;
}

struct TopStripKeeper::Impl {
    bool allowed = false;
    bool enabled = false;
    TopStripCallbacks callbacks;
    HWND window = nullptr;
    std::array<HWINEVENTHOOK, 4> hooks{};
    std::vector<HWND> pending;
    bool sweep = false;
    HWND dragging = nullptr;   // a window being moved or sized by hand: left alone until it is let go
    StripRetryBudget budget;
    std::optional<bool> lastCovered;
    std::uint64_t lastMissing = 0;
    DWORD self = ::GetCurrentProcessId();

    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_TIMER && wParam == kLookTimer) {
            ::KillTimer(hwnd, kLookTimer);
            if (auto* impl = reinterpret_cast<Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) impl->Look();
            return 0;
        }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static void CALLBACK OnEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG child, DWORD, DWORD) {
        auto* keeper = t_keeper;
        if (!keeper || !keeper->impl_ || !keeper->impl_->enabled) return;
        auto& p = *keeper->impl_;
        if (object != OBJID_WINDOW || child != CHILDID_SELF || !hwnd || ::GetAncestor(hwnd, GA_ROOT) != hwnd) return;
        switch (event) {
            case EVENT_SYSTEM_MOVESIZESTART: p.dragging = hwnd; return;
            case EVENT_SYSTEM_MOVESIZEEND:
                if (p.dragging == hwnd) p.dragging = nullptr;
                p.Queue(hwnd);
                return;
            case EVENT_OBJECT_HIDE:
            case EVENT_SYSTEM_MINIMIZESTART:
                p.Queue(nullptr);   // something may have left the strip: only the cover is looked at again
                return;
            default: p.Queue(hwnd); return;
        }
    }

    bool EnsureWindow() {
        if (window) return true;
        WNDCLASSEXW cls{sizeof(cls)};
        cls.lpfnWndProc = &Impl::Proc;
        cls.hInstance = ::GetModuleHandleW(nullptr);
        cls.lpszClassName = kWindowClass;
        (void)::RegisterClassExW(&cls);   // already registered is fine
        window = ::CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
        if (!window) return false;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        return true;
    }

    void Hook(bool on, TopStripKeeper* owner) {
        if (on) {
            t_keeper = owner;
            constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
            hooks[0] = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &Impl::OnEvent, 0, 0, flags);
            hooks[1] = ::SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, &Impl::OnEvent, 0, 0, flags);
            hooks[2] = ::SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, &Impl::OnEvent, 0, 0, flags);
            hooks[3] = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, &Impl::OnEvent, 0, 0, flags);
            if (std::any_of(hooks.begin(), hooks.end(), [](HWINEVENTHOOK h) { return h == nullptr; }))
                AETHER_LOG_WARN("app", "top strip: some window events cannot be watched; windows are checked less often");
            return;
        }
        for (auto& hook : hooks) {
            if (hook) ::UnhookWinEvent(hook);
            hook = nullptr;
        }
        if (t_keeper == owner) t_keeper = nullptr;
    }

    void Queue(HWND hwnd) {
        if (!enabled || !window) return;
        if (hwnd && std::find(pending.begin(), pending.end(), hwnd) == pending.end()) {
            if (pending.size() >= kMaxPending) sweep = true;
            else pending.push_back(hwnd);
        }
        (void)::SetTimer(window, kLookTimer, kLookDelayMs, nullptr);   // restarted: one look after a burst of events
    }

    // Reads what the keeper needs; false for a window it never moves.
    bool Facts(HWND hwnd, StripWindowFacts& facts) const {
        if (!::IsWindow(hwnd) || !::IsWindowVisible(hwnd) || ::IsIconic(hwnd) || ::GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
        const auto style = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
        const auto extended = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        if (style & WS_CHILD) return false;
        if (extended & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST)) return false;
        // An application window has a caption or a sizing border; menus, tooltips and popups have neither.
        if ((style & WS_CAPTION) != WS_CAPTION && !(style & WS_THICKFRAME)) return false;
        DWORD process = 0;
        if (!::GetWindowThreadProcessId(hwnd, &process) || process == self) return false;
        if (platform::IsWindowCloaked(hwnd)) return false;
        try {
            const auto name = platform::WindowClassName(hwnd);
            for (const auto shell : kShellClasses)
                if (name == shell) return false;
        } catch (...) {
            return false;
        }
        if (!::GetWindowRect(hwnd, &facts.outer)) return false;
        if (FAILED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &facts.frame, sizeof(facts.frame)))) facts.frame = facts.outer;
        MONITORINFO info{sizeof(info)};
        if (!::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) return false;
        facts.monitor = info.rcMonitor;
        facts.work = info.rcWork;
        facts.maximized = ::IsZoomed(hwnd) != FALSE;
        facts.resizable = (style & WS_THICKFRAME) != 0;
        return true;
    }

    // True when the window was asked to move.
    bool Place(HWND hwnd, const RECT& strip, std::uint64_t now) {
        if (hwnd == dragging) return false;
        StripWindowFacts facts;
        if (!Facts(hwnd, facts)) return false;
        RECT reached{};
        if (!::IntersectRect(&reached, &facts.frame, &strip)) return false;
        if (facts.maximized && facts.work.top < strip.bottom) {
            // The work area still includes the strip: the reservation must come back first (the bar bounds it).
            if (now - lastMissing >= 3000 && callbacks.reservationMissing) {
                lastMissing = now;
                callbacks.reservationMissing();
            }
            return false;
        }
        const auto fitted = FitBelowStrip(facts, strip);
        if (!fitted || !budget.Take(hwnd, now)) return false;
        constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS;
        if (!::SetWindowPos(hwnd, nullptr, fitted->left, fitted->top, fitted->right - fitted->left, fitted->bottom - fitted->top, flags))
            return false;
        AETHER_LOG_INFO("app", "top strip: {} put back below the Top Bar", facts.maximized ? "a maximized window" : "a window");
        return true;
    }

    void Look() {
        if (!enabled) return;
        const auto strip = callbacks.strip ? callbacks.strip() : std::nullopt;
        std::vector<HWND> windows;
        windows.swap(pending);
        if (strip && !Empty(*strip)) {
            const auto now = ::GetTickCount64();
            if (sweep) {
                sweep = false;
                ::EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
                    auto& list = *reinterpret_cast<std::vector<HWND>*>(data);
                    if (list.size() < 1024 && ::IsWindowVisible(hwnd)) list.push_back(hwnd);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&windows));
            }
            bool moved = false;
            for (const HWND hwnd : windows) {
                try {
                    moved = Place(hwnd, *strip, now) || moved;
                } catch (...) {
                }
            }
            // The moves are asynchronous: what they leave free is seen by one more look a moment later.
            if (moved) (void)::SetTimer(window, kLookTimer, 2 * kLookDelayMs, nullptr);
        } else {
            sweep = false;
        }
        Report(strip);
    }

    // Whether an application window still lies under the strip.
    void Report(const std::optional<RECT>& strip) {
        const bool covered = strip && !Empty(*strip) && ApplicationWindowCovers(*strip);
        if (lastCovered == covered) return;
        lastCovered = covered;
        if (callbacks.covered) callbacks.covered(covered);
    }
};

TopStripKeeper::TopStripKeeper(bool allowNativeAccess, TopStripCallbacks callbacks) : impl_(std::make_unique<Impl>()) {
    impl_->allowed = allowNativeAccess;
    impl_->callbacks = std::move(callbacks);
}

TopStripKeeper::~TopStripKeeper() {
    SetEnabled(false);
    if (impl_->window) {
        ::SetWindowLongPtrW(impl_->window, GWLP_USERDATA, 0);
        ::DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
}

void TopStripKeeper::SetEnabled(bool enabled) {
    auto& p = *impl_;
    enabled = enabled && p.allowed;
    if (enabled == p.enabled) return;
    if (enabled && !p.EnsureWindow()) {
        AETHER_LOG_WARN("app", "top strip: the keeper could not start ({})", ::GetLastError());
        return;
    }
    p.enabled = enabled;
    p.Hook(enabled, this);
    p.pending.clear();
    p.dragging = nullptr;
    if (enabled) {
        p.sweep = true;
        p.Queue(nullptr);
    } else {
        if (p.window) ::KillTimer(p.window, kLookTimer);
        p.sweep = false;
        if (p.lastCovered.value_or(false) && p.callbacks.covered) p.callbacks.covered(false);
        p.lastCovered.reset();
    }
}

bool TopStripKeeper::Enabled() const noexcept { return impl_->enabled; }

void TopStripKeeper::SweepSoon() {
    auto& p = *impl_;
    if (!p.enabled) return;
    p.sweep = true;
    p.Queue(nullptr);
}

}  // namespace aether::app
