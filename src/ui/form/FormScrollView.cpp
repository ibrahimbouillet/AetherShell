// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/form/FormScrollView.h"

#include "diagnostics/Logger.h"
#include "graphics/FrameScheduler.h"
#include "platform/windows/WindowUtilities.h"
#include "ui/form/FormInternal.h"
#include "ui/form/FormPaint.h"

#include <uiautomation.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <new>
#include <optional>

namespace aether::ui::form {
namespace {

constexpr wchar_t kViewportClass[] = L"AetherShell.FormScroll.Viewport";
constexpr wchar_t kCanvasClass[] = L"AetherShell.FormScroll.Canvas";
constexpr wchar_t kIndicatorClass[] = L"AetherShell.FormScroll.Indicator";
constexpr UINT_PTR kAnimationTimer = 0x5343524Cu;  // "SCRL"
constexpr float kWheelLineDip = 16.0f;              // one wheel "line"
constexpr float kLineDip = 48.0f;                   // SB_LINEUP / SB_LINEDOWN and UIA small steps
constexpr float kMinThumbDip = 24.0f;
constexpr float kRevealMarginDip = 12.0f;

template <class F>
HRESULT Guard(F&& function) noexcept {
    try {
        return function();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

// State shared by the view and its UI Automation provider. The provider outlives the view when a
// client still holds it; the callbacks are only used while `connected` is set, on the owner thread.
struct ScrollAccess {
    HWND hwnd = nullptr;
    DWORD thread = 0;
    std::atomic<bool> connected{true};
    std::function<double()> percent;
    std::function<double()> viewSize;
    std::function<bool()> scrollable;
    std::function<void(ScrollAmount)> scrollBy;
    std::function<void(double)> scrollTo;
};

// Standard STA COM object (no free-threaded marshaler): with ProviderOptions_UseComThreading UIA
// calls it on the viewport's thread.
class ScrollProvider final : public IRawElementProviderSimple, public IScrollProvider {
public:
    explicit ScrollProvider(std::shared_ptr<ScrollAccess> access) : access_(std::move(access)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple)) {
            *object = static_cast<IRawElementProviderSimple*>(this);
        } else if (iid == __uuidof(IScrollProvider)) {
            *object = static_cast<IScrollProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG remaining = --refs_;
        if (remaining == 0) delete this;
        return remaining;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) noexcept override {
        if (!options) return E_POINTER;
        *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (const HRESULT hr = Check(); FAILED(hr)) return hr;
        if (pattern == UIA_ScrollPatternId) {
            *result = static_cast<IScrollProvider*>(this);
            AddRef();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* value) noexcept override {
        if (!value) return E_POINTER;
        ::VariantInit(value);
        if (const HRESULT hr = Check(); FAILED(hr)) return hr;
        if (property == UIA_IsScrollPatternAvailablePropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_TRUE;
        }
        return S_OK;  // everything else comes from the merged native HWND provider
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) noexcept override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (const HRESULT hr = Check(); FAILED(hr)) return hr;
        return ::UiaHostProviderFromHwnd(access_->hwnd, result);
    }

    // IScrollProvider
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) noexcept override {
        return Guard([&]() -> HRESULT {
            if (const HRESULT hr = Check(); FAILED(hr)) return hr;
            if (horizontal != ScrollAmount_NoAmount) return UIA_E_INVALIDOPERATION;
            if (vertical < ScrollAmount_LargeDecrement || vertical > ScrollAmount_SmallIncrement) return E_INVALIDARG;
            if (vertical != ScrollAmount_NoAmount && !access_->scrollable()) return UIA_E_INVALIDOPERATION;
            access_->scrollBy(vertical);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) noexcept override {
        return Guard([&]() -> HRESULT {
            if (const HRESULT hr = Check(); FAILED(hr)) return hr;
            if (horizontal != UIA_ScrollPatternNoScroll) return UIA_E_INVALIDOPERATION;
            if (vertical == UIA_ScrollPatternNoScroll) return S_OK;
            if (!std::isfinite(vertical) || vertical < 0.0 || vertical > 100.0) return E_INVALIDARG;
            if (!access_->scrollable()) return UIA_E_INVALIDOPERATION;
            access_->scrollTo(vertical);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* value) noexcept override {
        if (!value) return E_POINTER;
        *value = UIA_ScrollPatternNoScroll;
        return Check();
    }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* value) noexcept override {
        if (!value) return E_POINTER;
        *value = UIA_ScrollPatternNoScroll;
        if (const HRESULT hr = Check(); FAILED(hr)) return hr;
        *value = access_->percent();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 100.0;
        return Check();
    }
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 100.0;
        if (const HRESULT hr = Check(); FAILED(hr)) return hr;
        *value = access_->viewSize();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* value) noexcept override {
        if (!value) return E_POINTER;
        *value = FALSE;
        return Check();
    }
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* value) noexcept override {
        if (!value) return E_POINTER;
        *value = FALSE;
        if (const HRESULT hr = Check(); FAILED(hr)) return hr;
        *value = access_->scrollable() ? TRUE : FALSE;
        return S_OK;
    }

private:
    HRESULT Check() const noexcept {
        if (!access_->connected.load()) return UIA_E_ELEMENTNOTAVAILABLE;
        return access_->thread == ::GetCurrentThreadId() ? S_OK : RPC_E_WRONG_THREAD;
    }

    std::shared_ptr<ScrollAccess> access_;
    std::atomic<ULONG> refs_{1};
};

}  // namespace

struct FormScrollView::Impl final : graphics::IFrameClient {
    FormScrollView* owner = nullptr;
    FormSurface* surface = nullptr;
    HWND viewport = nullptr;
    HWND canvas = nullptr;
    HWND indicator = nullptr;
    RECT frame{};
    int content = 0;
    int position = 0;
    int wheelRemainder = 0;
    // Animated scrolling follows a critically damped spring: new wheel notches retarget it without
    // resetting its velocity, a reversal decelerates through zero instead of jumping, and it never
    // overshoots the content.
    graphics::Spring spring;
    double lastStep = 0.0;
    // Opaque layered editors are masked while the canvas moves (FormSurface::MaskEditors): the frame
    // after masking holds still, motion follows, and the editors are revealed once it has settled.
    enum class Masking { None, Imaging, Holding, Moving, Revealing };
    Masking masking = Masking::None;
    std::optional<int> deferred;  // an immediate position waiting for the held frame
    bool movedSinceTick = false;  // the canvas moved since the previous frame tick
    graphics::Tween thumbHover;
    bool scrolling = false;
    bool hovered = false;
    bool dragging = false;
    int dragOffset = 0;
    bool frameRequested = false;
    bool timerRunning = false;
    std::shared_ptr<int> life = std::make_shared<int>(0);
    std::shared_ptr<ScrollAccess> access;
    winrt::com_ptr<IRawElementProviderSimple> provider;

