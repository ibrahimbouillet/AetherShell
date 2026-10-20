// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/WorkAreaInsets.h"

#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/WindowUtilities.h"

#include <shellapi.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace aether::app {
namespace {
constexpr UINT kAppBarMessage = WM_APP + 126;
constexpr UINT kRefreshMessage = WM_APP + 127;

Error NativeError(const char* message) {
    return MakeError(ErrorCode::Platform, message, ::GetLastError());
}
bool FitsLong(std::int64_t value) noexcept {
    return value >= std::numeric_limits<LONG>::min() && value <= std::numeric_limits<LONG>::max();
}
} // namespace

Result<WorkAreaInsetLayout> ComputeWorkAreaInsets(const RECT& monitor, UINT dpi, float insetDip) {
    const auto width = static_cast<std::int64_t>(monitor.right) - monitor.left;
    const auto height = static_cast<std::int64_t>(monitor.bottom) - monitor.top;
    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max() ||
        dpi < 48 || dpi > 768 || !std::isfinite(insetDip) || insetDip <= 0 || insetDip > 100)
        return MakeError(ErrorCode::InvalidArgument, "Invalid work-area inset geometry");
    const auto inset = static_cast<int>(std::lround(static_cast<double>(insetDip) * dpi / 96.0));
    if (inset <= 0 || 2LL * inset >= width)
        return MakeError(ErrorCode::Unavailable, "Monitor is too narrow for work-area insets");
    WorkAreaInsetLayout layout;
    layout.widthPx = inset;
    layout.left = {monitor.left, monitor.top, monitor.left + inset, monitor.bottom};
    layout.right = {monitor.right - inset, monitor.top, monitor.right, monitor.bottom};
    return layout;
}

Result<RECT> FitWorkAreaInset(const RECT& approved, bool leftEdge, int widthPx) {
    const auto height = static_cast<std::int64_t>(approved.bottom) - approved.top;
    if (height <= 0 || height > std::numeric_limits<int>::max() || widthPx <= 0)
        return MakeError(ErrorCode::InvalidArgument, "Invalid approved work-area inset");
    RECT positioned = approved;
    const auto edge = leftEdge ? static_cast<std::int64_t>(approved.left) + widthPx :
        static_cast<std::int64_t>(approved.right) - widthPx;
    if (!FitsLong(edge)) return MakeError(ErrorCode::InvalidArgument, "Work-area inset bounds overflow");
    if (leftEdge) positioned.right = static_cast<LONG>(edge);
    else positioned.left = static_cast<LONG>(edge);
    return positioned;
}

struct WorkAreaInsets::Impl {
    struct Slot {
        Impl* owner = nullptr;
        HWND window = nullptr;
        UINT edge = ABE_LEFT;
        bool registered = false;
        bool notifying = false;
        std::optional<RECT> positioned;
    };

    Impl(bool allowed, float dip) : nativeAllowed(allowed), insetDip(dip), ownerThread(::GetCurrentThreadId()) {
        slots[0].owner = slots[1].owner = this;
        slots[1].edge = ABE_RIGHT;
    }
    bool nativeAllowed;
    float insetDip;
    DWORD ownerThread;
    bool wanted = false;
    bool updating = false;
    bool refreshQueued = false;
    std::array<Slot, 2> slots;

