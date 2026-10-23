// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/CinematicSequence.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace aether::showcase {
namespace {

bool IsFinite(Point point) noexcept { return std::isfinite(point.x) && std::isfinite(point.y); }
bool IsFinite(Size size) noexcept { return std::isfinite(size.width) && std::isfinite(size.height); }
bool IsFinite(Rect rect) noexcept {
    return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) && std::isfinite(rect.bottom);
}

bool IsInside(Point point, Size size) noexcept {
    return point.x >= 0.0 && point.y >= 0.0 && point.x <= size.width && point.y <= size.height;
}

double Clamp01(double value) noexcept { return std::clamp(value, 0.0, 1.0); }

Point Lerp(Point from, Point to, double amount) noexcept {
    return {from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount};
}

Point EvaluatePath(const CubicPath& path, double progress) noexcept {
    const double t = Clamp01(progress);
    const double inverse = 1.0 - t;
    const double a = inverse * inverse * inverse;
    const double b = 3.0 * inverse * inverse * t;
    const double c = 3.0 * inverse * t * t;
    const double d = t * t * t;
    return {a * path.start.x + b * path.control1.x + c * path.control2.x + d * path.end.x,
            a * path.start.y + b * path.control1.y + c * path.control2.y + d * path.end.y};
}

double CurveProgress(const graphics::CubicBezier& curve, double progress) noexcept {
    return static_cast<double>(graphics::EvaluateCubicBezier(curve, static_cast<float>(Clamp01(progress))));
}

double SmoothQuintic(double progress) noexcept {
    const double t = Clamp01(progress);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double TransitionProgress(EasingProfile profile, const graphics::CubicBezier& curve, double progress) noexcept {
    return profile == EasingProfile::CustomCubicBezier ? CurveProgress(curve, progress) : SmoothQuintic(progress);
}

bool Positive(double value) noexcept { return std::isfinite(value) && value > 0.0; }

bool HasRestToRestBezier(const graphics::CubicBezier& curve) noexcept {
    constexpr float kTolerance = 1e-6f;
    return std::isfinite(curve.x1) && std::isfinite(curve.y1) && std::isfinite(curve.x2) &&
           std::isfinite(curve.y2) && curve.x1 > 0.0f && curve.x1 <= 1.0f && curve.x2 >= 0.0f &&
           curve.x2 < 1.0f && std::abs(curve.y1) <= kTolerance && std::abs(curve.y2 - 1.0f) <= kTolerance;
}

}  // namespace

Result<CinematicSequence> CinematicSequence::Create(SequenceConfig config) {
    const auto& timing = config.timing;
    const double durations[] = {timing.desktopHold, timing.cursorApproach, timing.dockDive,
                                timing.horizontalTrack, timing.focusPause, timing.cameraReveal,
                                timing.finalHold};
    if (!IsFinite(config.sourcePixels) || !Positive(config.sourcePixels.width) || !Positive(config.sourcePixels.height) ||
        !IsFinite(config.outputPixels) || !Positive(config.outputPixels.width) || !Positive(config.outputPixels.height) ||
        !IsFinite(config.dockBoundsPixels) || config.dockBoundsPixels.left < 0.0 || config.dockBoundsPixels.top < 0.0 ||
        config.dockBoundsPixels.right > config.sourcePixels.width || config.dockBoundsPixels.bottom > config.sourcePixels.height ||
        config.dockBoundsPixels.right <= config.dockBoundsPixels.left || config.dockBoundsPixels.bottom <= config.dockBoundsPixels.top ||
        !IsInside(config.visualStudioCodeIconPixels, config.sourcePixels) || !IsInside(config.cursorStartPixels, config.sourcePixels) ||
        !std::isfinite(timing.topMarginPixels) || timing.topMarginPixels < 0.0 ||
        timing.topMarginPixels >= config.outputPixels.height || !std::isfinite(timing.cursorAnchorX) ||
        timing.cursorAnchorX < 0.0 || timing.cursorAnchorX > 1.0 || !std::isfinite(timing.dockEntryFraction) ||
        timing.dockEntryFraction < 0.0 || timing.dockEntryFraction > 1.0 ||
        !std::isfinite(timing.postCloseHold) || timing.postCloseHold < 0.0 ||
        !std::isfinite(timing.maximumDockZoom) || timing.maximumDockZoom < 1.0 ||
        (timing.easingProfile != EasingProfile::SmoothQuintic &&
         timing.easingProfile != EasingProfile::CustomCubicBezier)) {
        return MakeError(ErrorCode::InvalidArgument, "showcase sequence geometry or timing is invalid");
    }
    if (timing.easingProfile == EasingProfile::CustomCubicBezier &&
        (!HasRestToRestBezier(timing.approachCurve) || !HasRestToRestBezier(timing.diveCurve) ||
         !HasRestToRestBezier(timing.trackCurve) || !HasRestToRestBezier(timing.revealCurve))) {
        return MakeError(ErrorCode::InvalidArgument,
                         "custom showcase curves must have finite controls and zero velocity at both endpoints");
    }
    for (const double duration : durations) {
        if (!Positive(duration)) return MakeError(ErrorCode::InvalidArgument, "showcase sequence durations must be positive");
    }
    if (!Positive(config.dockBoundsPixels.bottom - config.dockBoundsPixels.top) ||
        (config.outputPixels.height - timing.topMarginPixels) /
                (config.dockBoundsPixels.bottom - config.dockBoundsPixels.top) < 1.0) {
        return MakeError(ErrorCode::InvalidArgument, "showcase output cannot fit the Dock at the requested scale");
    }
    return CinematicSequence(std::move(config));
}

