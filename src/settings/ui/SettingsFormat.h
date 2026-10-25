// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// How the Settings window shows numeric settings to people: "80" with a "%" unit for an opacity of
// 0.80, "1.45" with "x" for a magnification factor, whole pixels without decimals. The stored values
// stay exact; parsing converts the displayed number back with the same scale.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace aether::settings::ui {

struct NumberFormat {
    double minimum = 0.0;   // displayed units (after scale)
    double maximum = 100.0;
    double step = 1.0;      // slider step in displayed units
    double scale = 1.0;     // displayed = stored * scale
    int decimals = 0;       // at most this many decimals, trailing zeros removed
    std::wstring unit;      // shown after the value field ("px", "%", "x", "min", "h")
};

// `stored` shown with `format`: scaled, rounded to the decimals, trailing zeros trimmed.
[[nodiscard]] std::wstring FormatNumber(double stored, const NumberFormat& format);
// The stored value for displayed `text`, or nullopt when it is not one finite number. Surrounding
// spaces and a trailing unit (the format's unit, '%', 'x' or the multiplication sign) are accepted.
// The result is not clamped: validation corrects out-of-range values and reports it.
[[nodiscard]] std::optional<double> ParseNumber(std::wstring_view text, const NumberFormat& format);
// Slider positions 0..SliderSteps(format) map to minimum..maximum in `step` increments.
[[nodiscard]] int SliderSteps(const NumberFormat& format) noexcept;
[[nodiscard]] int SliderPosition(double stored, const NumberFormat& format) noexcept;
[[nodiscard]] double StoredFromSlider(int position, const NumberFormat& format) noexcept;
// True when the displayed value of `stored` lies within minimum..maximum.
[[nodiscard]] bool InRange(double stored, const NumberFormat& format) noexcept;
// "From 32 to 96 px" for helper text.
[[nodiscard]] std::wstring RangeText(const NumberFormat& format);

}  // namespace aether::settings::ui
