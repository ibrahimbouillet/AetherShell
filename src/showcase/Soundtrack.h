// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <cstdint>
#include <vector>

namespace aether::showcase {

// Deterministic original stereo score: a quiet harmonic bed, one restrained motion sweep, and a
// single soft focus chime. No external music or sound assets are used.
[[nodiscard]] Result<std::vector<std::int16_t>> CreateShowcaseSoundtrack(
    double durationSeconds, double focusTimeSeconds, std::uint32_t sampleRate = 48'000);

// Bind the sweep to the authored camera track rather than fixed wall-clock timestamps. Equal
// sweep endpoints intentionally omit that cue; the harmonic bed and focus chime remain.
[[nodiscard]] Result<std::vector<std::int16_t>> CreateShowcaseSoundtrack(
    double durationSeconds, double focusTimeSeconds, double sweepStartSeconds, double sweepEndSeconds,
    std::uint32_t sampleRate = 48'000);

}  // namespace aether::showcase
