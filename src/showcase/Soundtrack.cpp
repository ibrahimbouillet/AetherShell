// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/Soundtrack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>

namespace aether::showcase {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = 2.0 * kPi;

struct Chord final { std::array<double, 4> notes; };

constexpr std::array<Chord, 4> kChords{{
    {{{130.8128, 164.8138, 196.0000, 246.9417}}},  // C major seventh
    {{{87.3071, 110.0000, 130.8128, 164.8138}}},   // F major seventh
    {{{110.0000, 130.8128, 164.8138, 196.0000}}},  // A minor seventh
    {{{98.0000, 123.4708, 146.8324, 196.0000}}},   // G sixth
}};

double Smooth01(double value) noexcept {
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double ChordTone(const Chord& chord, double seconds) noexcept {
    double sum = 0.0;
    constexpr std::array<double, 4> weights{0.45, 0.30, 0.22, 0.15};
    for (std::size_t index = 0; index < chord.notes.size(); ++index) {
        const double phase = kTwoPi * chord.notes[index] * seconds;
        sum += weights[index] * (std::sin(phase) + 0.19 * std::sin(phase * 2.0 + 0.17) +
                                0.055 * std::sin(phase * 3.0 + 0.41));
    }
    return sum;
}

}  // namespace

Result<std::vector<std::int16_t>> CreateShowcaseSoundtrack(double durationSeconds, double focusTimeSeconds,
                                                           std::uint32_t sampleRate) {
    const double sweepEnd = std::max(0.0, focusTimeSeconds - 0.8);
    return CreateShowcaseSoundtrack(durationSeconds, focusTimeSeconds,
                                    std::max(0.0, sweepEnd - 2.4), sweepEnd, sampleRate);
}

Result<std::vector<std::int16_t>> CreateShowcaseSoundtrack(double durationSeconds, double focusTimeSeconds,
                                                           double sweepStartSeconds, double sweepEndSeconds,
                                                           std::uint32_t sampleRate) {
    if (!std::isfinite(durationSeconds) || durationSeconds < 1.0 || durationSeconds > 180.0 ||
        !std::isfinite(focusTimeSeconds) || focusTimeSeconds < 0.0 || focusTimeSeconds > durationSeconds ||
        !std::isfinite(sweepStartSeconds) || !std::isfinite(sweepEndSeconds) || sweepStartSeconds < 0.0 ||
        sweepEndSeconds < sweepStartSeconds || sweepEndSeconds > durationSeconds ||
        sampleRate != 48'000) {
        return MakeError(ErrorCode::InvalidArgument, "showcase soundtrack duration, cue times or sample rate are invalid");
    }
    const double requestedCount = std::ceil(durationSeconds * sampleRate);
    if (!std::isfinite(requestedCount) || requestedCount > static_cast<double>(std::numeric_limits<std::size_t>::max() / 2U))
        return MakeError(ErrorCode::InvalidArgument, "showcase soundtrack duration is too large");

    try {
        const auto sampleCount = static_cast<std::size_t>(requestedCount);
        std::vector<double> left(sampleCount, 0.0);
        std::vector<double> right(sampleCount, 0.0);
        double peak = 0.0;
        constexpr double kChordSeconds = 3.6;
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const double time = static_cast<double>(index) / sampleRate;
            const auto chordIndex = static_cast<std::size_t>(std::floor(time / kChordSeconds)) % kChords.size();
            const auto nextChord = (chordIndex + 1U) % kChords.size();
            const double intoChord = std::fmod(time, kChordSeconds);
            const double blend = Smooth01((intoChord - (kChordSeconds - 0.65)) / 0.65);
            const double pad = ChordTone(kChords[chordIndex], time) * (1.0 - blend) +
                               ChordTone(kChords[nextChord], time) * blend;

            const double sweepDuration = sweepEndSeconds - sweepStartSeconds;
            const double sweepProgress = sweepDuration > 0.0
                ? std::clamp((time - sweepStartSeconds) / sweepDuration, 0.0, 1.0) : 0.0;
            const double sweepEnvelope = std::sin(kPi * sweepProgress);
            const double sweepTime = std::clamp(time - sweepStartSeconds, 0.0, sweepDuration);
            const double sweepPhase = kTwoPi * (145.0 * sweepTime +
                (sweepDuration > 0.0 ? 0.5 * (350.0 / sweepDuration) * sweepTime * sweepTime : 0.0));
            const double sweep = (sweepDuration > 0.0 && time >= sweepStartSeconds && time <= sweepEndSeconds)
                ? 0.055 * sweepEnvelope * sweepEnvelope *
                    (std::sin(sweepPhase) + 0.27 * std::sin(sweepPhase * 1.53 + 0.4)) : 0.0;

            const double focusDelta = time - focusTimeSeconds;
            const double focus = focusDelta >= 0.0 && focusDelta < 0.28
                ? 0.11 * std::exp(-focusDelta * 16.0) *
                    (std::sin(kTwoPi * 784.0 * focusDelta) + 0.42 * std::sin(kTwoPi * 1174.66 * focusDelta))
                : 0.0;

            const double fadeIn = Smooth01(time / 0.85);
            const double fadeOut = Smooth01((durationSeconds - time) / 1.05);
            const double envelope = fadeIn * fadeOut;
            const double pan = std::clamp(sweepProgress, 0.0, 1.0) * 0.12;
            left[index] = envelope * (0.086 * pad + sweep * (1.0 - pan) + focus);
            right[index] = envelope * (0.086 * pad + sweep * (0.88 + pan) + focus * 0.94);
            peak = std::max({peak, std::abs(left[index]), std::abs(right[index])});
        }
        if (!(peak > 1e-8)) return MakeError(ErrorCode::Validation, "showcase soundtrack contains no audible signal");

        const double outputPeak = 0.30;
        const double gain = outputPeak / peak;
        std::vector<std::int16_t> pcm(sampleCount * 2U);
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const double l = std::clamp(left[index] * gain, -0.98, 0.98);
            const double r = std::clamp(right[index] * gain, -0.98, 0.98);
            pcm[index * 2U] = static_cast<std::int16_t>(std::lround(l * 32767.0));
            pcm[index * 2U + 1U] = static_cast<std::int16_t>(std::lround(r * 32767.0));
        }
        return pcm;
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase soundtrack could not be allocated");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase soundtrack synthesis failed");
    }
}

}  // namespace aether::showcase