    ~Impl() override { DestroyWindows(); }

    [[nodiscard]] const graphics::DesignTokens& Tokens() const noexcept { return surface->Tokens(); }
    [[nodiscard]] int Px(float dip) const noexcept { return static_cast<int>(std::lround(surface->ToPx(dip))); }
    [[nodiscard]] int Page() const noexcept {
        if (!viewport) return 0;
        RECT client{};
        ::GetClientRect(viewport, &client);
        return client.bottom - client.top;
    }
    [[nodiscard]] int Range() const noexcept { return std::max(0, content - Page()); }
    [[nodiscard]] int Gutter() const noexcept {
        if (!surface) return 0;
        const auto& f = Tokens().form;
        return Px(f.scrollbarHoverWidth + 2.0f * f.scrollbarInset);
    }
    [[nodiscard]] double Now() const noexcept { return graphics::FrameScheduler::Now(); }
    // The position the view is heading to (the animation target while one runs).
    [[nodiscard]] int Goal() const noexcept {
        return scrolling ? static_cast<int>(std::lround(spring.Target())) : position;
    }
    [[nodiscard]] graphics::Spring::Parameters SpringParameters() const noexcept {
        // About 0.2 s to settle a notch at normal speed (form.scroll is 0.16 s), in pixels.
        auto parameters = graphics::SpringFromDampingRatio(720.0f, 1.0f);
        parameters.restDelta = 0.35f;
        parameters.restVelocity = 6.0f;
        return surface ? surface->Motion().ScaleSpring(parameters) : parameters;
    }

