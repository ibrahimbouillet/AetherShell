// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/CinematicSequence.h"

#include <cmath>
#include <limits>

using namespace aether::showcase;

namespace {

SequenceConfig MakeConfig() {
    SequenceConfig config;
    config.sourcePixels = {1920.0, 1080.0};
    config.outputPixels = {1920.0, 1080.0};
    config.dockBoundsPixels = {700.0, 990.0, 1220.0, 1060.0};
    config.visualStudioCodeIconPixels = {1110.0, 1025.0};
    config.cursorStartPixels = {780.0, 650.0};
    return config;
}

bool Near(double a, double b, double tolerance = 0.001) { return std::abs(a - b) <= tolerance; }

}  // namespace

AETHER_TEST("showcase: desktop hold is perfectly stable for its configured opening") {
    auto sequence = CinematicSequence::Create(MakeConfig());
    CHECK(sequence.ok());
    const auto first = sequence->Evaluate(0.0);
    const auto later = sequence->Evaluate(1.999);
    CHECK_EQ(first.phase, SequencePhase::DesktopHold);
    CHECK_EQ(later.phase, SequencePhase::DesktopHold);
    CHECK(Near(first.camera.centerPixels.x, later.camera.centerPixels.x));
    CHECK(Near(first.camera.centerPixels.y, later.camera.centerPixels.y));
    CHECK(Near(first.camera.zoom, later.camera.zoom));
    CHECK(Near(first.cursorSourcePixels.x, later.cursorSourcePixels.x));
    CHECK(Near(first.cursorSourcePixels.y, later.cursorSourcePixels.y));
    CHECK(Near(sequence->Evaluate(2.0).cursorSourcePixels.x, sequence->Evaluate(2.0).cursorSourcePixels.x));
}

AETHER_TEST("showcase: cursor approach has exact endpoints and stays in the source display") {
    auto sequence = CinematicSequence::Create(MakeConfig());
    CHECK(sequence.ok());
    const auto begin = sequence->Evaluate(2.0);
    const auto middle = sequence->Evaluate(2.55);
    const auto end = sequence->Evaluate(3.1);
    CHECK_EQ(begin.phase, SequencePhase::CursorApproach);
    CHECK_EQ(end.phase, SequencePhase::DockDive);
    CHECK(Near(begin.cursorSourcePixels.x, 780.0));
    CHECK(Near(begin.cursorSourcePixels.y, 650.0));
    CHECK(Near(end.cursorSourcePixels.x, 700.0));
    CHECK(Near(end.cursorSourcePixels.y, 1025.0));
    CHECK(middle.cursorSourcePixels.y > begin.cursorSourcePixels.y);
    CHECK(middle.cursorSourcePixels.y < end.cursorSourcePixels.y);
    CHECK(middle.cursorSourcePixels.x >= 0.0 && middle.cursorSourcePixels.x <= 1920.0);
    CHECK(middle.cursorSourcePixels.y >= 0.0 && middle.cursorSourcePixels.y <= 1080.0);
}

AETHER_TEST("showcase: camera and cursor remain position-continuous across every choreography cut") {
    auto sequence = CinematicSequence::Create(MakeConfig());
    CHECK(sequence.ok());
    const double boundaries[] = {2.0, 3.1, 4.5, 6.9, 7.9};
    for (const double boundary : boundaries) {
        const auto before = sequence->Evaluate(boundary - 0.001);
        const auto at = sequence->Evaluate(boundary);
        const auto after = sequence->Evaluate(boundary + 0.001);
        CHECK(std::abs(before.cursorSourcePixels.x - at.cursorSourcePixels.x) < 4.0);
        CHECK(std::abs(before.cursorSourcePixels.y - at.cursorSourcePixels.y) < 4.0);
        CHECK(std::abs(at.cursorSourcePixels.x - after.cursorSourcePixels.x) < 4.0);
        CHECK(std::abs(at.cursorSourcePixels.y - after.cursorSourcePixels.y) < 4.0);
        CHECK(std::abs(before.camera.centerPixels.x - at.camera.centerPixels.x) < 4.0);
        CHECK(std::abs(before.camera.centerPixels.y - at.camera.centerPixels.y) < 4.0);
        CHECK(std::abs(at.camera.centerPixels.x - after.camera.centerPixels.x) < 4.0);
        CHECK(std::abs(at.camera.centerPixels.y - after.camera.centerPixels.y) < 4.0);
        CHECK(std::abs(before.camera.zoom - at.camera.zoom) < 0.1);
        CHECK(std::abs(at.camera.zoom - after.camera.zoom) < 0.1);
    }
}

