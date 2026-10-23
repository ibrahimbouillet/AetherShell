// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "showcase/GpuFrameCompositor.h"

#include <array>
#include <cstdint>
#include <vector>

using namespace aether;
using namespace aether::showcase;

namespace {

Result<CinematicSequence> SmallSequence() {
    SequenceConfig config;
    config.sourcePixels = {2.0, 2.0};
    config.outputPixels = {2.0, 2.0};
    config.dockBoundsPixels = {0.0, 1.0, 2.0, 2.0};
    config.visualStudioCodeIconPixels = {1.0, 1.0};
    config.cursorStartPixels = {0.0, 0.0};
    config.timing.topMarginPixels = 0.0;
    return CinematicSequence::Create(config);
}

class SyntheticSource final : public ITemporalFrameSource {
public:
    Result<RgbaFrameView> ReadFrameAt(double timestampSeconds) noexcept override {
        lastTimestamp = timestampSeconds;
        const auto& pixels = timestampSeconds < switchAt ? first : second;
        const std::uint64_t identity = identifyContent ? (timestampSeconds < switchAt ? 1U : 2U) : 0U;
        return RgbaFrameView{2, 2, 8, pixels, identity};
    }
    std::array<std::uint8_t, 16> first{};
    std::array<std::uint8_t, 16> second{};
    double switchAt = 0.0;
    double lastTimestamp = 0.0;
    bool identifyContent = false;
};

}  // namespace

AETHER_TEST("showcase GPU compositor: renders a synthetic source frame with exact geometry through WARP or hardware") {
    auto sequence = SmallSequence();
    REQUIRE(sequence.ok());
    auto compositorResult = GpuFrameCompositor::Create();
    REQUIRE(compositorResult.ok());
    auto compositor = std::move(compositorResult).value();
    SyntheticSource source;
    source.first = {255, 0, 0, 255, 0, 255, 0, 255,
                    0, 0, 255, 255, 255, 255, 255, 255};
    source.switchAt = 1.0;
    std::array<std::uint8_t, 16> output{};
    CompositeSettings settings;
    settings.motionBlurEnabled = false;
    auto rendered = compositor.Render(*sequence, source, 0.0, {}, output, 2, 2, 8, settings);
    REQUIRE(rendered.ok());
    CHECK_EQ(rendered->sourceFrameReads, 1U);
    CHECK_EQ(rendered->sourceTextureUploads, 1U);
    CHECK_EQ(output[0], static_cast<std::uint8_t>(255));
    CHECK_EQ(output[1], static_cast<std::uint8_t>(0));
    CHECK_EQ(output[2], static_cast<std::uint8_t>(0));
    CHECK_EQ(output[3], static_cast<std::uint8_t>(255));
    CHECK_EQ(output[12], static_cast<std::uint8_t>(255));
    CHECK_EQ(output[13], static_cast<std::uint8_t>(255));
    CHECK_EQ(output[14], static_cast<std::uint8_t>(255));
}

AETHER_TEST("showcase GPU compositor: blends genuine temporal samples in linear-light sRGB") {
    auto sequence = SmallSequence();
    REQUIRE(sequence.ok());
    auto compositorResult = GpuFrameCompositor::Create();
    REQUIRE(compositorResult.ok());
    auto compositor = std::move(compositorResult).value();
    SyntheticSource source;
    source.first.fill(0);
    for (std::size_t index = 3; index < source.first.size(); index += 4) source.first[index] = 255;
    source.second.fill(255);
    source.switchAt = 0.1;
    std::array<std::uint8_t, 16> output{};
    CompositeSettings settings;
    settings.framesPerSecond = 60.0;
    settings.shutterAngleDegrees = 180.0;
    settings.sampleCount = 8;
    auto rendered = compositor.Render(*sequence, source, 0.1, {}, output, 2, 2, 8, settings);
    REQUIRE(rendered.ok());
    CHECK_EQ(rendered->sourceFrameReads, 8U);
    CHECK_EQ(rendered->sourceTextureUploads, 8U);
    CHECK(output[0] >= 180 && output[0] <= 193);
    CHECK_EQ(output[3], static_cast<std::uint8_t>(255));
}

AETHER_TEST("showcase GPU compositor: caches identified source images without reusing another provider's pixels") {
    auto sequence = SmallSequence();
    REQUIRE(sequence.ok());
    auto compositor = GpuFrameCompositor::Create();
    REQUIRE(compositor.ok());
    SyntheticSource source;
    source.first = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    source.second = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    source.switchAt = 0.5;
    source.identifyContent = true;
    std::array<std::uint8_t, 16> output{};
    auto first = compositor->Render(*sequence, source, 0.2, {}, output, 2, 2, 8);
    REQUIRE(first.ok());
    CHECK_EQ(first->sourceFrameReads, 8U);
    CHECK_EQ(first->sourceTextureUploads, 1U);
    CHECK_EQ(output[0], 255U);
    auto repeated = compositor->Render(*sequence, source, 0.3, {}, output, 2, 2, 8);
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated->sourceTextureUploads, 0U);
    CHECK_EQ(output[0], 255U);
    auto transition = compositor->Render(*sequence, source, 0.5, {}, output, 2, 2, 8);
    REQUIRE(transition.ok());
    CHECK_EQ(transition->sourceTextureUploads, 1U);
    CHECK(output[0] >= 180 && output[0] <= 193);
    CHECK(output[2] >= 180 && output[2] <= 193);

    SyntheticSource differentSource;
    differentSource.first = {0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255};
    differentSource.switchAt = 1.0;
    differentSource.identifyContent = true;
    auto changedProvider = compositor->Render(*sequence, differentSource, 0.2, {}, output, 2, 2, 8);
    REQUIRE(changedProvider.ok());
    CHECK_EQ(changedProvider->sourceTextureUploads, 1U);
    CHECK_EQ(output[0], 0U);
    CHECK_EQ(output[1], 255U);
}