    // --- layout and position ---
    void Layout() {
        if (!viewport) return;
        RECT client{};
        ::GetClientRect(viewport, &client);
        const int width = client.right;
        const int height = std::max<int>(client.bottom, content);
        position = std::clamp(position, 0, Range());
        if (scrolling && spring.Target() > static_cast<float>(Range())) {
            scrolling = false;
            spring.SnapTo(static_cast<float>(position));
        }
        ::SetWindowPos(canvas, nullptr, 0, -position, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        const int gutter = Gutter();
        ::SetWindowPos(indicator, HWND_TOP, width - gutter, 0, gutter, client.bottom,
                       SWP_NOACTIVATE | (Range() > 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        ::InvalidateRect(indicator, nullptr, FALSE);
    }

    // True when the canvas may move now. The first movement next to visible editors only masks
    // them and presents the unmoved frame; movement starts on the next frame.
    bool ReadyToMove() {
        switch (masking) {
            case Masking::Moving:
                return true;
            case Masking::Revealing:
                masking = Masking::Moving;  // still masked: continue without another hold
                return true;
            case Masking::Imaging:
            case Masking::Holding:
                return false;
            case Masking::None:
                break;
        }
        if (!surface->HasVisibleEditors(viewport)) return true;
        if (!surface->MaskEditors(viewport)) {
            // Stale images were refreshed: show them under the live editors before hiding them.
            (void)surface->Present(viewport);
            masking = Masking::Imaging;
        } else {
            if (!surface->EditorsMasked(viewport)) return true;
            masking = Masking::Holding;
        }
        RequestFrame();
        return false;
    }

    void EndMasking() noexcept {
        if (masking == Masking::None) return;
        masking = Masking::None;
        deferred.reset();
        if (surface) surface->UnmaskEditors(viewport);
    }

    // `continuous`: part of a motion (spring, drag, touchpad) - masked next to editors. An isolated jump
    // (keyboard, UI Automation, focus reveal) lands coherently in one composition and moves at once.
    void Apply(int next, bool continuous = true) {
        next = std::clamp(next, 0, Range());
        if (next == position || !viewport) return;
        if (masking == Masking::Imaging || masking == Masking::Holding) {
            deferred = next;
            return;
        }
        if ((continuous || masking != Masking::None) && !ReadyToMove()) {
            deferred = next;
            return;
        }
        ApplyNow(next);
        if (masking != Masking::None) RequestFrame();  // reveal once the motion has settled
    }

    void ApplyNow(int next) {
        next = std::clamp(next, 0, Range());
        if (next == position || !viewport) return;
        movedSinceTick = true;
        const double before = VerticalPercent();
        const int previous = position;
        position = next;
        RECT client{};
        ::GetClientRect(viewport, &client);
        // The whole visible scene - material, cards and every control at the new offset - is
        // rendered first and presented as one frame right behind the move. Repainting child by child
        // would show controls at old and new positions together (and stale pixels) until the last
        // one painted. The grain stays anchored to the form because every visible pixel comes from
        // the scene.
        const auto move = [&] {
            ::SetWindowPos(canvas, nullptr, 0, -position, client.right, std::max<int>(client.bottom, content),
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);
        };
        if (!surface->PresentMove(viewport, canvas, previous - next, move)) {
            move();
            ::RedrawWindow(viewport, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
        RaiseScrolled(before);
        if (owner && owner->scrolled) {
            const std::weak_ptr<int> alive = life;
            try {
                auto callback = owner->scrolled;
                callback();
            } catch (...) {
                AETHER_LOG_ERROR("ui_form", "scroll callback threw");
            }
            if (alive.expired()) return;
        }
    }

    void RaiseScrolled(double before) noexcept {
        if (!provider || !::UiaClientsAreListening()) return;
        VARIANT oldValue{};
        VARIANT newValue{};
        oldValue.vt = VT_R8;
        oldValue.dblVal = before;
        newValue.vt = VT_R8;
        newValue.dblVal = VerticalPercent();
        (void)::UiaRaiseAutomationPropertyChangedEvent(provider.get(), UIA_ScrollVerticalScrollPercentPropertyId, oldValue, newValue);
    }

    void ScrollTo(int target, bool animate) {
        if (!viewport || !surface) return;
        target = std::clamp(target, 0, Range());
        const auto& motion = surface->Motion();
        const int from = scrolling ? static_cast<int>(std::lround(spring.Value())) : position;
        // Short animated moves only: long jumps (Home/End, scroll bar drags) stay immediate.
        if (animate && motion.AllowsMovement() && motion.Scale(Tokens().form.scroll) > 0.0 &&
            std::abs(target - from) <= 3 * std::max(1, Page())) {
            if (!scrolling) {
                spring.SnapTo(static_cast<float>(position));
                lastStep = Now();
                if (target != position) (void)ReadyToMove();  // masks at input time when editors are visible
            }
            spring.SetParameters(SpringParameters());
            spring.SetTarget(static_cast<float>(target));
            scrolling = true;
            RequestFrame();
            return;
        }
        scrolling = false;
        spring.SnapTo(static_cast<float>(target));
        Apply(target, false);
    }

    // Continuous input (precision touchpads, high-resolution wheels) already arrives smoothly: it
    // moves the view at once, carrying any notch animation in flight along with it.
    void ScrollImmediately(int delta) {
        if (!scrolling) {
            Apply(position + delta);
            return;
        }
        spring.ShiftBy(static_cast<float>(delta));
        const float range = static_cast<float>(Range());
        if (spring.Target() < 0.0f || spring.Target() > range) spring.SetTarget(std::clamp(spring.Target(), 0.0f, range));
        Apply(static_cast<int>(std::lround(std::clamp(spring.Value(), 0.0f, range))));
    }

    // --- animation ---
    void RequestFrame() {
        if (auto* scheduler = surface ? surface->Scheduler() : nullptr) {
            if (!frameRequested) {
                frameRequested = true;
                scheduler->RequestFrame(this);
            }
        } else if (!timerRunning && viewport) {
            timerRunning = ::SetTimer(viewport, kAnimationTimer, 16, nullptr) != 0;
        }
    }

    void StopTimer() noexcept {
        if (timerRunning && viewport) ::KillTimer(viewport, kAnimationTimer);
        timerRunning = false;
    }

    void OnFrame(double now) override {
        frameRequested = false;
        try {
            Tick(now);
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "scroll animation frame failed");
        }
    }

    void Tick(double now) {
        const std::weak_ptr<int> alive = life;
        bool again = false;
        const bool wasMoving = masking == Masking::Moving;
        const bool movedBefore = movedSinceTick;  // immediate steps between frames (drag, touchpad)
        movedSinceTick = false;
        if (masking == Masking::Imaging) {
            // The refreshed images are on screen under the live editors: hide the editors now.
            if (surface->MaskEditors(viewport)) {
                masking = surface->EditorsMasked(viewport) ? Masking::Holding : Masking::None;
                if (masking == Masking::None && deferred) {
                    const int target = *deferred;
                    deferred.reset();
                    ApplyNow(target);
                    if (alive.expired()) return;
                }
            }
            lastStep = now;
            again = true;
        } else if (masking == Masking::Holding) {
            // The masked, unmoved frame has been on screen for a frame: movement may start.
            masking = Masking::Moving;
            lastStep = now;
            if (deferred) {
                const int target = *deferred;
                deferred.reset();
                ApplyNow(target);
                if (alive.expired()) return;
            }
            again = true;
        } else if (scrolling) {
            spring.Step(std::clamp(now - lastStep, 0.0, 0.05));
            lastStep = now;
            const bool running = !spring.IsSettled();
            if (!running) spring.SnapTo(spring.Target());
            Apply(static_cast<int>(std::lround(spring.Value())));
            if (alive.expired()) return;
            scrolling = running;
            again = running;
        }
        const bool moved = movedBefore || movedSinceTick;
        movedSinceTick = false;
        if (masking == Masking::Revealing && !moved) {
            EndMasking();
        } else if (masking == Masking::Moving && wasMoving && !moved && !scrolling && !dragging) {
            masking = Masking::Revealing;  // one still frame first, then the editors return
            again = true;
        } else if (masking != Masking::None) {
            again = true;
        }
        if (thumbHover.IsAnimating(now)) {
            ::InvalidateRect(indicator, nullptr, FALSE);
            again = true;
        } else if (indicator) {
            ::InvalidateRect(indicator, nullptr, FALSE);  // the settled width
        }
        if (again) {
            RequestFrame();
        } else {
            StopTimer();
        }
    }

    void PolicyChanged() {
        if (surface->Motion().AllowsMovement() && surface->Material() != FormMaterial::Native) return;
        const int target = deferred.value_or(Goal());
        scrolling = false;
        EndMasking();
        spring.SnapTo(static_cast<float>(target));
        thumbHover.SnapTo(thumbHover.Target());
        if (auto* scheduler = surface->Scheduler()) scheduler->Cancel(this);
        frameRequested = false;
        StopTimer();
        Apply(target, false);
        if (indicator) ::InvalidateRect(indicator, nullptr, FALSE);
    }

    void SetIndicatorHover(bool value) {
        if (hovered == value) return;
        hovered = value;
        const float goal = hovered || dragging ? 1.0f : 0.0f;
        const auto& motion = surface->Motion();
        thumbHover.AnimateTo(goal, Now(), motion.Scale(Tokens().form.feedback), Tokens().curves.standard);
        RequestFrame();
    }

    // --- indicator ---
    struct Thumb {
        int top = 0;
        int length = 0;
        int track = 0;
        int inset = 0;
    };
    [[nodiscard]] Thumb ThumbGeometry() const {
        Thumb thumb;
        RECT client{};
        ::GetClientRect(indicator, &client);
        thumb.inset = Px(Tokens().form.scrollbarInset);
        thumb.track = std::max(1, static_cast<int>(client.bottom) - 2 * thumb.inset);
        const int range = Range();
        const int page = std::max(1, Page());
        const int total = std::max(page, content);
        thumb.length = std::clamp(static_cast<int>(std::lround(static_cast<double>(thumb.track) * page / total)),
                                  std::min(thumb.track, Px(kMinThumbDip)), thumb.track);
        thumb.top = thumb.inset;
        if (range > 0) thumb.top += static_cast<int>(std::lround(static_cast<double>(thumb.track - thumb.length) * position / range));
        return thumb;
    }

    // The thumb in DIPs of a window whose client starts `offsetXPx` left of the indicator's.
    void DrawThumb(ID2D1DeviceContext* target, const graphics::DesignTokens& tokens, int offsetXPx) {
        if (!indicator || Range() <= 0) return;
        const Thumb thumb = ThumbGeometry();
        RECT client{};
        ::GetClientRect(indicator, &client);
        const float amount = std::clamp(thumbHover.Value(Now()), 0.0f, 1.0f);
        const auto& f = tokens.form;
        const float scale = surface->ToPx(1.0f);
        const float width = f.scrollbarWidth + (f.scrollbarHoverWidth - f.scrollbarWidth) * amount;
        const float right = static_cast<float>(offsetXPx + client.right - thumb.inset) / scale;
        const auto rect = D2D1::RectF(right - width, static_cast<float>(thumb.top) / scale, right,
                                      static_cast<float>(thumb.top + thumb.length) / scale);
        paint::PaintScrollThumb(target, rect, amount, tokens);
    }

    void PaintIndicator() {
        PAINTSTRUCT ps{};
        HDC hdc = ::BeginPaint(indicator, &ps);
        if (hdc && surface && surface->Attached()) {
            const Thumb thumb = ThumbGeometry();
            RECT client{};
            ::GetClientRect(indicator, &client);
            const float amount = std::clamp(thumbHover.Value(Now()), 0.0f, 1.0f);
            if (surface->Material() == FormMaterial::Native) {
                surface->PaintRegion(indicator, hdc, ps.rcPaint);
                const int width = Px(amount > 0.5f ? Tokens().form.scrollbarHoverWidth : Tokens().form.scrollbarWidth);
                RECT r{client.right - thumb.inset - width, thumb.top, client.right - thumb.inset, thumb.top + thumb.length};
                ::FillRect(hdc, &r, ::GetSysColorBrush(hovered || dragging ? COLOR_HIGHLIGHT : COLOR_WINDOWTEXT));
            } else {
                surface->PaintRegion(indicator, hdc, ps.rcPaint, [this](ID2D1DeviceContext* target, const graphics::DesignTokens& tokens) {
                    DrawThumb(target, tokens, 0);
                });
            }
        }
        ::EndPaint(indicator, &ps);
    }

    // Present draws the thumb with the scene (in the viewport's client coordinates).
    void DrawThumbOverlay(ID2D1DeviceContext* target, const graphics::DesignTokens& tokens) {
        if (!indicator || !::IsWindowVisible(indicator)) return;
        RECT bounds{};
        ::GetWindowRect(indicator, &bounds);
        ::MapWindowPoints(nullptr, viewport, reinterpret_cast<POINT*>(&bounds), 2);
        DrawThumb(target, tokens, bounds.left);
    }

    void IndicatorMouse(UINT message, LPARAM lParam) {
        const int y = static_cast<short>(HIWORD(lParam));
        switch (message) {
            case WM_MOUSEMOVE:
                if (!hovered) {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, indicator, 0};
                    ::TrackMouseEvent(&track);
                    SetIndicatorHover(true);
                }
                if (dragging) {
                    const Thumb thumb = ThumbGeometry();
                    const int travel = std::max(1, thumb.track - thumb.length);
                    const double fraction = static_cast<double>(y - dragOffset - thumb.inset) / travel;
                    scrolling = false;
                    const int target = static_cast<int>(std::lround(fraction * Range()));
                    spring.SnapTo(static_cast<float>(std::clamp(target, 0, Range())));
                    Apply(target, true);  // a drag is continuous motion
                }
                break;
            case WM_MOUSELEAVE:
                SetIndicatorHover(false);
                break;
            case WM_LBUTTONDOWN: {
                const Thumb thumb = ThumbGeometry();
                if (y >= thumb.top && y < thumb.top + thumb.length) {
                    dragging = true;
                    dragOffset = y - thumb.top;
                    ::SetCapture(indicator);
                } else {
                    // Track click pages towards the pointer.
                    const int step = std::max(1, Page() - Px(kLineDip));
                    ScrollTo(Goal() + (y < thumb.top ? -step : step), true);
                }
                ::InvalidateRect(indicator, nullptr, FALSE);
                break;
            }
            case WM_LBUTTONUP:
                if (dragging) ::ReleaseCapture();
                break;
            case WM_CAPTURECHANGED:
                if (dragging) {
                    dragging = false;
                    SetIndicatorHover(false);
                    hovered = false;
                    thumbHover.AnimateTo(0.0f, Now(), surface->Motion().Scale(Tokens().form.feedback), Tokens().curves.standard);
                    RequestFrame();
                }
                break;
            default:
                break;
        }
    }

    // --- input ---
    bool Wheel(WPARAM wParam) {
        if (Range() <= 0) return false;
        const int raw = GET_WHEEL_DELTA_WPARAM(wParam);
        wheelRemainder += raw;
        UINT lines = 3;
        ::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        int delta = 0;
        if (lines == WHEEL_PAGESCROLL) {
            const int pages = wheelRemainder / WHEEL_DELTA;
            wheelRemainder -= pages * WHEEL_DELTA;
            delta = -pages * Page();
        } else {
            // Precision touchpads send fractions of a notch: scroll them proportionally.
            const double pixels = static_cast<double>(wheelRemainder) / WHEEL_DELTA * lines * Px(kWheelLineDip);
            delta = -static_cast<int>(pixels);
            if (delta != 0) wheelRemainder = 0;
        }
        if (delta == 0) return true;
        if (raw % WHEEL_DELTA != 0) ScrollImmediately(delta);  // continuous source: follow it directly
        else ScrollTo(Goal() + delta, true);                   // notches glide on the spring
        return true;
    }

    bool ScrollCode(int code, std::optional<int> thumbPosition) {
        const int line = Px(kLineDip);
        const int page = std::max(line, Page() - line);
        switch (code) {
            case SB_LINEUP: ScrollTo(Goal() - line, false); return true;
            case SB_LINEDOWN: ScrollTo(Goal() + line, false); return true;
            case SB_PAGEUP: ScrollTo(Goal() - page, false); return true;
            case SB_PAGEDOWN: ScrollTo(Goal() + page, false); return true;
            case SB_TOP: ScrollTo(0, false); return true;
            case SB_BOTTOM: ScrollTo(Range(), false); return true;
            case SB_ENDSCROLL: return true;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK:
                if (!thumbPosition) return false;
                ScrollTo(*thumbPosition, false);
                return true;
            default: return false;
        }
    }

    bool EnsureVisible(const RECT& rect, int margin) {
        const int page = Page();
        const int base = Goal();
        const int top = rect.top - margin;
        const int bottom = rect.bottom + margin;
        int next = base;
        if (top < base) {
            next = top;
        } else if (bottom > base + page) {
            next = std::min(top, bottom - page);
        }
        next = std::clamp(next, 0, Range());
        if (next == base) return false;
        ScrollTo(next, false);
        return true;
    }

    bool RevealFocus(int margin) {
        HWND focus = ::GetFocus();
        if (!canvas || !focus || !::IsChild(canvas, focus)) return false;
        // Prefer the registered control's visual frame (a field's well is larger than its editor).
        HWND target = focus;
        for (HWND walk = focus; walk && walk != canvas; walk = ::GetParent(walk)) {
            if (surface->Spec(walk)) {
                target = walk;
                break;
            }
        }
        RECT rect{};
        if (surface->Spec(target)) {
            rect = surface->Frame(target);
            ::MapWindowPoints(::GetParent(target), canvas, reinterpret_cast<POINT*>(&rect), 2);
        } else {
            ::GetWindowRect(target, &rect);
            ::MapWindowPoints(nullptr, canvas, reinterpret_cast<POINT*>(&rect), 2);
        }
        return EnsureVisible(rect, margin);
    }

    // --- UI Automation ---
    [[nodiscard]] double VerticalPercent() const noexcept {
        const int range = Range();
        if (range <= 0) return UIA_ScrollPatternNoScroll;
        return std::clamp(100.0 * position / range, 0.0, 100.0);
    }

    [[nodiscard]] double VerticalViewSize() const noexcept {
        const int page = Page();
        if (content <= page || content <= 0) return 100.0;
        return std::clamp(100.0 * page / content, 0.0, 100.0);
    }

    void ScrollByAmount(ScrollAmount amount) noexcept {
        try {
            switch (amount) {
                case ScrollAmount_LargeDecrement: ScrollCode(SB_PAGEUP, std::nullopt); break;
                case ScrollAmount_SmallDecrement: ScrollCode(SB_LINEUP, std::nullopt); break;
                case ScrollAmount_LargeIncrement: ScrollCode(SB_PAGEDOWN, std::nullopt); break;
                case ScrollAmount_SmallIncrement: ScrollCode(SB_LINEDOWN, std::nullopt); break;
                default: break;
            }
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "UI Automation scroll failed");
        }
    }

    void ScrollToPercent(double percent) noexcept {
        try {
            ScrollTo(static_cast<int>(std::lround(percent / 100.0 * Range())), false);
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "UI Automation scroll failed");
        }
    }

    IRawElementProviderSimple* Provider() {
        if (!provider) {
            access = std::make_shared<ScrollAccess>();
            access->hwnd = viewport;
            access->thread = ::GetCurrentThreadId();
            access->percent = [this] { return VerticalPercent(); };
            access->viewSize = [this] { return VerticalViewSize(); };
            access->scrollable = [this] { return Range() > 0; };
            access->scrollBy = [this](ScrollAmount amount) { ScrollByAmount(amount); };
            access->scrollTo = [this](double percent) { ScrollToPercent(percent); };
            provider.attach(static_cast<IRawElementProviderSimple*>(new ScrollProvider(access)));
        }
        return provider.get();
    }

    void Disconnect() noexcept {
        if (access) access->connected.store(false);
        if (viewport) (void)::UiaReturnRawElementProvider(viewport, 0, 0, nullptr);
        if (provider) (void)::UiaDisconnectProvider(provider.get());
        provider = nullptr;
        access.reset();
    }

    void DestroyWindows() noexcept {
        if (surface) {
            if (auto* scheduler = surface->Scheduler()) scheduler->Cancel(this);
        }
        frameRequested = false;
        StopTimer();
        if (viewport && ::IsWindow(viewport)) ::DestroyWindow(viewport);
        Disconnect();
        viewport = canvas = indicator = nullptr;
    }

    static Result<void> RegisterClasses() {
        const HINSTANCE instance = ::GetModuleHandleW(nullptr);
        WNDCLASSEXW viewportClass{sizeof(viewportClass)};
        viewportClass.lpfnWndProc = &ViewportProc;
        viewportClass.hInstance = instance;
        viewportClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        viewportClass.lpszClassName = kViewportClass;
        if (auto result = platform::RegisterWindowClassOnce(viewportClass); !result) return result.error();
        WNDCLASSEXW canvasClass = viewportClass;
        canvasClass.lpfnWndProc = &CanvasProc;
        canvasClass.lpszClassName = kCanvasClass;
        if (auto result = platform::RegisterWindowClassOnce(canvasClass); !result) return result.error();
        WNDCLASSEXW indicatorClass = viewportClass;
        indicatorClass.lpfnWndProc = &IndicatorProc;
        indicatorClass.lpszClassName = kIndicatorClass;
        if (auto result = platform::RegisterWindowClassOnce(indicatorClass); !result) return result.error();
        return Ok();
    }

    // --- window procedures ---
    static Impl* From(HWND hwnd) noexcept { return reinterpret_cast<Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

    static LRESULT CALLBACK ViewportProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        Impl* self = From(hwnd);
        if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
        try {
            switch (message) {
                case WM_MOUSEWHEEL:
                    if (self->Wheel(wParam)) return 0;
                    break;
                case WM_VSCROLL:
                    if (lParam == 0 && self->ScrollCode(LOWORD(wParam), static_cast<int>(HIWORD(wParam)))) return 0;
                    break;
                case WM_TIMER:
                    if (wParam == kAnimationTimer) {
                        self->Tick(self->Now());
                        return 0;
                    }
                    break;
                case WM_SIZE:
                    self->Layout();
                    break;
                case WM_GETOBJECT:
                    if (static_cast<LONG>(lParam) == UiaRootObjectId && self->viewport == hwnd) {
                        return ::UiaReturnRawElementProvider(hwnd, wParam, lParam, self->Provider());
                    }
                    break;
                case WM_DESTROY:
                    self->Disconnect();
                    self->StopTimer();
                    break;
                case WM_NCDESTROY:
                    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                    if (self->viewport == hwnd) self->viewport = self->canvas = self->indicator = nullptr;
                    break;
                default:
                    if (message == detail::PolicyChangedMessage()) {
                        self->PolicyChanged();
                        return 0;
                    }
                    if (message == detail::RevealFocusMessage()) {
                        self->RevealFocus(self->Px(kRevealMarginDip));
                        return 0;
                    }
                    break;
            }
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "scroll viewport message {} failed", message);
        }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        if (message == WM_NCDESTROY) ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        // Wheel messages from the content travel up to the viewport through DefWindowProc.
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK IndicatorProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        Impl* self = From(hwnd);
        if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
        try {
            switch (message) {
                case WM_ERASEBKGND:
                    return 1;
                case WM_PAINT:
                    self->PaintIndicator();
                    return 0;
                case WM_MOUSEMOVE:
                case WM_MOUSELEAVE:
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_CAPTURECHANGED:
                    self->IndicatorMouse(message, lParam);
                    return 0;
                case WM_NCDESTROY:
                    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                    break;
                default:
                    break;
            }
        } catch (...) {
            AETHER_LOG_ERROR("ui_form", "scroll indicator message {} failed", message);
        }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
};

FormScrollView::FormScrollView() : impl_(std::make_unique<Impl>()) { impl_->owner = this; }
FormScrollView::~FormScrollView() { Destroy(); }

Result<void> FormScrollView::Create(FormSurface& surface, HWND parent, int id) {
    auto& impl = *impl_;
    try {
        if (impl.viewport) return MakeError(ErrorCode::InvalidState, "scroll view already created");
        if (!surface.Attached()) return MakeError(ErrorCode::InvalidState, "form surface is not attached");
        if (!parent || (parent != surface.Window() && !::IsChild(surface.Window(), parent)))
            return MakeError(ErrorCode::InvalidArgument, "scroll view parent is not part of the form");
        if (auto registered = Impl::RegisterClasses(); !registered) return registered;
        impl.surface = &surface;
        const HINSTANCE instance = ::GetModuleHandleW(nullptr);
        constexpr DWORD style = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        impl.viewport = ::CreateWindowExW(WS_EX_CONTROLPARENT, kViewportClass, L"", style, 0, 0, 0, 0, parent,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, &impl);
        if (!impl.viewport) return MakeError(ErrorCode::Platform, "scroll viewport creation failed", ::GetLastError());
        impl.canvas = ::CreateWindowExW(WS_EX_CONTROLPARENT, kCanvasClass, L"", style, 0, 0, 0, 0, impl.viewport, nullptr,
                                        instance, &impl);
        impl.indicator = ::CreateWindowExW(WS_EX_NOPARENTNOTIFY, kIndicatorClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                                           impl.viewport, nullptr, instance, &impl);
        if (!impl.canvas || !impl.indicator) {
            const auto error = ::GetLastError();
            impl.DestroyWindows();
            return MakeError(ErrorCode::Platform, "scroll canvas creation failed", error);
        }
        ControlSpec container;
        container.role = ControlRole::Container;
        if (auto result = surface.Register(impl.viewport, container); !result) {
            impl.DestroyWindows();
            return result;
        }
        if (auto result = surface.Register(impl.canvas, container); !result) {
            impl.DestroyWindows();
            return result;
        }
        surface.SetContainerOverlay(
            impl.viewport, [&impl](ID2D1DeviceContext* target, const graphics::DesignTokens& tokens) { impl.DrawThumbOverlay(target, tokens); },
            impl.indicator);
        impl.Layout();
        return Ok();
    } catch (...) {
        impl.DestroyWindows();
        return MakeError(ErrorCode::Internal, "scroll view creation failed");
    }
}

void FormScrollView::Destroy() noexcept { impl_->DestroyWindows(); }
HWND FormScrollView::Viewport() const noexcept { return impl_->viewport; }
HWND FormScrollView::Canvas() const noexcept { return impl_->canvas; }

void FormScrollView::SetFrame(const RECT& framePx) {
    auto& impl = *impl_;
    impl.frame = framePx;
    if (!impl.viewport) return;
    try {
        ::SetWindowPos(impl.viewport, nullptr, framePx.left, framePx.top, framePx.right - framePx.left,
                       framePx.bottom - framePx.top, SWP_NOZORDER | SWP_NOACTIVATE);
        impl.Layout();
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "scroll view layout failed");
    }
}

RECT FormScrollView::Frame() const noexcept { return impl_->frame; }

void FormScrollView::SetContentHeight(int heightPx) {
    auto& impl = *impl_;
    impl.content = std::max(0, heightPx);
    if (!impl.viewport) return;
    try {
        const int before = impl.position;
        impl.Layout();
        if (impl.position != before) {
            // Layout clamped the position: report it like any other scroll.
            const int clamped = impl.position;
            impl.position = before;
            impl.Apply(clamped, false);
        }
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "scroll view layout failed");
    }
}

int FormScrollView::ContentHeight() const noexcept { return impl_->content; }
int FormScrollView::GutterPx() const noexcept { return impl_->Gutter(); }
int FormScrollView::Position() const noexcept { return impl_->position; }
int FormScrollView::Range() const noexcept { return impl_->Range(); }
int FormScrollView::Page() const noexcept { return impl_->Page(); }

void FormScrollView::ScrollTo(int positionPx, bool animate) {
    try {
        impl_->ScrollTo(positionPx, animate);
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "scroll failed");
    }
}

void FormScrollView::ScrollBy(int deltaPx, bool animate) {
    try {
        impl_->ScrollTo(impl_->Goal() + deltaPx, animate);
    } catch (...) {
        AETHER_LOG_ERROR("ui_form", "scroll failed");
    }
}

bool FormScrollView::EnsureVisible(const RECT& canvasRectPx, int marginPx) {
    try {
        return impl_->viewport && impl_->EnsureVisible(canvasRectPx, marginPx);
    } catch (...) {
        return false;
    }
}

bool FormScrollView::RevealFocus(int marginPx) {
    try {
        return impl_->viewport && impl_->RevealFocus(marginPx);
    } catch (...) {
        return false;
    }
}

bool FormScrollView::HandleScrollCode(int code) {
    try {
        return impl_->viewport && impl_->ScrollCode(code, std::nullopt);
    } catch (...) {
        return false;
    }
}

bool FormScrollView::HandleScrollCode(int code, int thumbPositionPx) {
    try {
        return impl_->viewport && impl_->ScrollCode(code, thumbPositionPx);
    } catch (...) {
        return false;
    }
}

}  // namespace aether::ui::form