AETHER_TEST("showcase: quintic camera moves settle with near-zero velocity at choreography cuts") {
    auto config = MakeConfig();
    CHECK_EQ(config.timing.easingProfile, EasingProfile::SmoothQuintic);
    auto sequence = CinematicSequence::Create(config);
    CHECK(sequence.ok());

    constexpr double delta = 0.001;
    const double stationaryBoundaries[] = {2.0, 3.1, 4.5, 6.9, 7.9};
    for (const double boundary : stationaryBoundaries) {
        const auto before = sequence->Evaluate(boundary - delta);
        const auto at = sequence->Evaluate(boundary);
        const auto after = sequence->Evaluate(boundary + delta);
        const double cursorSpeedBefore = std::hypot(at.cursorSourcePixels.x - before.cursorSourcePixels.x,
                                                    at.cursorSourcePixels.y - before.cursorSourcePixels.y) / delta;
        const double cursorSpeedAfter = std::hypot(after.cursorSourcePixels.x - at.cursorSourcePixels.x,
                                                   after.cursorSourcePixels.y - at.cursorSourcePixels.y) / delta;
        const double cameraSpeedBefore = std::hypot(at.camera.centerPixels.x - before.camera.centerPixels.x,
                                                    at.camera.centerPixels.y - before.camera.centerPixels.y) / delta;
        const double cameraSpeedAfter = std::hypot(after.camera.centerPixels.x - at.camera.centerPixels.x,
                                                   after.camera.centerPixels.y - at.camera.centerPixels.y) / delta;
        CHECK(cursorSpeedBefore < 0.1);
        CHECK(cursorSpeedAfter < 0.1);
        CHECK(cameraSpeedBefore < 0.1);
        CHECK(cameraSpeedAfter < 0.1);
        CHECK(std::abs(at.camera.zoom - before.camera.zoom) / delta < 0.001);
        CHECK(std::abs(after.camera.zoom - at.camera.zoom) / delta < 0.001);
        const double cursorAcceleration = std::hypot(
            after.cursorSourcePixels.x - 2.0 * at.cursorSourcePixels.x + before.cursorSourcePixels.x,
            after.cursorSourcePixels.y - 2.0 * at.cursorSourcePixels.y + before.cursorSourcePixels.y) / (delta * delta);
        const double cameraAcceleration = std::hypot(
            after.camera.centerPixels.x - 2.0 * at.camera.centerPixels.x + before.camera.centerPixels.x,
            after.camera.centerPixels.y - 2.0 * at.camera.centerPixels.y + before.camera.centerPixels.y) / (delta * delta);
        const double zoomAcceleration =
            std::abs(after.camera.zoom - 2.0 * at.camera.zoom + before.camera.zoom) / (delta * delta);
        CHECK(cursorAcceleration < 30.0);
        CHECK(cameraAcceleration < 30.0);
        CHECK(zoomAcceleration < 1.0);
    }

    SequenceEvents windowVisible;
    windowVisible.visualStudioCodeWindowVisibleAt = sequence->LaunchAtSeconds() + 0.9;
    const double revealStart = *windowVisible.visualStudioCodeWindowVisibleAt;
    const auto beforeReveal = sequence->Evaluate(revealStart - delta, windowVisible);
    const auto reveal = sequence->Evaluate(revealStart, windowVisible);
    const auto afterReveal = sequence->Evaluate(revealStart + delta, windowVisible);
    CHECK_EQ(reveal.phase, SequencePhase::RevealWindow);
    CHECK(std::abs(reveal.camera.centerPixels.x - beforeReveal.camera.centerPixels.x) / delta < 0.1);
    CHECK(std::abs(afterReveal.camera.centerPixels.x - reveal.camera.centerPixels.x) / delta < 0.1);
    CHECK(std::abs(reveal.camera.zoom - beforeReveal.camera.zoom) / delta < 0.01);
    CHECK(std::abs(afterReveal.camera.zoom - reveal.camera.zoom) / delta < 0.01);
    CHECK(std::abs(afterReveal.camera.centerPixels.x - 2.0 * reveal.camera.centerPixels.x +
                   beforeReveal.camera.centerPixels.x) / (delta * delta) < 30.0);
    CHECK(std::abs(afterReveal.camera.zoom - 2.0 * reveal.camera.zoom + beforeReveal.camera.zoom) /
              (delta * delta) < 1.0);
}

