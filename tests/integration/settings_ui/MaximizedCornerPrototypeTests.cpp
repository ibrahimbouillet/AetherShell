// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "platform/windows/Handles.h"
#include "platform/windows/WindowUtilities.h"
#include "settings/ui/MaximizedCornerPrototype.h"

#include <array>
#include <limits>
#include <thread>

using namespace aether;
using namespace aether::settings::ui::experimental;

namespace {
struct HiddenOwnedWindow {
    HWND handle = nullptr;
    MaximizedCornerPrototype* reentryPrototype = nullptr;
    RECT reentryBounds{};
    unsigned reentryAttempts = 0;
    bool allReentryDenied = true;
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* instance = reinterpret_cast<HiddenOwnedWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            instance = static_cast<HiddenOwnedWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
        }
        if (instance && instance->reentryPrototype &&
            (message == WM_WINDOWPOSCHANGING || message == WM_WINDOWPOSCHANGED)) {
            ++instance->reentryAttempts;
            auto apply = instance->reentryPrototype->Apply(instance->reentryBounds, instance->reentryBounds, 96);
            auto restore = instance->reentryPrototype->Restore();
            instance->allReentryDenied = instance->allReentryDenied && !apply && !restore &&
                apply.error().code == ErrorCode::InvalidState && restore.error().code == ErrorCode::InvalidState;
        }
        return ::DefWindowProcW(window, message, wParam, lParam);
    }
    ~HiddenOwnedWindow() { reentryPrototype = nullptr; if (handle) ::DestroyWindow(handle); }
    void Create(bool maximized = true) {
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpfnWndProc = &Proc;
        type.lpszClassName = L"AetherShell.Test.MaximizedCorner";
        REQUIRE(platform::RegisterWindowClassOnce(type).ok());
        // Genuine initial maximize state, never WS_VISIBLE, ShowWindow, foreground or real AppBar.
        handle = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName,
            L"Aether corner experiment", WS_OVERLAPPEDWINDOW | (maximized ? WS_MAXIMIZE : 0),
            0, 0, 160, 160, nullptr, nullptr, type.hInstance, this);
        REQUIRE(handle);
        REQUIRE_FALSE(::IsWindowVisible(handle));
        REQUIRE_EQ(::IsZoomed(handle) != FALSE, maximized);
    }
};
RECT NativeBounds(HWND window) {
    RECT bounds{}; REQUIRE(::GetWindowRect(window, &bounds)); return bounds;
}
RECT Inset(const RECT& bounds, LONG amount) {
    return {bounds.left + amount, bounds.top + amount, bounds.right - amount, bounds.bottom - amount};
}
void CheckPlacementUnchanged(HWND window, const RECT& bounds, const WINDOWPLACEMENT& before) {
    const auto afterBounds = NativeBounds(window);
    CHECK(::EqualRect(&bounds, &afterBounds));
    WINDOWPLACEMENT after{sizeof(after)}; REQUIRE(::GetWindowPlacement(window, &after));
    CHECK_EQ(after.showCmd, before.showCmd); CHECK_EQ(after.flags, before.flags);
    CHECK(::EqualRect(&before.rcNormalPosition, &after.rcNormalPosition));
    CHECK_EQ(after.ptMinPosition.x, before.ptMinPosition.x); CHECK_EQ(after.ptMinPosition.y, before.ptMinPosition.y);
    CHECK_EQ(after.ptMaxPosition.x, before.ptMaxPosition.x); CHECK_EQ(after.ptMaxPosition.y, before.ptMaxPosition.y);
    CHECK(::IsZoomed(window)); CHECK_FALSE(::IsWindowVisible(window));
}
}  // namespace

AETHER_TEST("corner prototype: visible maximize clip uses physical frame and work bounds at each DPI") {
    const RECT native{-1928, -1088, 8, 8};
    const RECT frame{-1920, -1080, 0, 0};
    const RECT work{-1908, -1068, -12, -12};
    for (const auto& [dpi, radius] : std::array<std::pair<UINT, int>, 3>{{{96, 12}, {144, 18}, {192, 24}}}) {
        const auto layout = ComputeMaximizedCornerLayout(native, frame, work, dpi);
        REQUIRE(layout.ok());
        CHECK_EQ(layout->clipPx.left, LONG{20}); CHECK_EQ(layout->clipPx.top, LONG{20});
        CHECK_EQ(layout->clipPx.right, LONG{1916}); CHECK_EQ(layout->clipPx.bottom, LONG{1076});
        CHECK_EQ(layout->radiusPx, radius);
    }
    const RECT small{0, 0, 9, 13};
    auto clamped = ComputeMaximizedCornerLayout(small, small, small, 192);
    REQUIRE(clamped.ok()); CHECK_EQ(clamped->radiusPx, 4);
}

