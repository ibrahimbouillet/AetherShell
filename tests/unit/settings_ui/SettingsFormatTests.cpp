// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// People-facing numbers in Settings: percentages instead of raw fractions, trimmed decimals, exact
// round trips of the stored values and the slider mapping.
#include "AetherTest.h"

#include "settings/ui/SettingsFormat.h"

#include <cmath>

using namespace aether::settings::ui;

namespace {

NumberFormat Percent() {
    NumberFormat format;
    format.minimum = 30;
    format.maximum = 100;
    format.scale = 100;
    format.unit = L"%";
    return format;
}

NumberFormat Factor() {
    NumberFormat format;
    format.minimum = 1.0;
    format.maximum = 2.0;
    format.step = 0.05;
    format.decimals = 2;
    format.unit = L"×";
    return format;
}

}  // namespace

AETHER_TEST("settings format: fractions show as percentages and factors without trailing zeros") {
    CHECK_EQ(FormatNumber(0.80, Percent()), std::wstring(L"80"));
    CHECK_EQ(FormatNumber(0.3, Percent()), std::wstring(L"30"));
    CHECK_EQ(FormatNumber(1.45, Factor()), std::wstring(L"1.45"));
    CHECK_EQ(FormatNumber(1.5, Factor()), std::wstring(L"1.5"));
    CHECK_EQ(FormatNumber(2.0, Factor()), std::wstring(L"2"));
    NumberFormat pixels;
    pixels.minimum = 32;
    pixels.maximum = 96;
    CHECK_EQ(FormatNumber(64, pixels), std::wstring(L"64"));
    CHECK_EQ(FormatNumber(-0.0001, pixels), std::wstring(L"0"));  // never "-0"
    CHECK(FormatNumber(std::nan(""), pixels).empty());
}

AETHER_TEST("settings format: parsing returns the exact stored value and accepts the unit") {
    const auto percent = Percent();
    CHECK(std::fabs(*ParseNumber(L"65", percent) - 0.65) < 1e-12);
    CHECK(std::fabs(*ParseNumber(L" 65 % ", percent) - 0.65) < 1e-12);
    CHECK(std::fabs(*ParseNumber(L"1.25", Factor()) - 1.25) < 1e-12);
    CHECK(std::fabs(*ParseNumber(L"1.25x", Factor()) - 1.25) < 1e-12);
    CHECK(std::fabs(*ParseNumber(L"1.25×", Factor()) - 1.25) < 1e-12);
    // Incomplete or non-numeric input is rejected rather than guessed.
    CHECK_FALSE(ParseNumber(L"", percent).has_value());
    CHECK_FALSE(ParseNumber(L"abc", percent).has_value());
    CHECK_FALSE(ParseNumber(L"6 5", percent).has_value());
    CHECK_FALSE(ParseNumber(L"inf", percent).has_value());
    // Out-of-range values are returned as typed; validation corrects and reports them.
    CHECK(std::fabs(*ParseNumber(L"150", percent) - 1.5) < 1e-12);
    CHECK_FALSE(InRange(1.5, percent));
    CHECK(InRange(0.3, percent));
    CHECK(InRange(1.0, percent));
}

AETHER_TEST("settings format: slider positions round-trip through the displayed steps") {
    const auto factor = Factor();
    CHECK_EQ(SliderSteps(factor), 20);
    CHECK_EQ(SliderPosition(1.45, factor), 9);
    CHECK(std::fabs(StoredFromSlider(9, factor) - 1.45) < 1e-9);
    CHECK_EQ(SliderPosition(5.0, factor), 20);  // clamped
    CHECK(std::fabs(StoredFromSlider(99, factor) - 2.0) < 1e-9);
    const auto percent = Percent();
    CHECK_EQ(SliderSteps(percent), 70);
    CHECK_EQ(SliderPosition(0.8, percent), 50);
    CHECK(std::fabs(StoredFromSlider(50, percent) - 0.8) < 1e-9);
    CHECK_EQ(RangeText(percent), std::wstring(L"From 30 to 100 %"));
}
