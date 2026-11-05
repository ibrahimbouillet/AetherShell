// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The transient FR-041 surface: one composition window on the normal desktop, never over the secure
// Windows desktop, showing the handwriting as it is written over the person's blurred desktop picture
// (D54). It can go up early, as a cover behind Windows' lock screen, so that an unlock reveals it instead
// of the desktop. It takes no focus and no keyboard, any touch or click on it skips it, and it owns no
// timer of its own: the application's controller decides when it ends. Reduced motion shows the finished
// words at once, without a moving pen; high contrast also replaces the picture with its system colors.
#pragma once

#include "core/Result.h"
#include "core/Signal.h"
#include "graphics/TextCache.h"
#include "graphics/Theme.h"
#include "ui/SurfaceWindow.h"
#include "welcome/Handwriting.h"
#include "welcome/WelcomeBackdrop.h"

#include <memory>
#include <optional>
#include <string>

namespace aether::welcome {

// Paints the ink written by `time` (and the moving pen) with its top-left at `origin`. Separate from
// the window so that it can be drawn and inspected offscreen.
void PaintHandwriting(ID2D1DeviceContext* context, const WelcomeScene& scene, D2D1_POINT_2F origin, float time, ID2D1Brush* ink, ID2D1Brush* pen);
// Only the ink the pen lays between two moments. The window keeps what is already written in a bitmap and
// adds this each frame, so a large greeting costs the same at its last stroke as at its first.
void PaintHandwritingRange(ID2D1DeviceContext* context, const WelcomeScene& scene, D2D1_POINT_2F origin, float fromTime, float toTime, ID2D1Brush* ink);
// Where that ink lies, with its width, in the scene's own coordinates; nullopt when there is none.
[[nodiscard]] std::optional<D2D1_RECT_F> HandwritingBounds(const WelcomeScene& scene, float fromTime, float toTime);

// The soft dark edge under the ink, which keeps pale ink clear over a pale picture (D61). Blurring the
// whole greeting again for every frame cost a blur of most of the screen per frame; this keeps the edge in
// its own bitmap and blurs again only around the ink just added. The result is the same as one blur of all.
class InkShadow {
public:
    // A fresh edge for `ink`, drawn by `context` at its DPI; false when the graphics refuse it.
    [[nodiscard]] bool Reset(ID2D1DeviceContext* context, ID2D1Bitmap1* ink, float deviation);
    // After ink was added inside `inkRect` (DIPs of the ink bitmap): the edge there and as far around as it reaches.
    [[nodiscard]] bool Update(ID2D1DeviceContext* context, const D2D1_RECT_F& inkRect);
    void Clear() noexcept;
    [[nodiscard]] ID2D1Bitmap1* Bitmap() const noexcept { return bitmap_.get(); }
    // How far the edge's bitmap extends beyond the ink bitmap on every side, in DIPs.
    [[nodiscard]] float Reach() const noexcept { return reach_; }

private:
    winrt::com_ptr<ID2D1Effect> effect_;
    winrt::com_ptr<ID2D1Bitmap1> bitmap_;
    float reach_ = 0.0f;
    float scale_ = 1.0f;  // pixels per DIP
};

class WelcomeWindow final : public ui::SurfaceWindow {
public:
    WelcomeWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler);
    ~WelcomeWindow() override;

    // The desktop picture; null for the plain desktop colour. It may arrive while the window shows.
    void SetBackdrop(std::shared_ptr<const BackdropImage> image);
    // The background alone, at once and without an entrance: it waits behind the lock screen.
    [[nodiscard]] Result<void> ShowCover(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion, const RECT& boundsPx);
    // Starts writing, over the cover if it is up, otherwise with an entrance.
    [[nodiscard]] Result<void> Present(std::wstring_view greeting, std::wstring_view name, const graphics::DesignTokens& tokens,
                                       const graphics::MotionPolicy& motion, const RECT& boundsPx);
    void Dismiss(bool animated);
    [[nodiscard]] bool Presenting() const noexcept { return presenting_; }
    [[nodiscard]] bool Covering() const noexcept { return covering_; }
    // Whether the writing has finished and been there long enough to read (without motion: just to read).
    [[nodiscard]] bool WritingFinished(double now) const noexcept;
    [[nodiscard]] const WelcomeScene& Scene() const noexcept { return scene_; }
    [[nodiscard]] bool AnimatesInk() const noexcept { return animated_; }
    [[nodiscard]] bool ShowsPicture() const noexcept { return blurred_ != nullptr; }
    [[nodiscard]] std::uint64_t RenderedFrames() const noexcept { return frames_; }

    Signal<> SkipRequested;

protected:
    void OnRender(const ui::RenderContext& context) override;
    bool OnAnimate(double now) override;
    void OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) override;
    bool OnTouchDown(const ui::TouchContact& contact) override;
    bool OnKeyDown(UINT virtualKey) override;
    void OnDeviceReplaced() override;

private:
    [[nodiscard]] Result<void> Prepare(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion, const RECT& boundsPx);
    void EnsureBackdrop();
    [[nodiscard]] bool EnsureInkLayer();
    void AdvanceInk(double now);
    void ResetInk() noexcept;
    [[nodiscard]] float InkMargin() const noexcept;
    void RequestSkip();

    graphics::DesignTokens tokens_;
    graphics::MotionPolicy motion_;
    graphics::TextCache text_;
    bool textReady_ = false;
    WelcomeScene scene_;
    std::shared_ptr<const BackdropImage> image_;
    winrt::com_ptr<ID2D1Bitmap1> blurred_;
    winrt::com_ptr<ID2D1DeviceContext> inkContext_;
    winrt::com_ptr<ID2D1Bitmap1> inkLayer_;
    winrt::com_ptr<ID2D1SolidColorBrush> inkBrush_;
    InkShadow shadow_;
    float inkUntil_ = -1.0f;
    double startedAt_ = 0.0;
    bool animated_ = true;
    bool presenting_ = false;
    bool covering_ = false;
    bool skipRequested_ = false;
    std::uint64_t frames_ = 0;
    // For the log line once a greeting is written: how smoothly it went on this PC.
    std::uint64_t writingFrames_ = 0;
    double lastFrameAt_ = 0.0, longestGap_ = 0.0;
    bool writingLogged_ = true;
};

}  // namespace aether::welcome