AETHER_TEST("showcase: selected art-directed cubic curve overrides the default quintic profile") {
    auto defaultSequence = CinematicSequence::Create(MakeConfig());
    CHECK(defaultSequence.ok());
    auto customConfig = MakeConfig();
    customConfig.timing.easingProfile = EasingProfile::CustomCubicBezier;
    customConfig.timing.approachCurve = {0.8f, 0.0f, 0.2f, 1.0f};
    auto customSequence = CinematicSequence::Create(customConfig);
    CHECK(customSequence.ok());

    const auto smooth = defaultSequence->Evaluate(2.44);
    const auto artDirected = customSequence->Evaluate(2.44);
    CHECK_EQ(artDirected.phase, SequencePhase::CursorApproach);
    CHECK(std::abs(artDirected.phaseProgress - smooth.phaseProgress) > 0.1);

    constexpr double delta = 0.0001;
    const auto atStart = customSequence->Evaluate(2.0);
    const auto afterStart = customSequence->Evaluate(2.0 + delta);
    const auto beforeEnd = customSequence->Evaluate(3.1 - delta);
    const auto atEnd = customSequence->Evaluate(3.1);
    const double startSpeed = std::hypot(afterStart.cursorSourcePixels.x - atStart.cursorSourcePixels.x,
                                         afterStart.cursorSourcePixels.y - atStart.cursorSourcePixels.y) / delta;
    const double endSpeed = std::hypot(atEnd.cursorSourcePixels.x - beforeEnd.cursorSourcePixels.x,
                                       atEnd.cursorSourcePixels.y - beforeEnd.cursorSourcePixels.y) / delta;
    CHECK(startSpeed < 0.1);
    CHECK(endSpeed < 0.1);

    auto unsafeConfig = customConfig;
    unsafeConfig.timing.approachCurve = {0.8f, 0.4f, 0.2f, 1.0f};
    CHECK_FALSE(CinematicSequence::Create(unsafeConfig).ok());
}

AETHER_TEST("showcase: temporal motion-blur samples are centered and bounded to the frame exposure") {
    auto samples = CinematicSequence::BuildMotionBlurSamples(1.0);
    CHECK(samples.ok());
    CHECK_EQ(samples->count, TemporalSampleSet::MaximumSamples);
    CHECK(Near(samples->exposureSeconds, 1.0 / 120.0));
    CHECK(Near(samples->timestamps[0], 1.0 - samples->exposureSeconds / 2.0 +
                                      samples->exposureSeconds / 16.0));
    CHECK(Near(samples->timestamps[7], 1.0 + samples->exposureSeconds / 2.0 -
                                      samples->exposureSeconds / 16.0));
    double average = 0.0;
    for (std::size_t index = 0; index < samples->count; ++index) {
        CHECK(samples->timestamps[index] >= 0.0);
        if (index > 0) CHECK(samples->timestamps[index] > samples->timestamps[index - 1]);
        average += samples->timestamps[index];
    }
    CHECK(Near(average / static_cast<double>(samples->count), 1.0));

    auto firstFrame = CinematicSequence::BuildMotionBlurSamples(0.0);
    CHECK(firstFrame.ok());
    CHECK(firstFrame->timestamps[0] >= 0.0);
}

AETHER_TEST("showcase: temporal motion-blur sampler rejects invalid frame and shutter settings") {
    CHECK_FALSE(CinematicSequence::BuildMotionBlurSamples(-0.1).ok());
    CHECK_FALSE(CinematicSequence::BuildMotionBlurSamples(0.0, 0.0).ok());
    CHECK_FALSE(CinematicSequence::BuildMotionBlurSamples(0.0, 60.0, 361.0).ok());
    CHECK_FALSE(CinematicSequence::BuildMotionBlurSamples(0.0, 60.0, 180.0, 0).ok());
    CHECK_FALSE(CinematicSequence::BuildMotionBlurSamples(0.0, 60.0, 180.0,
                                                          TemporalSampleSet::MaximumSamples + 1).ok());
}

