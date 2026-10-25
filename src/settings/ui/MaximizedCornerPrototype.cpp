// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/ui/MaximizedCornerPrototype.h"

#include "platform/windows/Handles.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aether::settings::ui::experimental {
namespace {
constexpr wchar_t kIdentityProperty[] = L"AetherShell.Experimental.MaximizedCornerIdentity";
constexpr std::int64_t kMaxRegionCoordinate = (std::int64_t{1} << 26) - 1;
struct ResetBusy {
    bool& busy;
    ~ResetBusy() { busy = false; }
};
bool ValidBounds(const RECT& bounds) noexcept {
    const auto width = static_cast<std::int64_t>(bounds.right) - bounds.left;
    const auto height = static_cast<std::int64_t>(bounds.bottom) - bounds.top;
    return width > 0 && height > 0 && width <= kMaxRegionCoordinate &&
        height <= kMaxRegionCoordinate;
}
Error NativeError(const char* message) {
    return MakeError(ErrorCode::Platform, message, ::GetLastError());
}
platform::UniqueRegion CopyRegion(HRGN source) noexcept {
    platform::UniqueRegion copy(::CreateRectRgn(0, 0, 0, 0));
    if (copy && ::CombineRgn(copy.Get(), source, nullptr, RGN_COPY) == ERROR) copy.Reset();
    return copy;
}
}  // namespace

Result<MaximizedCornerLayout> ComputeMaximizedCornerLayout(const RECT& nativeBounds,
    const RECT& visibleFrame, const RECT& workArea, UINT dpi, float radiusDip) try {
    if (!ValidBounds(nativeBounds) || !ValidBounds(visibleFrame) || !ValidBounds(workArea) ||
        dpi < 48 || dpi > 768 || !std::isfinite(radiusDip) || radiusDip <= 0 || radiusDip > 100)
        return MakeError(ErrorCode::InvalidArgument, "Invalid maximized corner geometry");
    const RECT visible{
        std::max({nativeBounds.left, visibleFrame.left, workArea.left}),
        std::max({nativeBounds.top, visibleFrame.top, workArea.top}),
        std::min({nativeBounds.right, visibleFrame.right, workArea.right}),
        std::min({nativeBounds.bottom, visibleFrame.bottom, workArea.bottom})};
    if (!ValidBounds(visible))
        return MakeError(ErrorCode::Unavailable, "Maximized corner clip is empty");
    const auto width = static_cast<std::int64_t>(visible.right) - visible.left;
    const auto height = static_cast<std::int64_t>(visible.bottom) - visible.top;
    const auto radius = static_cast<int>(std::lround(static_cast<double>(radiusDip) * dpi / 96.0));
    const auto clamped = std::min<std::int64_t>({radius, width / 2, height / 2});
    if (clamped < 1)
        return MakeError(ErrorCode::Unavailable, "Maximized corner clip is too small");
    MaximizedCornerLayout result;
    // Each translated coordinate is bounded by the validated native width/height.
    result.clipPx = {
        static_cast<LONG>(static_cast<std::int64_t>(visible.left) - nativeBounds.left),
        static_cast<LONG>(static_cast<std::int64_t>(visible.top) - nativeBounds.top),
        static_cast<LONG>(static_cast<std::int64_t>(visible.right) - nativeBounds.left),
        static_cast<LONG>(static_cast<std::int64_t>(visible.bottom) - nativeBounds.top)};
    result.radiusPx = static_cast<int>(clamped);
    return result;
} catch (...) {
    return MakeError(ErrorCode::Internal, "Maximized corner geometry could not be computed");
}

MaximizedCornerPrototype::MaximizedCornerPrototype(HWND window, DWORD thread, HRGN original,
    bool hadRegion) noexcept : window_(window), thread_(thread), original_(original), hadRegion_(hadRegion) {}

Result<std::unique_ptr<MaximizedCornerPrototype>> MaximizedCornerPrototype::Attach(HWND window) try {
    DWORD process = 0;
    const auto thread = ::GetWindowThreadProcessId(window, &process);
    if (!thread || thread != ::GetCurrentThreadId() || process != ::GetCurrentProcessId() ||
        ::GetAncestor(window, GA_ROOT) != window)
        return MakeError(ErrorCode::AccessDenied, "Corner prototype requires an owned top-level UI-thread window");
    const auto style = ::GetWindowLongPtrW(window, GWL_STYLE);
    const auto extended = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_CAPTION) != WS_CAPTION || !(style & WS_THICKFRAME) || (extended & WS_EX_LAYOUTRTL))
        return MakeError(ErrorCode::Unsupported, "Corner prototype requires a standard left-to-right resizable frame");
    if (::GetPropW(window, kIdentityProperty))
        return MakeError(ErrorCode::AlreadyExists, "Corner prototype is already attached");
    platform::UniqueRegion original(::CreateRectRgn(0, 0, 0, 0));
    if (!original) return NativeError("Corner prototype snapshot allocation failed");
    const bool hadRegion = ::GetWindowRgn(window, original.Get()) != ERROR;
    auto instance = std::unique_ptr<MaximizedCornerPrototype>(
        new MaximizedCornerPrototype(window, thread, original.Get(), hadRegion));
    (void)original.Release();
    if (!::SetPropW(window, kIdentityProperty, instance.get()))
        return NativeError("Corner prototype identity registration failed");
    return instance;
} catch (...) {
    return MakeError(ErrorCode::Internal, "Corner prototype could not be attached");
}

