// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/TextCache.h"

#include <cmath>

namespace aether::graphics {

void TextCache::Initialize(IDWriteFactory* factory, std::wstring textFamily, std::wstring displayFamily) {
    factory_.copy_from(factory);
    formats_.clear();
    textFamily_ = ResolveFamily(textFamily);
    displayFamily_ = ResolveFamily(displayFamily);
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
    const auto key = std::make_tuple(style.display, static_cast<int>(std::lround(style.size * 100.0f)),
                                     static_cast<int>(style.weight), static_cast<int>(style.alignment),
                                     static_cast<int>(style.verticalAlignment), style.ellipsis);
    if (auto it = formats_.find(key); it != formats_.end()) return it->second.get();

    winrt::com_ptr<IDWriteTextFormat> format;
    const std::wstring& family = style.display ? displayFamily_ : textFamily_;
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

D2D1_SIZE_F TextCache::Measure(std::wstring_view text, const TextStyle& style, float maxWidth) {
    auto* format = Format(style);
    if (!format || text.empty()) return D2D1::SizeF(0.0f, style.size * 1.33f);
    winrt::com_ptr<IDWriteTextLayout> layout;
    if (FAILED(factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format, maxWidth,
                                          style.size * 4.0f, layout.put()))) {
        return D2D1::SizeF(0.0f, style.size * 1.33f);
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return D2D1::SizeF(metrics.widthIncludingTrailingWhitespace, metrics.height);
}

void TextCache::Draw(ID2D1DeviceContext* context, std::wstring_view text, const D2D1_RECT_F& rect,
                     const TextStyle& style, ID2D1Brush* brush) {
    auto* format = Format(style);
    if (!format || text.empty() || !brush) return;
    context->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format, rect, brush,
                       D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

}  // namespace aether::graphics
