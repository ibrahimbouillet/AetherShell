// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FR-041 handwriting: original pen paths (authored in tools/handwriting/strokes.py, not a font and not
// traced from one) composed into continuous ink. A word is one line; its dots, crossbars and accents
// follow when it is finished. Each point carries the time the pen reaches it and the line width there,
// from a model of a hand: slower through tight curves and at the start and end of a stroke, a little
// uneven, thicker when slow, with real pause-and-travel pen lifts. Pure, with no window or device.
#pragma once

#include "core/Result.h"
#include "settings/Settings.h"

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aether::welcome {

struct PenPoint {
    float x = 0.0f, y = 0.0f;  // DIPs, y downwards
    float time = 0.0f;         // seconds from the start of the writing
    float width = 0.0f;        // DIPs
};
struct PenStroke { std::vector<PenPoint> points; };

struct PenPosition {
    float x = 0.0f, y = 0.0f;
    bool lifted = false;  // travelling between strokes
};

// What the greeting shows. An unsupported script is never guessed at or dropped: `fallbackName` is the
// name in readable type, revealed behind the moving pen.
struct WelcomeScene {
    std::vector<PenStroke> strokes;
    std::wstring fallbackName;
    float fallbackLeft = 0.0f, fallbackTop = 0.0f, fallbackWidth = 0.0f, fallbackHeight = 0.0f, fallbackSize = 0.0f;
    float width = 0.0f, height = 0.0f;  // the ink's bounds; strokes start at the origin
    float inkLength = 0.0f;             // measured along the paths, DIPs
    float inkDuration = 0.0f;           // seconds until the last stroke ends
    float duration = 0.0f;              // seconds, including the fallback's reveal
    float penWidth = 0.0f;              // DIPs, the nominal line width at this size
    int penLifts = 0;
};

// Whether every character of `text` has pen strokes (accented Latin included, after decomposition).
[[nodiscard]] bool CanWrite(std::wstring_view text);
// Ink for a greeting and an optional name, fitted within `maxWidth` x `maxHeight` DIPs: one line when it
// fits, two when a long name needs them. Deterministic: the same input writes the same way.
[[nodiscard]] Result<WelcomeScene> ComposeScene(std::wstring_view greeting, std::wstring_view name, float maxWidth, float maxHeight);
// Where the pen is at `time`; nullopt before it starts and after it finished.
[[nodiscard]] std::optional<PenPosition> PenAt(const WelcomeScene& scene, float time) noexcept;

// "Salut" or "Hello": the explicit choice, otherwise the Windows display language.
[[nodiscard]] std::wstring GreetingWord(settings::GreetingLanguage language, LANGID displayLanguage);

}  // namespace aether::welcome
