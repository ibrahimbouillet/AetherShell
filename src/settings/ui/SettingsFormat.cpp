// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/ui/SettingsFormat.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace aether::settings::ui {
namespace {

constexpr wchar_t kMultiplication = L'×';

double Displayed(double stored, const NumberFormat& format) noexcept { return stored * format.scale; }

std::wstring_view Trim(std::wstring_view text) noexcept {
    while (!text.empty() && std::iswspace(text.front())) text.remove_prefix(1);
    while (!text.empty() && std::iswspace(text.back())) text.remove_suffix(1);
    return text;
}

}  // namespace

std::wstring FormatNumber(double stored, const NumberFormat& format) {
    if (!std::isfinite(stored)) return {};
    const int decimals = std::clamp(format.decimals, 0, 6);
    const double factor = std::pow(10.0, decimals);
    double value = std::round(Displayed(stored, format) * factor) / factor;
    if (value == 0.0) value = 0.0;  // no "-0"
    wchar_t buffer[64]{};
    std::swprintf(buffer, 64, L"%.*f", decimals, value);
    std::wstring text(buffer);
    if (decimals > 0) {
        while (!text.empty() && text.back() == L'0') text.pop_back();
        if (!text.empty() && text.back() == L'.') text.pop_back();
    }
    return text;
}

std::optional<double> ParseNumber(std::wstring_view text, const NumberFormat& format) {
    text = Trim(text);
    if (!format.unit.empty() && text.size() > format.unit.size() &&
        text.substr(text.size() - format.unit.size()) == format.unit) {
        text.remove_suffix(format.unit.size());
    } else if (!text.empty() && (text.back() == L'%' || text.back() == kMultiplication || text.back() == L'x' || text.back() == L'X')) {
        text.remove_suffix(1);
    }
    text = Trim(text);
    if (text.empty()) return std::nullopt;
    const std::wstring copy(text);
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(copy.c_str(), &end);
    if (end == copy.c_str() || *end || !std::isfinite(parsed)) return std::nullopt;
    if (!(format.scale > 0.0) || !std::isfinite(format.scale)) return std::nullopt;
    return parsed / format.scale;
}

int SliderSteps(const NumberFormat& format) noexcept {
    if (!(format.step > 0.0) || !(format.maximum > format.minimum)) return 0;
    return std::max(0, static_cast<int>(std::lround((format.maximum - format.minimum) / format.step)));
}

int SliderPosition(double stored, const NumberFormat& format) noexcept {
    const int steps = SliderSteps(format);
    if (!steps || !std::isfinite(stored)) return 0;
    const double position = (Displayed(stored, format) - format.minimum) / format.step;
    return std::clamp(static_cast<int>(std::lround(position)), 0, steps);
}

double StoredFromSlider(int position, const NumberFormat& format) noexcept {
    const int steps = SliderSteps(format);
    const double displayed = format.minimum + format.step * std::clamp(position, 0, steps);
    return std::min(displayed, format.maximum) / (format.scale > 0.0 ? format.scale : 1.0);
}

bool InRange(double stored, const NumberFormat& format) noexcept {
    if (!std::isfinite(stored)) return false;
    // Compare at the displayed precision so 0.30 * 100 counts as 30.
    const double displayed = std::round(Displayed(stored, format) * 1e6) / 1e6;
    return displayed >= format.minimum && displayed <= format.maximum;
}

std::wstring RangeText(const NumberFormat& format) {
    NumberFormat displayed = format;
    displayed.scale = 1.0;
    std::wstring text = L"From " + FormatNumber(format.minimum, displayed) + L" to " + FormatNumber(format.maximum, displayed);
    if (!format.unit.empty()) text += L" " + format.unit;
    return text;
}

}  // namespace aether::settings::ui
