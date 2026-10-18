// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// DirectWrite text formats keyed by family/size/weight, plus small helpers for
// measuring and drawing single-line UI text. Formats are device-independent and
// survive device loss.
#pragma once

#include "graphics/WinRT.h"

#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace aether::graphics {

struct TextStyle {
    float size = 13.0f;  // DIPs
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT verticalAlignment = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    bool display = false;  // use the display family (titles, large text)
    bool ellipsis = true;  // trim with an ellipsis instead of wrapping
};

class TextCache {
public:
    TextCache() = default;
    void Initialize(IDWriteFactory* factory, std::wstring textFamily, std::wstring displayFamily);

    [[nodiscard]] IDWriteTextFormat* Format(const TextStyle& style);

    // Width/height of `text` laid out on one line with `style` (DIPs).
    [[nodiscard]] D2D1_SIZE_F Measure(std::wstring_view text, const TextStyle& style, float maxWidth = 10000.0f);

    void Draw(ID2D1DeviceContext* context, std::wstring_view text, const D2D1_RECT_F& rect, const TextStyle& style,
              ID2D1Brush* brush);

    [[nodiscard]] IDWriteFactory* Factory() const noexcept { return factory_.get(); }

private:
    [[nodiscard]] std::wstring ResolveFamily(const std::wstring& preferred) const;

    winrt::com_ptr<IDWriteFactory> factory_;
    std::wstring textFamily_;
    std::wstring displayFamily_;
    std::map<std::tuple<bool, int, int, int, int, bool>, winrt::com_ptr<IDWriteTextFormat>> formats_;
};

}  // namespace aether::graphics