double CinematicSequence::LaunchAtSeconds() const noexcept {
    const auto& timing = config_.timing;
    return timing.desktopHold + timing.cursorApproach + timing.dockDive + timing.horizontalTrack + timing.focusPause;
}

std::optional<double> CinematicSequence::DurationSeconds(const SequenceEvents& events) const noexcept {
    if (!events.visualStudioCodeWindowVisibleAt || !std::isfinite(*events.visualStudioCodeWindowVisibleAt) ||
        *events.visualStudioCodeWindowVisibleAt < LaunchAtSeconds()) {
        return std::nullopt;
    }
    const double closeAt = *events.visualStudioCodeWindowVisibleAt + config_.timing.cameraReveal + config_.timing.finalHold;
    if (config_.timing.postCloseHold == 0.0) return closeAt;
    if (!events.visualStudioCodeWindowClosedAt || !std::isfinite(*events.visualStudioCodeWindowClosedAt) ||
        *events.visualStudioCodeWindowClosedAt < closeAt) return std::nullopt;
    return *events.visualStudioCodeWindowClosedAt + config_.timing.postCloseHold;
}

std::optional<double> CinematicSequence::CloseAtSeconds(const SequenceEvents& events) const noexcept {
    if (config_.timing.postCloseHold == 0.0 || !events.visualStudioCodeWindowVisibleAt ||
        !std::isfinite(*events.visualStudioCodeWindowVisibleAt) ||
        *events.visualStudioCodeWindowVisibleAt < LaunchAtSeconds()) return std::nullopt;
    return *events.visualStudioCodeWindowVisibleAt + config_.timing.cameraReveal + config_.timing.finalHold;
}

Point CinematicSequence::DockEntry() const noexcept {
    const auto& dock = config_.dockBoundsPixels;
    const double x = dock.left + (dock.right - dock.left) * config_.timing.dockEntryFraction;
    return {x, (dock.top + dock.bottom) / 2.0};
}

CubicPath CinematicSequence::ApproachPath() const noexcept {
    const Point start = config_.cursorStartPixels;
    const Point end = DockEntry();
    const double dy = end.y - start.y;
    const double detourX = std::max(0.0, std::min(start.x, end.x) - config_.sourcePixels.width * 0.12);
    return {start, {detourX, start.y + dy * 0.12}, {detourX, end.y - dy * 0.28}, end};
}

CubicPath CinematicSequence::TrackPath() const noexcept {
    const Point start = DockEntry();
    const Point end = config_.visualStudioCodeIconPixels;
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    return {start, {start.x + dx * 0.34, start.y + dy * 0.08},
            {end.x - dx * 0.22, end.y - dy * 0.06}, end};
}

CubicPath CinematicSequence::ReturnPath() const noexcept {
    const Point start = config_.visualStudioCodeIconPixels;
    const Point end = config_.cursorStartPixels;
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    return {start, {start.x + dx * 0.12, start.y + dy * 0.50},
            {end.x - dx * 0.25, end.y - dy * 0.06}, end};
}

double CinematicSequence::DockZoom() const noexcept {
    const double desktopScale = std::min(config_.outputPixels.width / config_.sourcePixels.width,
                                         config_.outputPixels.height / config_.sourcePixels.height);
    const double fitZoom = (config_.outputPixels.height - config_.timing.topMarginPixels) /
                           (config_.dockBoundsPixels.bottom - config_.dockBoundsPixels.top + DockBottomPadding());
    return std::min(fitZoom, desktopScale * config_.timing.maximumDockZoom);
}