AETHER_TEST("showcase: dock close-up caps raster magnification and preserves its aspect ratio") {
    auto sequence = CinematicSequence::Create(MakeConfig());
    CHECK(sequence.ok());
    const auto frame = sequence->Evaluate(sequence->LaunchAtSeconds());
    const auto projected = CinematicSequence::MapSourceToOutput(MakeConfig().dockBoundsPixels, frame.camera,
                                                                  MakeConfig().outputPixels);
    CHECK_EQ(frame.phase, SequencePhase::FocusVisualStudioCode);
    CHECK(Near(frame.camera.zoom, 3.0));
    CHECK(projected.top >= 48.0);
    CHECK(Near(projected.bottom, 1020.0));
    CHECK(Near((projected.right - projected.left) / (projected.bottom - projected.top),
               (520.0 / 70.0)));
}

AETHER_TEST("showcase: configured close-up is constrained by the Dock vertical fit") {
    auto config = MakeConfig();
    config.timing.maximumDockZoom = 30.0;
    auto sequence = CinematicSequence::Create(config);
    CHECK(sequence.ok());
    const auto frame = sequence->Evaluate(sequence->LaunchAtSeconds());
    const auto projected = CinematicSequence::MapSourceToOutput(config.dockBoundsPixels, frame.camera,
                                                                  config.outputPixels);
    CHECK(Near(projected.top, config.timing.topMarginPixels));
    CHECK(projected.bottom < config.outputPixels.height);
    const double viewportBottom = frame.camera.centerPixels.y + config.outputPixels.height / (2.0 * frame.camera.zoom);
    CHECK(Near(viewportBottom, config.sourcePixels.height));
    CHECK(frame.camera.zoom < config.timing.maximumDockZoom);
}

AETHER_TEST("showcase: horizontal camera follows the cursor and settles exactly on the VS Code icon") {
    auto sequence = CinematicSequence::Create(MakeConfig());
    CHECK(sequence.ok());
    const double trackStart = 2.0 + 1.1 + 1.4;
    const double trackEnd = trackStart + 2.4;
    const auto mid = sequence->Evaluate(trackStart + 1.2);
    const auto settled = sequence->Evaluate(trackEnd + sequence->Config().timing.focusPause);
    CHECK_EQ(mid.phase, SequencePhase::HorizontalTrack);
    CHECK(mid.cursorSourcePixels.x > 700.0);
    CHECK(mid.cursorSourcePixels.x < 1110.0);
    CHECK(Near(mid.cursorOutputPixels.x, 1920.0 * 0.54));
    CHECK_EQ(settled.phase, SequencePhase::FocusVisualStudioCode);
    CHECK(Near(settled.cursorSourcePixels.x, 1110.0));
    CHECK(Near(settled.cursorSourcePixels.y, 1025.0));
    CHECK(Near(settled.cursorOutputPixels.x, 1920.0 * 0.54));
    CHECK(settled.launchVisualStudioCode);
}

AETHER_TEST("showcase: launch waits for the configured focus pause and reveal waits for the real window event") {
    auto sequence = CinematicSequence::Create(MakeConfig());
    CHECK(sequence.ok());
    const double launchAt = sequence->LaunchAtSeconds();
    auto atLaunch = sequence->Evaluate(launchAt);
    CHECK_EQ(atLaunch.phase, SequencePhase::FocusVisualStudioCode);
    CHECK(atLaunch.launchVisualStudioCode);
    CHECK_FALSE(atLaunch.visualStudioCodeWindowVisible);
    CHECK_FALSE(sequence->DurationSeconds(SequenceEvents{}).has_value());

    SequenceEvents launchConsumed;
    launchConsumed.visualStudioCodeLaunchRequested = true;
    CHECK_FALSE(sequence->Evaluate(launchAt + 0.25, launchConsumed).launchVisualStudioCode);

    SequenceEvents events;
    events.visualStudioCodeWindowVisibleAt = launchAt + 0.9;
    auto waiting = sequence->Evaluate(launchAt + 0.5, events);
    auto reveal = sequence->Evaluate(launchAt + 0.9, events);
    CHECK_EQ(waiting.phase, SequencePhase::FocusVisualStudioCode);
    CHECK_EQ(reveal.phase, SequencePhase::RevealWindow);
    CHECK(reveal.visualStudioCodeWindowVisible);
    CHECK(Near(reveal.camera.zoom, 3.0));
    const double endAt = *sequence->DurationSeconds(events);
    const auto finished = sequence->Evaluate(endAt, events);
    CHECK_EQ(finished.phase, SequencePhase::FinalHold);
    CHECK(finished.finished);
    CHECK(Near(finished.camera.zoom, 1.0));
}

