// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/FrameCompositor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace aether::showcase {
namespace {

struct FloatPixel final {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
};

struct WeightedPixel final {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 0.0;
};

bool Positive(double value) noexcept { return std::isfinite(value) && value > 0.0; }

bool ValidPlane(std::uint32_t width, std::uint32_t height, std::size_t stride,
                std::size_t byteCount) noexcept {
    constexpr std::size_t kBytesPerPixel = 4;
    if (width == 0 || height == 0 || width > std::numeric_limits<std::size_t>::max() / kBytesPerPixel) {
        return false;
    }
    const auto rowBytes = static_cast<std::size_t>(width) * kBytesPerPixel;
    if (stride < rowBytes || (height > 1 && stride > (std::numeric_limits<std::size_t>::max() - rowBytes) /
                                                          static_cast<std::size_t>(height - 1))) {
        return false;
    }
    return byteCount >= stride * static_cast<std::size_t>(height - 1) + rowBytes;
}

double CatmullRomWeight(double value) noexcept {
    constexpr double kB = 0.0;
    constexpr double kC = 0.5;
    const double x = std::abs(value);
    if (x < 1.0) {
        return ((12.0 - 9.0 * kB - 6.0 * kC) * x * x * x +
                (-18.0 + 12.0 * kB + 6.0 * kC) * x * x + (6.0 - 2.0 * kB)) / 6.0;
    }
    if (x < 2.0) {
        return ((-kB - 6.0 * kC) * x * x * x + (6.0 * kB + 30.0 * kC) * x * x +
                (-12.0 * kB - 48.0 * kC) * x + (8.0 * kB + 24.0 * kC)) / 6.0;
    }
    return 0.0;
}

std::uint8_t ToByte(double value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

WeightedPixel ReadMitchell(const RgbaFrameView& frame, double x, double y) noexcept {
    // Coordinates are expressed at pixel centers: (0.5, 0.5) is the center of the top-left pixel.
    const double sampleX = x - 0.5;
    const double sampleY = y - 0.5;
    const auto baseX = static_cast<std::int64_t>(std::floor(sampleX));
    const auto baseY = static_cast<std::int64_t>(std::floor(sampleY));
    WeightedPixel result;
    double totalWeight = 0.0;
    for (std::int64_t row = baseY - 1; row <= baseY + 2; ++row) {
        const double weightY = CatmullRomWeight(sampleY - static_cast<double>(row));
        const auto clampedY = static_cast<std::uint32_t>(std::clamp<std::int64_t>(row, 0, frame.height - 1));
        for (std::int64_t column = baseX - 1; column <= baseX + 2; ++column) {
            const double weight = weightY * CatmullRomWeight(sampleX - static_cast<double>(column));
            const auto clampedX = static_cast<std::uint32_t>(std::clamp<std::int64_t>(column, 0, frame.width - 1));
            const auto offset = static_cast<std::size_t>(clampedY) * frame.rowStrideBytes +
                                static_cast<std::size_t>(clampedX) * 4;
            const double alpha = static_cast<double>(frame.pixels[offset + 3]) / 255.0;
            result.red += static_cast<double>(frame.pixels[offset]) * alpha * weight;
            result.green += static_cast<double>(frame.pixels[offset + 1]) * alpha * weight;
            result.blue += static_cast<double>(frame.pixels[offset + 2]) * alpha * weight;
            result.alpha += static_cast<double>(frame.pixels[offset + 3]) * weight;
            totalWeight += weight;
        }
    }
    if (std::abs(totalWeight) > 1e-12) {
        result.red /= totalWeight;
        result.green /= totalWeight;
        result.blue /= totalWeight;
        result.alpha /= totalWeight;
    }
    const double alpha = std::clamp(result.alpha, 0.0, 255.0);
    result.red = std::clamp(result.red, 0.0, alpha);
    result.green = std::clamp(result.green, 0.0, alpha);
    result.blue = std::clamp(result.blue, 0.0, alpha);
    result.alpha = alpha;
    return result;
}

}  // namespace

Result<CompositeMetrics> FrameCompositor::Render(const CinematicSequence& sequence,
                                                  ITemporalFrameSource& source,
                                                  double frameCenterSeconds,
                                                  const SequenceEvents& events,
                                                  std::span<std::uint8_t> destination,
                                                  std::uint32_t destinationWidth,
                                                  std::uint32_t destinationHeight,
                                                  std::size_t destinationRowStrideBytes,
                                                  const CompositeSettings& settings) noexcept {
    if (!std::isfinite(frameCenterSeconds) || frameCenterSeconds < 0.0 ||
        !Positive(settings.framesPerSecond) || !std::isfinite(settings.shutterAngleDegrees) ||
        settings.shutterAngleDegrees < 0.0 || settings.shutterAngleDegrees > 360.0 ||
        settings.sampleCount == 0 || settings.sampleCount > TemporalSampleSet::MaximumSamples ||
        !Positive(sequence.Config().outputPixels.width) || !Positive(sequence.Config().outputPixels.height) ||
        std::round(sequence.Config().outputPixels.width) != sequence.Config().outputPixels.width ||
        std::round(sequence.Config().outputPixels.height) != sequence.Config().outputPixels.height ||
        sequence.Config().outputPixels.width > std::numeric_limits<std::uint32_t>::max() ||
        sequence.Config().outputPixels.height > std::numeric_limits<std::uint32_t>::max() ||
        destinationWidth != static_cast<std::uint32_t>(sequence.Config().outputPixels.width) ||
        destinationHeight != static_cast<std::uint32_t>(sequence.Config().outputPixels.height) ||
        !ValidPlane(destinationWidth, destinationHeight, destinationRowStrideBytes, destination.size())) {
        return MakeError(ErrorCode::InvalidArgument, "showcase compositor dimensions or sampling settings are invalid");
    }

    const auto start = std::chrono::steady_clock::now();
    const std::size_t sampleCount = settings.motionBlurEnabled && settings.shutterAngleDegrees > 0.0
                                       ? settings.sampleCount
                                       : 1;
    TemporalSampleSet samples;
    if (sampleCount == 1 && (!settings.motionBlurEnabled || settings.shutterAngleDegrees == 0.0)) {
        samples.count = 1;
        samples.timestamps[0] = frameCenterSeconds;
    } else {
        auto planned = CinematicSequence::BuildMotionBlurSamples(frameCenterSeconds, settings.framesPerSecond,
                                                                  settings.shutterAngleDegrees, sampleCount);
        if (!planned) return planned.error();
        samples = std::move(planned).value();
    }

    if (destinationHeight != 0 && static_cast<std::size_t>(destinationWidth) >
                                      std::numeric_limits<std::size_t>::max() / destinationHeight) {
        return MakeError(ErrorCode::InvalidArgument, "showcase compositor output size overflows addressable memory");
    }
    const auto pixelCount = static_cast<std::size_t>(destinationWidth) * destinationHeight;
    try {
        std::vector<FloatPixel> accumulated(pixelCount);
        for (std::size_t sampleIndex = 0; sampleIndex < samples.count; ++sampleIndex) {
            auto frameResult = source.ReadFrameAt(samples.timestamps[sampleIndex]);
            if (!frameResult) return frameResult.error();
            const auto frame = frameResult.value();
            const auto expectedWidth = sequence.Config().sourcePixels.width;
            const auto expectedHeight = sequence.Config().sourcePixels.height;
            if (!ValidPlane(frame.width, frame.height, frame.rowStrideBytes, frame.pixels.size()) ||
                static_cast<double>(frame.width) != expectedWidth || static_cast<double>(frame.height) != expectedHeight) {
                return MakeError(ErrorCode::Validation, "showcase capture frame does not match the configured source geometry");
            }

            const auto state = sequence.Evaluate(samples.timestamps[sampleIndex], events);
            for (std::uint32_t outputY = 0; outputY < destinationHeight; ++outputY) {
                const double sourceY = state.camera.centerPixels.y +
                    ((static_cast<double>(outputY) + 0.5) - sequence.Config().outputPixels.height / 2.0) /
                        state.camera.zoom;
                for (std::uint32_t outputX = 0; outputX < destinationWidth; ++outputX) {
                    const double sourceX = state.camera.centerPixels.x +
                        ((static_cast<double>(outputX) + 0.5) - sequence.Config().outputPixels.width / 2.0) /
                            state.camera.zoom;
                    const auto pixel = ReadMitchell(frame, sourceX, sourceY);
                    const auto index = static_cast<std::size_t>(outputY) * destinationWidth + outputX;
                    auto& target = accumulated[index];
                    target.red += static_cast<float>(pixel.red / 255.0);
                    target.green += static_cast<float>(pixel.green / 255.0);
                    target.blue += static_cast<float>(pixel.blue / 255.0);
                    target.alpha += static_cast<float>(pixel.alpha / 255.0);
                }
            }
        }

        const float divisor = static_cast<float>(samples.count);
        for (std::uint32_t y = 0; y < destinationHeight; ++y) {
            auto* row = destination.data() + static_cast<std::size_t>(y) * destinationRowStrideBytes;
            for (std::uint32_t x = 0; x < destinationWidth; ++x) {
                const auto& pixel = accumulated[static_cast<std::size_t>(y) * destinationWidth + x];
                const double alpha = static_cast<double>(pixel.alpha / divisor);
                const double premultipliedRed = static_cast<double>(pixel.red / divisor);
                const double premultipliedGreen = static_cast<double>(pixel.green / divisor);
                const double premultipliedBlue = static_cast<double>(pixel.blue / divisor);
                const double scale = alpha > 1e-8 ? 1.0 / alpha : 0.0;
                const auto offset = static_cast<std::size_t>(x) * 4;
                row[offset] = ToByte(premultipliedRed * scale * 255.0);
                row[offset + 1] = ToByte(premultipliedGreen * scale * 255.0);
                row[offset + 2] = ToByte(premultipliedBlue * scale * 255.0);
                row[offset + 3] = ToByte(alpha * 255.0);
            }
        }
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase compositor could not allocate the requested frame");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase compositor failed without exposing capture details");
    }

    CompositeMetrics metrics;
    metrics.sourceFrameReads = samples.count;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    metrics.elapsedMicroseconds = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0;
    return metrics;
}

}  // namespace aether::showcase