double CinematicSequence::DockBottomPadding() const noexcept {
    // Accessible control bounds omit the material footer and running indicators. Preserve half
    // an icon row plus a little desktop breathing space, bounded by real pixels below the row.
    // Keeping this padding in source coordinates also avoids a velocity kink during the zoom.
    const double rowHeight = config_.dockBoundsPixels.bottom - config_.dockBoundsPixels.top;
    return std::min(config_.sourcePixels.height - config_.dockBoundsPixels.bottom,
                    rowHeight * 0.5 + config_.sourcePixels.height * 0.01);
}

CameraPose CinematicSequence::DockPose(Point cursor) const noexcept {
    return DockPose(cursor, DockZoom());
}

CameraPose CinematicSequence::DockPose(Point cursor, double zoom) const noexcept {
    const double cursorAnchor = config_.outputPixels.width * config_.timing.cursorAnchorX;
    const double centerX = cursor.x - (cursorAnchor - config_.outputPixels.width / 2.0) / zoom;
    const double centerY = config_.dockBoundsPixels.bottom + DockBottomPadding() -
                           config_.outputPixels.height / (2.0 * zoom);
    return {{centerX, centerY}, zoom};
}

Point CinematicSequence::MapSourceToOutput(Point sourcePoint, const CameraPose& camera, Size outputPixels) noexcept {
    if (!IsFinite(sourcePoint) || !IsFinite(camera.centerPixels) || !Positive(camera.zoom) ||
        !IsFinite(outputPixels) || !Positive(outputPixels.width) || !Positive(outputPixels.height)) {
        return {};
    }
    return {(sourcePoint.x - camera.centerPixels.x) * camera.zoom + outputPixels.width / 2.0,
            (sourcePoint.y - camera.centerPixels.y) * camera.zoom + outputPixels.height / 2.0};
}

Rect CinematicSequence::MapSourceToOutput(Rect sourceRect, const CameraPose& camera, Size outputPixels) noexcept {
    const auto topLeft = MapSourceToOutput(Point{sourceRect.left, sourceRect.top}, camera, outputPixels);
    const auto bottomRight = MapSourceToOutput(Point{sourceRect.right, sourceRect.bottom}, camera, outputPixels);
    return {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
}

Result<TemporalSampleSet> CinematicSequence::BuildMotionBlurSamples(double frameCenterSeconds,
                                                                    double framesPerSecond,
                                                                    double shutterAngleDegrees,
                                                                    std::size_t sampleCount) {
    if (!std::isfinite(frameCenterSeconds) || frameCenterSeconds < 0.0 || !Positive(framesPerSecond) ||
        !std::isfinite(shutterAngleDegrees) || shutterAngleDegrees < 0.0 || shutterAngleDegrees > 360.0 ||
        sampleCount == 0 || sampleCount > TemporalSampleSet::MaximumSamples) {
        return MakeError(ErrorCode::InvalidArgument, "showcase temporal sampling settings are invalid");
    }

    TemporalSampleSet samples;
    samples.count = sampleCount;
    samples.exposureSeconds = shutterAngleDegrees / 360.0 / framesPerSecond;
    const double openSeconds = std::max(0.0, frameCenterSeconds - samples.exposureSeconds / 2.0);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        const double fraction = (static_cast<double>(index) + 0.5) / static_cast<double>(sampleCount);
        samples.timestamps[index] = openSeconds + samples.exposureSeconds * fraction;
    }
    return samples;
}