AETHER_TEST("corner prototype: invalid and GDI-overflow geometry stays unsupported without mutation") {
    const RECT normal{0, 0, 100, 100}, empty{20, 20, 20, 30}, outside{200, 200, 300, 300};
    CHECK_FALSE(ComputeMaximizedCornerLayout(empty, normal, normal, 96));
    CHECK_FALSE(ComputeMaximizedCornerLayout(normal, empty, normal, 96));
    CHECK_FALSE(ComputeMaximizedCornerLayout(normal, normal, outside, 96));
    for (const auto dpi : {0u, 47u, 769u})
        CHECK_FALSE(ComputeMaximizedCornerLayout(normal, normal, normal, dpi));
    for (const auto radius : {0.0f, -1.0f, 101.0f, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()})
        CHECK_FALSE(ComputeMaximizedCornerLayout(normal, normal, normal, 96, radius));
    const RECT huge{std::numeric_limits<LONG>::min(), 0, std::numeric_limits<LONG>::max(), 100};
    CHECK_FALSE(ComputeMaximizedCornerLayout(huge, normal, normal, 96));
    const RECT gdiTooWide{0, 0, LONG{1} << 26, 100};
    CHECK_FALSE(ComputeMaximizedCornerLayout(gdiTooWide, gdiTooWide, gdiTooWide, 96));
    const RECT sliver{0, 0, 1, 20};
    CHECK_FALSE(ComputeMaximizedCornerLayout(sliver, sliver, sliver, 96));
    const LONG minimum = std::numeric_limits<LONG>::min();
    const RECT negative{minimum, minimum, minimum + 100, minimum + 100};
    auto valid = ComputeMaximizedCornerLayout(negative, negative, negative, 96);
    REQUIRE(valid.ok()); CHECK_EQ(valid->clipPx.right, LONG{100});
}

AETHER_TEST("corner prototype: hidden true-maximized owned window receives rounded input without placement change") {
    HiddenOwnedWindow window; window.Create();
    const auto native = NativeBounds(window.handle);
    WINDOWPLACEMENT before{sizeof(before)}; REQUIRE(::GetWindowPlacement(window.handle, &before));
    auto prototype = MaximizedCornerPrototype::Attach(window.handle); REQUIRE(prototype.ok());
    CHECK_FALSE(MaximizedCornerPrototype::Attach(window.handle));
    // Synthetic observations isolate region ownership/geometry. Hidden windows cannot establish
    // actual DWM composition quality, real monitor clipping or visible caption-button acceptance.
    const auto frame = Inset(native, 8), work = Inset(native, 20);
    auto applied = (*prototype)->Apply(frame, work, 144); REQUIRE(applied.ok());
    platform::UniqueRegion region(::CreateRectRgn(0, 0, 0, 0)); REQUIRE(region);
    REQUIRE(::GetWindowRgn(window.handle, region.Get()) != ERROR);
    const auto& clip = applied->clipPx;
    CHECK_FALSE(::PtInRegion(region.Get(), clip.left, clip.top));
    CHECK_FALSE(::PtInRegion(region.Get(), clip.right - 1, clip.top));
    CHECK_FALSE(::PtInRegion(region.Get(), clip.left, clip.bottom - 1));
    CHECK_FALSE(::PtInRegion(region.Get(), clip.right - 1, clip.bottom - 1));
    CHECK(::PtInRegion(region.Get(), (clip.left + clip.right) / 2, clip.top));
    CHECK(::PtInRegion(region.Get(), (clip.left + clip.right) / 2, (clip.top + clip.bottom) / 2));
    CHECK_FALSE(::PtInRegion(region.Get(), clip.left - 1, (clip.top + clip.bottom) / 2));
    REQUIRE((*prototype)->Apply(frame, work, 144).ok());
    CheckPlacementUnchanged(window.handle, native, before);
    REQUIRE((*prototype)->Restore().ok());
    CHECK_EQ(::GetWindowRgn(window.handle, region.Get()), ERROR);
    CheckPlacementUnchanged(window.handle, native, before);
}

