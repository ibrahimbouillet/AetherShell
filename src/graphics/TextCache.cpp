// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/TextCache.h"

#include <algorithm>
#include <cmath>

namespace aether::graphics {

void TextCache::Initialize(IDWriteFactory* factory, std::wstring textFamily, std::wstring displayFamily) {
    factory_.copy_from(factory);
    formats_.clear();
    layouts_.clear();
    tabular_ = nullptr;
    textFamily_ = ResolveFamily(textFamily);
    displayFamily_ = ResolveFamily(displayFamily);
    // The optical Small instance belongs to Segoe UI Variable; other families use the text family.
    smallFamily_ = textFamily.rfind(L"Segoe UI Variable", 0) == 0 ? ResolveFamily(L"Segoe UI Variable Small") : textFamily_;
}

std::wstring TextCache::ResolveFamily(const std::wstring& preferred) const {
    // "Segoe UI Variable" ships with Windows 11; Windows 10 falls back to "Segoe UI".
    if (!factory_) return L"Segoe UI";
    winrt::com_ptr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(factory_->GetSystemFontCollection(fonts.put(), FALSE)) && fonts) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(preferred.c_str(), &index, &exists)) && exists) return preferred;
    }
    return L"Segoe UI";
}

IDWriteTextFormat* TextCache::Format(const TextStyle& style) {
    if (!factory_) return nullptr;
    const bool useSmall = style.opticalSmall && !style.display;
    const auto key = std::make_tuple(style.display, static_cast<int>(std::lround(style.size * 100.0f)),
                                     static_cast<int>(style.weight), static_cast<int>(style.alignment),
                                     static_cast<int>(style.verticalAlignment), style.ellipsis, useSmall);
    if (auto it = formats_.find(key); it != formats_.end()) return it->second.get();

    winrt::com_ptr<IDWriteTextFormat> format;
    const std::wstring& family = style.display ? displayFamily_ : useSmall ? smallFamily_ : textFamily_;
    if (FAILED(factory_->CreateTextFormat(family.c_str(), nullptr, style.weight, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, style.size, L"", format.put()))) {
        return nullptr;
    }
    format->SetTextAlignment(style.alignment);
    format->SetParagraphAlignment(style.verticalAlignment);
    if (style.ellipsis) {
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        winrt::com_ptr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(factory_->CreateEllipsisTrimmingSign(format.get(), ellipsis.put()))) {
            const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            format->SetTrimming(&trimming, ellipsis.get());
        }
    }
    auto* raw = format.get();
    formats_.emplace(key, std::move(format));
    return raw;
}

winrt::com_ptr<IDWriteTextLayout> TextCache::TabularLayout(std::wstring_view text, IDWriteTextFormat* format, float width,
                                                         float height) {
    winrt::com_ptr<IDWriteTextLayout> layout;
    if (!factory_ || FAILED(factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
                                                       std::max(1.0f, width), std::max(1.0f, height), layout.put()))) {
        return nullptr;
    }
    if (!tabular_ && SUCCEEDED(factory_->CreateTypography(tabular_.put()))) {
        if (FAILED(tabular_->AddFontFeature(DWRITE_FONT_FEATURE{DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1}))) tabular_ = nullptr;
    }
    if (tabular_) layout->SetTypography(tabular_.get(), DWRITE_TEXT_RANGE{0, static_cast<UINT32>(text.size())});
    return layout;
}

IDWriteTextLayout* TextCache::CachedLayout(std::wstring_view text, const TextStyle& style, IDWriteTextFormat* format, float width,
                                           float height) {
    // Sizes to a hundredth of a DIP: a box that moves by less draws the same text.
    LayoutKey key{std::wstring(text), style.display, static_cast<int>(std::lround(style.size * 100.0f)), static_cast<int>(style.weight),
                  static_cast<int>(style.alignment), static_cast<int>(style.verticalAlignment), style.ellipsis, style.opticalSmall,
                  style.tabularFigures, static_cast<int>(std::lround(width * 100.0f)), static_cast<int>(std::lround(height * 100.0f))};
    if (auto found = layouts_.find(key); found != layouts_.end()) return found->second.get();
    winrt::com_ptr<IDWriteTextLayout> layout;
    if (style.tabularFigures) layout = TabularLayout(text, format, width, height);
    else if (FAILED(factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format, std::max(1.0f, width),
                                               std::max(1.0f, height), layout.put())))
        layout = nullptr;
    if (!layout) return nullptr;
    // Bounded: a clock with seconds makes a new text every second. Starting again costs one layout per label.
    if (layouts_.size() >= 192) layouts_.clear();
    auto* raw = layout.get();
    layouts_.emplace(std::move(key), std::move(layout));
    return raw;
}

D2D1_SIZE_F TextCache::Measure(std::wstring_view text, const TextStyle& style, float maxWidth) {
    auto* format = Format(style);
    if (!format || text.empty()) return D2D1::SizeF(0.0f, style.size * 1.33f);
    auto* layout = CachedLayout(text, style, format, maxWidth, style.size * 4.0f);
    if (!layout) return D2D1::SizeF(0.0f, style.size * 1.33f);
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return D2D1::SizeF(metrics.widthIncludingTrailingWhitespace, metrics.height);
}

void TextCache::Draw(ID2D1DeviceContext* context, std::wstring_view text, const D2D1_RECT_F& rect,
                     const TextStyle& style, ID2D1Brush* brush) {
    auto* format = Format(style);
    if (!format || text.empty() || !brush) return;
    if (auto* layout = CachedLayout(text, style, format, rect.right - rect.left, rect.bottom - rect.top)) {
        context->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout, brush,
                                D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        return;
    }
    context->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format, rect, brush,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

}  // namespace aether::graphics
