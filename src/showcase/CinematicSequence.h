// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "graphics/Animation.h"

#include <array>
#include <cstddef>
#include <optional>

namespace aether::showcase {

struct Point final {
    double x = 0.0;
    double y = 0.0;
};

struct Size final {
    double width = 0.0;
    double height = 0.0;
};

struct Rect final {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

enum class EasingProfile {
    // Quintic smootherstep: position, velocity and acceleration settle at both ends of every move.
    SmoothQuintic,
    // Explicit art-direction override. Review its joins before using it for a finished sequence.
    CustomCubicBezier,
};

struct CubicPath final {
    Point start;
    Point control1;
    Point control2;
    Point end;
};

struct SequenceTiming final {
    double desktopHold = 2.0;
    double cursorApproach = 1.1;
    double dockDive = 1.4;
    double horizontalTrack = 2.4;
    double focusPause = 1.0;
    double cameraReveal = 1.6;
    double finalHold = 3.0;
    // Zero preserves the legacy app-view ending. A positive hold enables observed close/reveal
    // choreography: hold the app, request closure, then hold the genuinely empty desktop.
    double postCloseHold = 0.0;
    double topMarginPixels = 48.0;
    double cursorAnchorX = 0.54;
    double dockEntryFraction = 0.0;
    // Magnification relative to the fitted desktop view. Keep native raster marks readable;
    // framing is also limited by the Dock's available vertical space.
    double maximumDockZoom = 3.0;
    EasingProfile easingProfile = EasingProfile::SmoothQuintic;
    graphics::CubicBezier approachCurve{0.2f, 0.0f, 0.0f, 1.0f};
    graphics::CubicBezier diveCurve{0.2f, 0.0f, 0.0f, 1.0f};
    graphics::CubicBezier trackCurve{0.2f, 0.0f, 0.0f, 1.0f};
    graphics::CubicBezier revealCurve{0.42f, 0.0f, 0.58f, 1.0f};
};

struct SequenceConfig final {
    Size sourcePixels;
    Size outputPixels{1920.0, 1080.0};
    Rect dockBoundsPixels;
    Point visualStudioCodeIconPixels;
    Point cursorStartPixels;
    SequenceTiming timing;
};

enum class SequencePhase {
    DesktopHold,
    CursorApproach,
    DockDive,
    HorizontalTrack,
    FocusVisualStudioCode,
    RevealWindow,
    FinalHold,
    AwaitWindowClose,
    PostCloseHold,
};

struct CameraPose final {
    Point centerPixels;
    double zoom = 1.0;
};

struct SequenceEvents final {
    // Set only after the real showcase window is visible and the runner's readiness check passes.
    // Until this observable event arrives, the camera holds on the real Dock hover state.
    std::optional<double> visualStudioCodeWindowVisibleAt;
    // The orchestration layer latches this after consuming the launch action, so repeated
    // stateless frame evaluations cannot launch VS Code more than once.
    bool visualStudioCodeLaunchRequested = false;
    // Supplied only after the runner observes that its showcase app window has actually closed.
    std::optional<double> visualStudioCodeWindowClosedAt;
    bool visualStudioCodeCloseRequested = false;
};

struct FrameState final {
    SequencePhase phase = SequencePhase::DesktopHold;
    CameraPose camera;
    Point cursorSourcePixels;
    Point cursorOutputPixels;
    double launchAtSeconds = 0.0;
    double phaseProgress = 0.0;
    bool launchVisualStudioCode = false;
    bool closeVisualStudioCode = false;
    bool visualStudioCodeWindowVisible = false;
    bool finished = false;
};

struct TemporalSampleSet final {
    static constexpr std::size_t MaximumSamples = 8;
    std::array<double, MaximumSamples> timestamps{};
    std::size_t count = 0;
    double exposureSeconds = 0.0;
};

// Stateless timeline evaluation: the same configuration, timestamp and observed event produce the
// same frame state. It does not capture pixels, move the user's pointer, launch applications or
// change process/window state; the orchestration and frame backends consume this plan separately.
class CinematicSequence final {
public:
    [[nodiscard]] static Result<CinematicSequence> Create(SequenceConfig config);

    [[nodiscard]] FrameState Evaluate(double timeSeconds, const SequenceEvents& events = {}) const noexcept;
    [[nodiscard]] const SequenceConfig& Config() const noexcept { return config_; }
    [[nodiscard]] double LaunchAtSeconds() const noexcept;
    [[nodiscard]] std::optional<double> CloseAtSeconds(const SequenceEvents& events) const noexcept;
    [[nodiscard]] std::optional<double> DurationSeconds(const SequenceEvents& events) const noexcept;

    [[nodiscard]] static Point MapSourceToOutput(Point sourcePoint, const CameraPose& camera,
                                                  Size outputPixels) noexcept;
    [[nodiscard]] static Rect MapSourceToOutput(Rect sourceRect, const CameraPose& camera,
                                                 Size outputPixels) noexcept;
    [[nodiscard]] static Result<TemporalSampleSet> BuildMotionBlurSamples(
        double frameCenterSeconds, double framesPerSecond = 60.0, double shutterAngleDegrees = 180.0,
        std::size_t sampleCount = TemporalSampleSet::MaximumSamples);

private:
    explicit CinematicSequence(SequenceConfig config) : config_(std::move(config)) {}

    [[nodiscard]] CubicPath ApproachPath() const noexcept;
    [[nodiscard]] CubicPath TrackPath() const noexcept;
    [[nodiscard]] CubicPath ReturnPath() const noexcept;
    [[nodiscard]] Point DockEntry() const noexcept;
    [[nodiscard]] double DockZoom() const noexcept;
    [[nodiscard]] double DockBottomPadding() const noexcept;
    [[nodiscard]] CameraPose DockPose(Point cursor) const noexcept;
    [[nodiscard]] CameraPose DockPose(Point cursor, double zoom) const noexcept;

    SequenceConfig config_;
};

}  // namespace aether::showcase