    Result<void> CheckThread() const {
        return ::GetCurrentThreadId() == ownerThread ? Ok() :
            Result<void>(MakeError(ErrorCode::InvalidState, "Work-area insets require their owning UI thread"));
    }
    void QueueRefresh() noexcept {
        if (!wanted || refreshQueued) return;
        for (const auto& slot : slots) if (slot.window) {
            refreshQueued = ::PostMessageW(slot.window, kRefreshMessage, 0, 0) != FALSE;
            return;
        }
    }
    void Unregister(Slot& slot) noexcept {
        const bool registered = slot.registered;
        slot.registered = false; slot.positioned.reset();
        if (registered && slot.window) {
            APPBARDATA data{sizeof(data)}; data.hWnd = slot.window;
            (void)::SHAppBarMessage(ABM_REMOVE, &data);
        }
    }
    void Release() noexcept {
        const bool previous = updating; updating = true;
        refreshQueued = false;
        for (auto& slot : slots) Unregister(slot);
        for (auto& slot : slots) if (slot.window) {
            (void)::DestroyWindow(slot.window);
            slot.window = nullptr;
        }
        updating = previous;
    }
    Result<void> CreateWindows() {
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = L"AetherShell.WorkAreaInset";
        type.lpfnWndProc = &WindowProc;
        if (auto registered = platform::RegisterWindowClassOnce(type); !registered) return registered.error();
        for (auto& slot : slots) {
            if (slot.window) continue;
            slot.window = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                type.lpszClassName, L"Aether Shell work-area inset", WS_POPUP,
                0, 0, 1, 1, nullptr, nullptr, type.hInstance, &slot);
            if (!slot.window) return NativeError("Work-area inset window creation failed");
            // Alpha zero plus WS_EX_TRANSPARENT makes these infrastructure windows fully
            // transparent to both rendering and mouse input, including across process boundaries.
            if (!::SetLayeredWindowAttributes(slot.window, 0, 0, LWA_ALPHA))
                return NativeError("Work-area inset transparency failed");
        }
        return Ok();
    }
    Result<void> Position(Slot& slot, const RECT& proposedBounds, int widthPx) {
        APPBARDATA data{sizeof(data)};
        data.hWnd = slot.window; data.uCallbackMessage = kAppBarMessage;
        if (!slot.registered) {
            if (!::SHAppBarMessage(ABM_NEW, &data)) return MakeError(ErrorCode::Platform, "Work-area inset registration failed");
            slot.registered = true;
        }
        data.uEdge = slot.edge; data.rc = proposedBounds;
        if (!::SHAppBarMessage(ABM_QUERYPOS, &data)) return MakeError(ErrorCode::Platform, "Work-area inset query failed");
        auto proposed = FitWorkAreaInset(data.rc, slot.edge == ABE_LEFT, widthPx);
        if (!proposed) return proposed.error();
        data.rc = *proposed;
        if (!slot.positioned || !::EqualRect(&*slot.positioned, &data.rc)) {
            if (!::SHAppBarMessage(ABM_SETPOS, &data)) return MakeError(ErrorCode::Platform, "Work-area inset reservation failed");
            const auto width = static_cast<std::int64_t>(data.rc.right) - data.rc.left;
            const auto height = static_cast<std::int64_t>(data.rc.bottom) - data.rc.top;
            if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
                return MakeError(ErrorCode::Unavailable, "Work-area inset reservation is empty");
            slot.positioned = data.rc; // honor the actual shell allocation after SETPOS
            if (!::SetWindowPos(slot.window, HWND_BOTTOM, data.rc.left, data.rc.top,
                static_cast<int>(width), static_cast<int>(height), SWP_NOACTIVATE))
                return NativeError("Work-area inset positioning failed");
        }
        if (!::IsWindowVisible(slot.window)) ::ShowWindow(slot.window, SW_SHOWNOACTIVATE);
        return Ok();
    }
    Result<void> Refresh() {
        if (!wanted || updating) return Ok();
        updating = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{updating};
        const auto monitor = platform::PrimaryMonitor();
        auto layout = ComputeWorkAreaInsets(monitor.bounds, monitor.dpi, insetDip);
        if (!layout) return layout.error();
        if (auto created = CreateWindows(); !created) return created;
        for (auto& slot : slots) {
            const auto& proposed = slot.edge == ABE_LEFT ? layout->left : layout->right;
            if (auto positioned = Position(slot, proposed, layout->widthPx); !positioned) return positioned;
        }
        return Ok();
    }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
        auto* slot = reinterpret_cast<Slot*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            slot = static_cast<Slot*>(creation->lpCreateParams);
            slot->window = window;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(slot));
        }
        if (!slot || !slot->owner) return ::DefWindowProcW(window, message, wParam, lParam);
        auto& owner = *slot->owner;
        try {
            if (message == WM_NCHITTEST) return HTTRANSPARENT;
            if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
            if (message == WM_ERASEBKGND) return 1;
            if (message == kAppBarMessage) {
                if (wParam == ABN_POSCHANGED) owner.QueueRefresh();
                return 0; // no independent fullscreen/reveal policy
            }
            if (message == kRefreshMessage) {
                owner.refreshQueued = false;
                if (auto refreshed = owner.Refresh(); !refreshed) { owner.wanted = false; owner.Release(); }
                return 0;
            }
            if (message == WM_DISPLAYCHANGE || message == WM_DPICHANGED || message == WM_SETTINGCHANGE)
                owner.QueueRefresh();
            if (slot->registered && !slot->notifying && (message == WM_ACTIVATE || message == WM_WINDOWPOSCHANGED)) {
                slot->notifying = true;
                APPBARDATA data{sizeof(data)}; data.hWnd = window;
                (void)::SHAppBarMessage(message == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &data);
                slot->notifying = false;
            }
            if (message == WM_DESTROY) owner.Unregister(*slot);
            if (message == WM_NCDESTROY) {
                ::SetWindowLongPtrW(window, GWLP_USERDATA, 0); slot->window = nullptr;
            }
        } catch (...) {
            if (message == kRefreshMessage) { owner.wanted = false; owner.Release(); }
        }
        return ::DefWindowProcW(window, message, wParam, lParam);
    }
};

WorkAreaInsets::WorkAreaInsets(bool allowNativeAccess, float insetDip)
    : impl_(std::make_unique<Impl>(allowNativeAccess, insetDip)) {}
WorkAreaInsets::~WorkAreaInsets() { impl_->wanted = false; impl_->Release(); }
bool WorkAreaInsets::Enabled() const noexcept {
    return impl_->wanted && impl_->slots[0].registered && impl_->slots[1].registered;
}
Result<void> WorkAreaInsets::SetEnabled(bool enabled) {
    if (auto thread = impl_->CheckThread(); !thread) return thread;
    if (!enabled) { impl_->wanted = false; impl_->Release(); return Ok(); }
    if (!impl_->nativeAllowed) return MakeError(ErrorCode::Unavailable, "Native work-area access is disabled");
    impl_->wanted = true;
    return Refresh();
}
Result<void> WorkAreaInsets::Refresh() {
    if (auto thread = impl_->CheckThread(); !thread) return thread;
    try {
        auto result = impl_->Refresh();
        if (!result) { impl_->wanted = false; impl_->Release(); }
        return result;
    } catch (...) {
        impl_->wanted = false; impl_->Release();
        return MakeError(ErrorCode::Internal, "Work-area insets could not be refreshed");
    }
}
Result<void> WorkAreaInsets::OnExplorerRestarted() {
    if (auto thread = impl_->CheckThread(); !thread) return thread;
    const bool wanted = impl_->wanted;
    impl_->wanted = false; impl_->Release(); impl_->wanted = wanted;
    return Refresh();
}

} // namespace aether::app