AETHER_TEST("showcase: output resolution changes preserve the source-coordinate composition") {
    auto config = MakeConfig();
    config.outputPixels = {3840.0, 2160.0};
    auto sequence = CinematicSequence::Create(config);
    CHECK(sequence.ok());
    const auto state = sequence->Evaluate(sequence->LaunchAtSeconds());
    const auto projected = CinematicSequence::MapSourceToOutput(config.dockBoundsPixels, state.camera, config.outputPixels);
    CHECK(Near(projected.top, 1620.0));
    CHECK(Near(projected.bottom, 2040.0));
    CHECK(Near(state.camera.zoom, 6.0));
    CHECK(Near(state.cursorOutputPixels.x, 3840.0 * 0.54));
}

AETHER_TEST("showcase: control-row close-up preserves the complete material footer within genuine source pixels") {
    auto config = MakeConfig();
    config.dockBoundsPixels = {678.0, 987.0, 1242.0, 1047.0};
    config.visualStudioCodeIconPixels = {1212.0, 1017.0};
    auto sequence = CinematicSequence::Create(config);
    REQUIRE(sequence.ok());
    const auto focused = sequence->Evaluate(sequence->LaunchAtSeconds());
    // The real material extends 29 source pixels below the accessible control row. Its lower
    // border and the icons must remain visible with a small breathing space, without stretching
    // the source image's bottom row to invent extra backdrop.
    const auto materialBottom = CinematicSequence::MapSourceToOutput(Point{1212.0, 1076.0}, focused.camera,
                                                                      config.outputPixels);
    CHECK(materialBottom.y <= config.outputPixels.height - 8.0);
    CHECK(materialBottom.y > config.outputPixels.height * 0.8);
    const double viewportBottom = focused.camera.centerPixels.y + config.outputPixels.height / (2.0 * focused.camera.zoom);
    CHECK(viewportBottom <= config.sourcePixels.height);
    const auto controls = CinematicSequence::MapSourceToOutput(config.dockBoundsPixels, focused.camera, config.outputPixels);
    CHECK(controls.bottom < config.outputPixels.height * 0.92);
}

AETHER_TEST("showcase: approach visibly detours left and camera keeps zooming during icon traversal") {
    const auto config = MakeConfig();
    auto sequence = CinematicSequence::Create(config);
    REQUIRE(sequence.ok());
    const auto approach = sequence->Evaluate(config.timing.desktopHold + config.timing.cursorApproach * 0.5);
    CHECK(approach.cursorSourcePixels.x < config.dockBoundsPixels.left);
    const double trackStart = config.timing.desktopHold + config.timing.cursorApproach + config.timing.dockDive;
    const auto begin = sequence->Evaluate(trackStart);
    const auto middle = sequence->Evaluate(trackStart + config.timing.horizontalTrack * 0.5);
    const auto end = sequence->Evaluate(trackStart + config.timing.horizontalTrack);
    CHECK(begin.camera.zoom < middle.camera.zoom);
    CHECK(middle.camera.zoom < end.camera.zoom);
    CHECK(begin.cursorSourcePixels.x < middle.cursorSourcePixels.x);
    CHECK(middle.cursorSourcePixels.x < end.cursorSourcePixels.x);
    CHECK(Near(end.camera.zoom, config.timing.maximumDockZoom));
}