AETHER_TEST("corner prototype: nonrectangular original region is intersected and exactly restored") {
    HiddenOwnedWindow window; window.Create();
    const auto native = NativeBounds(window.handle);
    const int width = native.right - native.left, height = native.bottom - native.top;
    platform::UniqueRegion original(::CreateEllipticRgn(0, 0, width, height)); REQUIRE(original);
    platform::UniqueRegion handed(::CreateRectRgn(0, 0, 0, 0)); REQUIRE(handed);
    REQUIRE(::CombineRgn(handed.Get(), original.Get(), nullptr, RGN_COPY) != ERROR);
    REQUIRE(::SetWindowRgn(window.handle, handed.Get(), FALSE)); (void)handed.Release();
    auto prototype = MaximizedCornerPrototype::Attach(window.handle); REQUIRE(prototype.ok());
    const auto observed = Inset(native, 8);
    REQUIRE((*prototype)->Apply(observed, observed, 96).ok());
    platform::UniqueRegion current(::CreateRectRgn(0, 0, 0, 0)), expanded(::CreateRectRgn(0, 0, 0, 0));
    REQUIRE(current); REQUIRE(expanded); REQUIRE(::GetWindowRgn(window.handle, current.Get()) != ERROR);
    CHECK_EQ(::CombineRgn(expanded.Get(), current.Get(), original.Get(), RGN_DIFF), NULLREGION);
    REQUIRE((*prototype)->Restore().ok());
    REQUIRE(::GetWindowRgn(window.handle, current.Get()) != ERROR);
    CHECK(::EqualRgn(current.Get(), original.Get()));
}

AETHER_TEST("corner prototype: synchronous position messages cannot recursively apply or restore") {
    HiddenOwnedWindow window; window.Create();
    const auto native = NativeBounds(window.handle), observed = Inset(native, 8);
    auto prototype = MaximizedCornerPrototype::Attach(window.handle); REQUIRE(prototype.ok());
    window.reentryPrototype = prototype->get(); window.reentryBounds = observed;
    REQUIRE((*prototype)->Apply(observed, observed, 96).ok());
    REQUIRE(window.reentryAttempts > 0); CHECK(window.allReentryDenied);
    const unsigned afterApply = window.reentryAttempts;
    REQUIRE((*prototype)->Restore().ok());
    CHECK(window.reentryAttempts > afterApply); CHECK(window.allReentryDenied);
    window.reentryPrototype = nullptr;
    CHECK(::IsZoomed(window.handle)); CHECK_FALSE(::IsWindowVisible(window.handle));
}

AETHER_TEST("corner prototype: a replacement region survives conditional restore and destructor") {
    HiddenOwnedWindow window; window.Create();
    const auto observed = Inset(NativeBounds(window.handle), 8);
    platform::UniqueRegion replacement(::CreateRectRgn(10, 10, 60, 60)); REQUIRE(replacement);
    {
        auto prototype = MaximizedCornerPrototype::Attach(window.handle); REQUIRE(prototype.ok());
        REQUIRE((*prototype)->Apply(observed, observed, 96).ok());
        platform::UniqueRegion handed(::CreateRectRgn(10, 10, 60, 60)); REQUIRE(handed);
        REQUIRE(::SetWindowRgn(window.handle, handed.Get(), FALSE)); (void)handed.Release();
        CHECK_FALSE((*prototype)->Apply(observed, observed, 96));
        CHECK_FALSE((*prototype)->Restore());
    }
    platform::UniqueRegion current(::CreateRectRgn(0, 0, 0, 0)); REQUIRE(current);
    REQUIRE(::GetWindowRgn(window.handle, current.Get()) != ERROR);
    CHECK(::EqualRgn(current.Get(), replacement.Get()));
    // The previous instance's identity property was cleaned, despite its preserved replacement.
    CHECK(MaximizedCornerPrototype::Attach(window.handle).ok());
}

AETHER_TEST("corner prototype: normal and wrong-thread windows are rejected and destroyed identity is not reused") {
    HiddenOwnedWindow window; window.Create(false);
    const auto observed = NativeBounds(window.handle);
    auto prototype = MaximizedCornerPrototype::Attach(window.handle); REQUIRE(prototype.ok());
    CHECK_FALSE((*prototype)->Apply(observed, observed, 96));
    bool denied = false;
    std::thread other([&] { denied = !MaximizedCornerPrototype::Attach(window.handle).ok(); });
    other.join(); CHECK(denied);
    REQUIRE(::DestroyWindow(window.handle)); window.handle = nullptr;
    CHECK_FALSE((*prototype)->Restore());
    CHECK_FALSE((*prototype)->Apply(observed, observed, 96));
    CHECK_FALSE(MaximizedCornerPrototype::Attach(nullptr));
}