MaximizedCornerPrototype::~MaximizedCornerPrototype() {
    try { (void)Restore(); } catch (...) { /* Destruction cannot cross a module boundary. */ }
    if (::GetCurrentThreadId() == thread_ && ::GetPropW(window_, kIdentityProperty) == this)
        (void)::RemovePropW(window_, kIdentityProperty);
    if (installed_) ::DeleteObject(installed_);
    if (original_) ::DeleteObject(original_);
}

Result<void> MaximizedCornerPrototype::CheckIdentity() const {
    DWORD process = 0;
    const auto thread = ::GetWindowThreadProcessId(window_, &process);
    if (::GetCurrentThreadId() != thread_ || thread != thread_ || process != ::GetCurrentProcessId() ||
        ::GetPropW(window_, kIdentityProperty) != this)
        return MakeError(ErrorCode::InvalidState, "Corner prototype window identity is no longer owned");
    return Ok();
}

Result<bool> MaximizedCornerPrototype::MatchesCurrent(HRGN expected, bool present) const {
    platform::UniqueRegion current(::CreateRectRgn(0, 0, 0, 0));
    if (!current) return NativeError("Corner prototype current region allocation failed");
    const bool hasCurrent = ::GetWindowRgn(window_, current.Get()) != ERROR;
    if (present != hasCurrent) return false;
    return !present || ::EqualRgn(current.Get(), expected) != FALSE;
}

Result<MaximizedCornerLayout> MaximizedCornerPrototype::Apply(const RECT& visibleFrame,
    const RECT& workArea, UINT dpi, float radiusDip) try {
    if (::GetCurrentThreadId() != thread_)
        return MakeError(ErrorCode::AccessDenied, "Corner prototype update requires its owning thread");
    if (busy_) return MakeError(ErrorCode::InvalidState, "Corner prototype update is already in progress");
    busy_ = true; ResetBusy reset{busy_};
    if (auto identity = CheckIdentity(); !identity) return identity.error();
    if (!::IsZoomed(window_) || ::IsIconic(window_))
        return MakeError(ErrorCode::Unsupported, "Corner prototype requires a true maximized window");
    auto stillOwned = MatchesCurrent(installed_ ? installed_ : original_, installed_ || hadRegion_);
    if (!stillOwned) return stillOwned.error();
    if (!*stillOwned)
        return MakeError(ErrorCode::InvalidState, "Corner prototype region was replaced");
    RECT nativeBounds{};
    if (!::GetWindowRect(window_, &nativeBounds)) return NativeError("Corner prototype bounds unavailable");
    auto layout = ComputeMaximizedCornerLayout(nativeBounds, visibleFrame, workArea, dpi, radiusDip);
    if (!layout) return layout.error();
    const auto& clip = layout->clipPx;
    platform::UniqueRegion candidate(::CreateRoundRectRgn(clip.left, clip.top, clip.right, clip.bottom,
        layout->radiusPx * 2, layout->radiusPx * 2));
    if (!candidate) return NativeError("Corner prototype rounded region allocation failed");
    // Never expand a caller's original region; the experiment only clips it further.
    if (hadRegion_ && ::CombineRgn(candidate.Get(), candidate.Get(), original_, RGN_AND) == ERROR)
        return NativeError("Corner prototype original region intersection failed");
    RECT regionBounds{};
    const int regionKind = ::GetRgnBox(candidate.Get(), &regionBounds);
    if (regionKind == ERROR) return NativeError("Corner prototype rounded region bounds unavailable");
    if (regionKind == NULLREGION)
        return MakeError(ErrorCode::Unavailable, "Corner prototype original region excludes the frame");
    if (installed_ && ::EqualRgn(installed_, candidate.Get())) return layout;
    auto retained = CopyRegion(candidate.Get());
    if (!retained) return NativeError("Corner prototype retained region allocation failed");
    if (!::SetWindowRgn(window_, candidate.Get(), FALSE)) return NativeError("Corner prototype region application failed");
    (void)candidate.Release();  // USER32 owns only successfully installed regions.
    if (installed_) ::DeleteObject(installed_);
    installed_ = retained.Release();
    if (auto identity = CheckIdentity(); !identity) return identity.error();
    return layout;
} catch (...) {
    return MakeError(ErrorCode::Internal, "Corner prototype could not apply its clip");
}

Result<void> MaximizedCornerPrototype::Restore() try {
    if (::GetCurrentThreadId() != thread_)
        return MakeError(ErrorCode::AccessDenied, "Corner prototype restore requires its owning thread");
    if (busy_) return MakeError(ErrorCode::InvalidState, "Corner prototype update is already in progress");
    busy_ = true; ResetBusy reset{busy_};
    if (auto identity = CheckIdentity(); !identity) return identity;
    if (!installed_) return Ok();
    auto stillOwned = MatchesCurrent(installed_, true);
    if (!stillOwned) return stillOwned.error();
    if (!*stillOwned) return MakeError(ErrorCode::InvalidState, "Corner prototype replaced region was preserved");
    platform::UniqueRegion restored;
    if (hadRegion_) {
        restored = CopyRegion(original_);
        if (!restored) return NativeError("Corner prototype restore allocation failed");
    }
    if (!::SetWindowRgn(window_, restored.Get(), FALSE)) return NativeError("Corner prototype restore failed");
    (void)restored.Release();
    ::DeleteObject(installed_); installed_ = nullptr;
    return Ok();
} catch (...) {
    return MakeError(ErrorCode::Internal, "Corner prototype could not restore its prior clip");
}

}  // namespace aether::settings::ui::experimental