AETHER_TEST("showcase: observed app closure gates the final empty-desktop hold") {
    auto config = MakeConfig();
    config.timing.postCloseHold = 3.0;
    auto sequence = CinematicSequence::Create(config);
    REQUIRE(sequence.ok());
    SequenceEvents events;
    events.visualStudioCodeLaunchRequested = true;
    CHECK_FALSE(sequence->CloseAtSeconds(events).has_value());
    CHECK_FALSE(sequence->DurationSeconds(events).has_value());
    events.visualStudioCodeWindowVisibleAt = sequence->LaunchAtSeconds() + 0.9;
    const auto closeAt = sequence->CloseAtSeconds(events);
    REQUIRE(closeAt.has_value());
    CHECK(Near(*closeAt, *events.visualStudioCodeWindowVisibleAt + config.timing.cameraReveal + config.timing.finalHold));
    CHECK_FALSE(sequence->Evaluate(*closeAt - 0.001, events).closeVisualStudioCode);
    const auto requestClose = sequence->Evaluate(*closeAt, events);
    CHECK_EQ(requestClose.phase, SequencePhase::AwaitWindowClose);
    CHECK(requestClose.closeVisualStudioCode);
    CHECK(requestClose.visualStudioCodeWindowVisible);
    CHECK_FALSE(requestClose.finished);
    CHECK_FALSE(sequence->DurationSeconds(events).has_value());
    events.visualStudioCodeCloseRequested = true;
    CHECK_FALSE(sequence->Evaluate(*closeAt + 0.1, events).closeVisualStudioCode);
    events.visualStudioCodeWindowClosedAt = *closeAt + 0.4;
    const auto closed = sequence->Evaluate(*events.visualStudioCodeWindowClosedAt, events);
    CHECK_EQ(closed.phase, SequencePhase::PostCloseHold);
    CHECK_FALSE(closed.visualStudioCodeWindowVisible);
    CHECK_FALSE(closed.finished);
    const auto duration = sequence->DurationSeconds(events);
    REQUIRE(duration.has_value());
    CHECK(Near(*duration, *events.visualStudioCodeWindowClosedAt + 3.0));
    CHECK_FALSE(sequence->Evaluate(*duration - 0.001, events).finished);
    CHECK(sequence->Evaluate(*duration, events).finished);
    CHECK(Near(sequence->Evaluate(*duration, events).camera.zoom, 1.0));
    events.visualStudioCodeWindowClosedAt = *closeAt - 0.001;
    CHECK_FALSE(sequence->DurationSeconds(events).has_value());
}

AETHER_TEST("showcase: ready-gated reveal returns the cursor smoothly and leaves a neutral empty ending") {
    auto config = MakeConfig();
    config.timing.postCloseHold = 3.0;
    auto sequence = CinematicSequence::Create(config);
    REQUIRE(sequence.ok());
    SequenceEvents events;
    events.visualStudioCodeLaunchRequested = true;
    events.visualStudioCodeWindowVisibleAt = sequence->LaunchAtSeconds() + 0.9;
    const double revealStart = *events.visualStudioCodeWindowVisibleAt;
    const double revealEnd = revealStart + config.timing.cameraReveal;
    const auto first = sequence->Evaluate(revealStart, events);
    const auto middle = sequence->Evaluate(revealStart + config.timing.cameraReveal * 0.5, events);
    const auto returned = sequence->Evaluate(revealEnd, events);
    CHECK(Near(first.cursorSourcePixels.x, config.visualStudioCodeIconPixels.x));
    CHECK(Near(first.cursorSourcePixels.y, config.visualStudioCodeIconPixels.y));
    CHECK(middle.cursorSourcePixels.x < first.cursorSourcePixels.x);
    CHECK(middle.cursorSourcePixels.x > config.cursorStartPixels.x);
    CHECK(middle.cursorSourcePixels.y < first.cursorSourcePixels.y);
    CHECK(middle.cursorSourcePixels.y > config.cursorStartPixels.y);
    CHECK(Near(returned.cursorSourcePixels.x, config.cursorStartPixels.x));
    CHECK(Near(returned.cursorSourcePixels.y, config.cursorStartPixels.y));
    constexpr double delta = 0.001;
    for (const double boundary : {revealStart, revealEnd}) {
        const auto before = sequence->Evaluate(boundary - delta, events);
        const auto at = sequence->Evaluate(boundary, events);
        const auto after = sequence->Evaluate(boundary + delta, events);
        CHECK(std::hypot(at.cursorSourcePixels.x - before.cursorSourcePixels.x,
                         at.cursorSourcePixels.y - before.cursorSourcePixels.y) / delta < 0.1);
        CHECK(std::hypot(after.cursorSourcePixels.x - at.cursorSourcePixels.x,
                         after.cursorSourcePixels.y - at.cursorSourcePixels.y) / delta < 0.1);
    }
    events.visualStudioCodeCloseRequested = true;
    events.visualStudioCodeWindowClosedAt = *sequence->CloseAtSeconds(events) + 0.4;
    const auto duration = sequence->DurationSeconds(events);
    REQUIRE(duration.has_value());
    const auto ended = sequence->Evaluate(*duration, events);
    CHECK(ended.finished);
    CHECK(Near(ended.cursorSourcePixels.x, config.cursorStartPixels.x));
    CHECK(Near(ended.cursorSourcePixels.y, config.cursorStartPixels.y));
}