FrameState CinematicSequence::Evaluate(double timeSeconds, const SequenceEvents& events) const noexcept {
    const double time = std::isfinite(timeSeconds) ? std::max(0.0, timeSeconds) : 0.0;
    const auto& timing = config_.timing;
    const double approachStart = timing.desktopHold;
    const double diveStart = approachStart + timing.cursorApproach;
    const double trackStart = diveStart + timing.dockDive;
    const double focusStart = trackStart + timing.horizontalTrack;
    const double launchAt = focusStart + timing.focusPause;
    const Point dockEntry = DockEntry();
    const auto approachPath = ApproachPath();
    const auto trackPath = TrackPath();
    const double dockZoom = DockZoom();
    const double desktopScale = std::min(config_.outputPixels.width / config_.sourcePixels.width,
                                         config_.outputPixels.height / config_.sourcePixels.height);
    const CameraPose desktopPose{{config_.sourcePixels.width / 2.0, config_.sourcePixels.height / 2.0}, desktopScale};
    const double trackEntryZoom = desktopScale + (dockZoom - desktopScale) * 0.35;

    FrameState state;
    state.camera = desktopPose;
    state.cursorSourcePixels = config_.cursorStartPixels;
    state.launchAtSeconds = launchAt;
    state.launchVisualStudioCode = time >= launchAt && !events.visualStudioCodeLaunchRequested;

    if (time < approachStart) {
        state.phase = SequencePhase::DesktopHold;
    } else if (time < diveStart) {
        const double p = TransitionProgress(timing.easingProfile, timing.approachCurve,
                                            (time - approachStart) / timing.cursorApproach);
        state.phase = SequencePhase::CursorApproach;
        state.phaseProgress = p;
        state.cursorSourcePixels = EvaluatePath(approachPath, p);
    } else if (time < trackStart) {
        const double p = TransitionProgress(timing.easingProfile, timing.diveCurve,
                                            (time - diveStart) / timing.dockDive);
        state.phase = SequencePhase::DockDive;
        state.phaseProgress = p;
        state.cursorSourcePixels = dockEntry;
        state.camera.centerPixels = Lerp(desktopPose.centerPixels, DockPose(dockEntry, trackEntryZoom).centerPixels, p);
        state.camera.zoom = desktopScale + (trackEntryZoom - desktopScale) * p;
    } else if (time < focusStart) {
        const double p = TransitionProgress(timing.easingProfile, timing.trackCurve,
                                            (time - trackStart) / timing.horizontalTrack);
        state.phase = SequencePhase::HorizontalTrack;
        state.phaseProgress = p;
        state.cursorSourcePixels = EvaluatePath(trackPath, p);
        const CameraPose pose = DockPose(state.cursorSourcePixels, trackEntryZoom + (dockZoom - trackEntryZoom) * p);
        state.camera = pose;
    } else {
        state.cursorSourcePixels = config_.visualStudioCodeIconPixels;
        state.camera = DockPose(state.cursorSourcePixels);
        if (time < launchAt) {
            state.phase = SequencePhase::FocusVisualStudioCode;
            state.phaseProgress = Clamp01((time - focusStart) / timing.focusPause);
        } else if (events.visualStudioCodeWindowVisibleAt &&
                   std::isfinite(*events.visualStudioCodeWindowVisibleAt) &&
                   *events.visualStudioCodeWindowVisibleAt >= launchAt &&
                   time >= *events.visualStudioCodeWindowVisibleAt) {
            const double revealStart = *events.visualStudioCodeWindowVisibleAt;
            const double revealEnd = revealStart + timing.cameraReveal;
            const double p = TransitionProgress(timing.easingProfile, timing.revealCurve,
                                                (time - revealStart) / timing.cameraReveal);
            state.phaseProgress = p;
            if (time < revealEnd) {
                state.phase = SequencePhase::RevealWindow;
                state.camera.centerPixels = Lerp(state.camera.centerPixels, desktopPose.centerPixels, p);
                state.camera.zoom = dockZoom + (desktopScale - dockZoom) * p;
                state.cursorSourcePixels = EvaluatePath(ReturnPath(), p);
            } else {
                state.phase = SequencePhase::FinalHold;
                state.camera = desktopPose;
                state.cursorSourcePixels = config_.cursorStartPixels;
                const double closeAt = revealEnd + timing.finalHold;
                state.finished = timing.postCloseHold == 0.0 && time >= closeAt;
                state.phaseProgress = state.finished ? 1.0 : Clamp01((time - revealEnd) / timing.finalHold);
                if (timing.postCloseHold > 0.0 && time >= closeAt) {
                    state.phase = SequencePhase::AwaitWindowClose;
                    state.phaseProgress = 1.0;
                    state.closeVisualStudioCode = !events.visualStudioCodeCloseRequested;
                    if (events.visualStudioCodeWindowClosedAt && std::isfinite(*events.visualStudioCodeWindowClosedAt) &&
                        *events.visualStudioCodeWindowClosedAt >= closeAt && time >= *events.visualStudioCodeWindowClosedAt) {
                        state.phase = SequencePhase::PostCloseHold;
                        state.closeVisualStudioCode = false;
                        state.finished = time >= *events.visualStudioCodeWindowClosedAt + timing.postCloseHold;
                        state.phaseProgress = state.finished ? 1.0 :
                            Clamp01((time - *events.visualStudioCodeWindowClosedAt) / timing.postCloseHold);
                    }
                }
            }
            state.visualStudioCodeWindowVisible = state.phase != SequencePhase::PostCloseHold;
        } else {
            state.phase = SequencePhase::FocusVisualStudioCode;
            state.phaseProgress = 1.0;
        }
    }
    state.cursorOutputPixels = MapSourceToOutput(state.cursorSourcePixels, state.camera, config_.outputPixels);
    return state;
}

}  // namespace aether::showcase