AETHER_TEST("showcase: scaled delivery stays continuous through the dive and settled reveal") {
    for (const Size output : {Size{3840.0, 2160.0}, Size{1280.0, 720.0}}) {
        auto config = MakeConfig();
        config.outputPixels = output;
        auto sequence = CinematicSequence::Create(config);
        CHECK(sequence.ok());
        const double desktopScale = output.width / config.sourcePixels.width;
        CHECK(Near(sequence->Evaluate(0.0).camera.zoom, desktopScale));
        SequenceEvents events;
        events.visualStudioCodeWindowVisibleAt = sequence->LaunchAtSeconds() + 0.9;
        constexpr double delta = 0.0001;
        const double diveBegin = config.timing.desktopHold + config.timing.cursorApproach;
        const double revealEnd = *events.visualStudioCodeWindowVisibleAt + config.timing.cameraReveal;
        for (const double boundary : {diveBegin, revealEnd}) {
            const auto before = sequence->Evaluate(boundary - delta, events);
            const auto at = sequence->Evaluate(boundary, events);
            const auto after = sequence->Evaluate(boundary + delta, events);
            CHECK(std::abs(before.camera.zoom - at.camera.zoom) < 0.00001);
            CHECK(std::abs(after.camera.zoom - at.camera.zoom) < 0.00001);
            CHECK(std::hypot(before.camera.centerPixels.x - at.camera.centerPixels.x,
                             before.camera.centerPixels.y - at.camera.centerPixels.y) < 0.00001);
            CHECK(std::hypot(after.camera.centerPixels.x - at.camera.centerPixels.x,
                             after.camera.centerPixels.y - at.camera.centerPixels.y) < 0.00001);
        }
        CHECK(Near(sequence->Evaluate(revealEnd, events).camera.zoom, desktopScale));
    }
}

AETHER_TEST("showcase: invalid geometry and event times fail closed") {
    auto config = MakeConfig();
    config.dockBoundsPixels.right = config.sourcePixels.width + 1.0;
    CHECK_FALSE(CinematicSequence::Create(config).ok());

    config = MakeConfig();
    config.outputPixels.width = 0.0;
    CHECK_FALSE(CinematicSequence::Create(config).ok());

    auto sequence = CinematicSequence::Create(MakeConfig());
    CHECK(sequence.ok());
    SequenceEvents invalidEvent;
    invalidEvent.visualStudioCodeWindowVisibleAt = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(sequence->DurationSeconds(invalidEvent).has_value());
    const auto invalidTime = sequence->Evaluate(std::numeric_limits<double>::quiet_NaN());
    CHECK_EQ(invalidTime.phase, SequencePhase::DesktopHold);
    CHECK(std::isfinite(invalidTime.camera.centerPixels.x));
    CHECK(std::isfinite(invalidTime.camera.zoom));

    config = MakeConfig();
    config.timing.easingProfile = static_cast<EasingProfile>(-1);
    CHECK_FALSE(CinematicSequence::Create(config).ok());

    config = MakeConfig();
    config.timing.maximumDockZoom = 0.9;
    CHECK_FALSE(CinematicSequence::Create(config).ok());
    config.timing.maximumDockZoom = std::numeric_limits<double>::infinity();
    CHECK_FALSE(CinematicSequence::Create(config).ok());
}
