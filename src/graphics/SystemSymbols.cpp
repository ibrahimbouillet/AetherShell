// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/SystemSymbols.h"

#include "graphics/Glyphs.h"

#include <algorithm>
#include <cmath>

namespace aether::graphics {
namespace {

constexpr std::size_t Index(Symbol symbol) noexcept { return static_cast<std::size_t>(symbol); }

// Segoe Fluent Icons code points (Microsoft's published icon list). MDL2 uses the same values
// except where noted in SymbolCodePoint.
constexpr char32_t FluentCodePoint(Symbol symbol) noexcept {
    const auto value = static_cast<int>(symbol);
    if (value >= static_cast<int>(Symbol::Battery0) && value <= static_cast<int>(Symbol::Battery10))
        return 0xEBA0 + static_cast<char32_t>(value - static_cast<int>(Symbol::Battery0));
    if (value >= static_cast<int>(Symbol::BatteryCharging0) && value <= static_cast<int>(Symbol::BatteryCharging10))
        return 0xEBAB + static_cast<char32_t>(value - static_cast<int>(Symbol::BatteryCharging0));
    if (value >= static_cast<int>(Symbol::BatterySaver0) && value <= static_cast<int>(Symbol::BatterySaver10))
        return 0xEBB6 + static_cast<char32_t>(value - static_cast<int>(Symbol::BatterySaver0));
    switch (symbol) {
        case Symbol::WiFi: return 0xE701;
        case Symbol::WiFiLevel1: return 0xE872;
        case Symbol::WiFiLevel2: return 0xE873;
        case Symbol::WiFiLevel3: return 0xE874;
        case Symbol::WiFiWarning: return 0xEB5E;
        case Symbol::Ethernet: return 0xE839;
        case Symbol::Bluetooth: return 0xE702;
        case Symbol::Airplane: return 0xE709;
        case Symbol::Accessibility: return 0xE776;
        case Symbol::EnergySaver: return 0xEBB6;
        case Symbol::LiveCaptions: return 0xED1E;
        case Symbol::NightLight: return 0xF08C;
        case Symbol::Hotspot: return 0xE704;
        case Symbol::NearbySharing: return 0xE72D;
        case Symbol::Cast: return 0xEC15;
        case Symbol::Project: return 0xEBC6;
        case Symbol::Settings: return 0xE713;
        case Symbol::Brightness: return 0xE706;
        case Symbol::Volume0: return 0xE992;
        case Symbol::Volume1: return 0xE993;
        case Symbol::Volume2: return 0xE994;
        case Symbol::Volume3: return 0xE995;
        case Symbol::VolumeMuted: return 0xE74F;
        case Symbol::Mixer: return 0xF4C3;
        case Symbol::ChevronRight: return 0xE76C;
        case Symbol::ChevronLeft: return 0xE76B;
        case Symbol::ChevronDown: return 0xE70D;
        case Symbol::ChevronUp: return 0xE70E;
        case Symbol::ChromeMinimize: return 0xE921;
        case Symbol::ChromeMaximize: return 0xE922;
        case Symbol::ChromeRestore: return 0xE923;
        case Symbol::ChromeClose: return 0xE8BB;
        case Symbol::CaretDown: return 0xEDDC;
        case Symbol::CaretUp: return 0xEDDB;
        case Symbol::Back: return 0xE72B;
        case Symbol::OpenExternal: return 0xE8A7;
        case Symbol::Check: return 0xE73E;
        case Symbol::Play: return 0xE768;
        case Symbol::Pause: return 0xE769;
        case Symbol::Previous: return 0xE892;
        case Symbol::Next: return 0xE893;
        case Symbol::Magnifier: return 0xE8A3;
        case Symbol::Narrator: return 0xF8B2;
        case Symbol::Keyboard: return 0xE765;
        case Symbol::BatteryUnknown: return 0xEC02;
        default: return 0;
    }
}

// --- vector fallback ---------------------------------------------------------------------------

struct Grid {
    D2D1_RECT_F box;
    float unit;  // one 16-grid unit in DIPs
    float x0, y0;
    [[nodiscard]] D2D1_POINT_2F P(float x, float y) const noexcept { return D2D1::Point2F(x0 + x * unit, y0 + y * unit); }
};

Grid MakeGrid(const D2D1_RECT_F& box) noexcept {
    const float side = std::min(box.right - box.left, box.bottom - box.top);
    Grid grid{box, side / 16.0f, 0.0f, 0.0f};
    grid.x0 = (box.left + box.right - side) / 2.0f;
    grid.y0 = (box.top + box.bottom - side) / 2.0f;
    return grid;
}

winrt::com_ptr<ID2D1PathGeometry> Path(ID2D1Factory* factory, const Grid& g, std::initializer_list<D2D1_POINT_2F> points,
                                       bool closed) {
    winrt::com_ptr<ID2D1PathGeometry> path;
    if (!factory || points.size() < 2 || FAILED(factory->CreatePathGeometry(path.put()))) return nullptr;
    winrt::com_ptr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(sink.put()))) return nullptr;
    auto it = points.begin();
    sink->BeginFigure(g.P(it->x, it->y), closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (++it; it != points.end(); ++it) sink->AddLine(g.P(it->x, it->y));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return nullptr;
    return path;
}

}  // namespace

std::optional<char32_t> SymbolCodePoint(Symbol symbol, SymbolFamily family) noexcept {
    if (Index(symbol) >= kSymbolCount) return std::nullopt;
    switch (family) {
        case SymbolFamily::Fluent: {
            const char32_t value = FluentCodePoint(symbol);
            return value ? std::optional<char32_t>(value) : std::nullopt;
        }
        case SymbolFamily::Mdl2:
            // MDL2's ED1E is a speech bubble: captions use its CC mark instead. Its E776 is the
            // Windows 10 Ease of Access mark, which is that system's accessibility symbol.
            if (symbol == Symbol::LiveCaptions) return 0xE7F0;
            if (symbol == Symbol::Narrator) return 0xE8D4;
            {
                const char32_t value = FluentCodePoint(symbol);
                return value ? std::optional<char32_t>(value) : std::nullopt;
            }
        case SymbolFamily::Vector:
        case SymbolFamily::None:
            return std::nullopt;
    }
    return std::nullopt;
}

std::wstring_view SymbolFamilyName(SymbolFamily family) noexcept {
    switch (family) {
        case SymbolFamily::Fluent: return L"Segoe Fluent Icons";
        case SymbolFamily::Mdl2: return L"Segoe MDL2 Assets";
        case SymbolFamily::Vector: return L"vector";
        case SymbolFamily::None: return L"none";
    }
    return L"none";
}

Symbol BatterySymbol(std::optional<int> percent, bool charging, bool saver) noexcept {
    if (!percent || *percent < 0 || *percent > 100) return Symbol::BatteryUnknown;
    // Nearest tenth, but a non-empty battery never shows the empty glyph.
    int step = (*percent + 5) / 10;
    if (*percent > 0) step = std::max(step, 1);
    const Symbol base = charging ? Symbol::BatteryCharging0 : saver ? Symbol::BatterySaver0 : Symbol::Battery0;
    return static_cast<Symbol>(static_cast<int>(base) + std::clamp(step, 0, 10));
}

Symbol VolumeSymbol(std::optional<int> percent, std::optional<bool> muted) noexcept {
    if (muted.value_or(false)) return Symbol::VolumeMuted;
    if (!percent) return Symbol::Volume0;
    if (*percent <= 0) return Symbol::Volume0;
    if (*percent < 34) return Symbol::Volume1;
    if (*percent < 67) return Symbol::Volume2;
    return Symbol::Volume3;
}

std::optional<SymbolRenderer::Face> SymbolRenderer::LoadFace(std::wstring_view family, winrt::com_ptr<IDWriteFont>& font) const {
    font = nullptr;
    if (!factory_) return std::nullopt;
    winrt::com_ptr<IDWriteFontCollection> fonts;
    if (FAILED(factory_->GetSystemFontCollection(fonts.put(), FALSE)) || !fonts) return std::nullopt;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(fonts->FindFamilyName(std::wstring(family).c_str(), &index, &exists)) || !exists) return std::nullopt;
    winrt::com_ptr<IDWriteFontFamily> fontFamily;
    if (FAILED(fonts->GetFontFamily(index, fontFamily.put())) || !fontFamily) return std::nullopt;
    if (FAILED(fontFamily->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                font.put())) || !font) {
        return std::nullopt;
    }
    Face result;
    if (FAILED(font->CreateFontFace(result.face.put())) || !result.face) return std::nullopt;
    DWRITE_FONT_METRICS metrics{};
    result.face->GetMetrics(&metrics);
    if (metrics.designUnitsPerEm == 0) return std::nullopt;
    result.ascent = static_cast<float>(metrics.ascent) / static_cast<float>(metrics.designUnitsPerEm);
    result.descent = static_cast<float>(metrics.descent) / static_cast<float>(metrics.designUnitsPerEm);
    return result;
}

void SymbolRenderer::Initialize(IDWriteFactory* factory) {
    factory_.copy_from(factory);
    std::array<winrt::com_ptr<IDWriteFont>, 2> fonts;
    faces_[0] = LoadFace(SymbolFamilyName(SymbolFamily::Fluent), fonts[0]);
    faces_[1] = LoadFace(SymbolFamilyName(SymbolFamily::Mdl2), fonts[1]);
    for (std::size_t i = 0; i < kSymbolCount; ++i) {
        Resolved resolved;
        for (const auto family : {SymbolFamily::Fluent, SymbolFamily::Mdl2}) {
            const std::size_t slot = family == SymbolFamily::Fluent ? 0 : 1;
            const auto code = SymbolCodePoint(static_cast<Symbol>(i), family);
            if (!code || !faces_[slot] || !fonts[slot]) continue;
            BOOL has = FALSE;
            if (FAILED(fonts[slot]->HasCharacter(static_cast<UINT32>(*code), &has)) || !has) continue;
            const UINT32 value = static_cast<UINT32>(*code);
            UINT16 glyph = 0;
            if (FAILED(faces_[slot]->face->GetGlyphIndices(&value, 1, &glyph)) || glyph == 0) continue;
            DWRITE_GLYPH_METRICS glyphMetrics{};
            DWRITE_FONT_METRICS metrics{};
            faces_[slot]->face->GetMetrics(&metrics);
            if (FAILED(faces_[slot]->face->GetDesignGlyphMetrics(&glyph, 1, &glyphMetrics, FALSE))) continue;
            resolved.family = family;
            resolved.glyph = glyph;
            resolved.advance = static_cast<float>(glyphMetrics.advanceWidth) / static_cast<float>(std::max<UINT16>(1, metrics.designUnitsPerEm));
            break;
        }
        resolved_[i] = resolved;
    }
}

SymbolFamily SymbolRenderer::FamilyOf(Symbol symbol) const noexcept {
    if (Index(symbol) >= kSymbolCount) return SymbolFamily::None;
    if (!factory_) return SymbolFamily::Vector;
    return resolved_[Index(symbol)].family;
}

void SymbolRenderer::Draw(ID2D1DeviceContext* context, Symbol symbol, const D2D1_RECT_F& box, ID2D1Brush* brush, float size) {
    if (!context || !brush || Index(symbol) >= kSymbolCount) return;
    const float width = box.right - box.left;
    const float height = box.bottom - box.top;
    if (!(width > 0.0f) || !(height > 0.0f)) return;
    const float em = size > 0.0f ? size : std::min(width, height);
    const float cx = (box.left + box.right) / 2.0f;
    const float cy = (box.top + box.bottom) / 2.0f;
    const auto& resolved = resolved_[Index(symbol)];
    const std::size_t slot = resolved.family == SymbolFamily::Fluent ? 0 : 1;
    if (factory_ && (resolved.family == SymbolFamily::Fluent || resolved.family == SymbolFamily::Mdl2) && faces_[slot]) {
        const auto& face = *faces_[slot];
        const float advance = resolved.advance * em;
        float x = cx - advance / 2.0f;
        float baseline = cy - (face.ascent + face.descent) * em / 2.0f + face.ascent * em;
        // Snap the em box to device pixels so strokes render identically wherever a symbol sits.
        D2D1_MATRIX_3X2_F transform{};
        context->GetTransform(&transform);
        float dpiX = 96.0f, dpiY = 96.0f;
        context->GetDpi(&dpiX, &dpiY);
        if (transform._12 == 0.0f && transform._21 == 0.0f && transform._11 > 0.0f && transform._22 > 0.0f && dpiX > 0.0f) {
            const float sx = transform._11 * dpiX / 96.0f;
            const float sy = transform._22 * dpiY / 96.0f;
            const float ox = transform._31 * dpiX / 96.0f;
            const float oy = transform._32 * dpiY / 96.0f;
            x = (std::round(x * sx + ox) - ox) / sx;
            baseline = (std::round(baseline * sy + oy) - oy) / sy;
        }
        DWRITE_GLYPH_RUN run{};
        run.fontFace = face.face.get();
        run.fontEmSize = em;
        run.glyphCount = 1;
        run.glyphIndices = &resolved.glyph;
        run.glyphAdvances = &advance;
        context->DrawGlyphRun(D2D1::Point2F(x, baseline), &run, brush, DWRITE_MEASURING_MODE_NATURAL);
        return;
    }
    PaintSymbolVector(context, symbol, D2D1::RectF(cx - em / 2.0f, cy - em / 2.0f, cx + em / 2.0f, cy + em / 2.0f), brush);
}

void PaintSymbolVector(ID2D1DeviceContext* context, Symbol symbol, const D2D1_RECT_F& box, ID2D1Brush* brush) {
    if (!context || !brush) return;
    winrt::com_ptr<ID2D1Factory> factory;
    context->GetFactory(factory.put());
    const Grid g = MakeGrid(box);
    const float stroke = std::max(0.75f, g.unit * 1.15f);
    auto* round = RoundStrokeStyle(factory.get());
    const auto draw = [&](const winrt::com_ptr<ID2D1PathGeometry>& path, bool fill) {
        if (!path) return;
        if (fill) context->FillGeometry(path.get(), brush);
        else context->DrawGeometry(path.get(), brush, stroke, round);
    };
    const auto line = [&](float ax, float ay, float bx, float by) { context->DrawLine(g.P(ax, ay), g.P(bx, by), brush, stroke, round); };
    const int value = static_cast<int>(symbol);
    const bool battery = value >= static_cast<int>(Symbol::Battery0) && value <= static_cast<int>(Symbol::BatterySaver10);
    if (battery || symbol == Symbol::BatteryUnknown || symbol == Symbol::EnergySaver) {
        // Horizontal cell with a terminal; a known level fills proportionally.
        const D2D1_ROUNDED_RECT cell{D2D1::RectF(g.P(2.5f, 5.0f).x, g.P(2.5f, 5.0f).y, g.P(13.0f, 11.0f).x, g.P(13.0f, 11.0f).y), g.unit, g.unit};
        context->DrawRoundedRectangle(cell, brush, stroke);
        line(14.4f, 7.0f, 14.4f, 9.0f);
        if (symbol == Symbol::BatteryUnknown) {
            line(6.3f, 6.6f, 9.2f, 9.4f);
            line(9.2f, 6.6f, 6.3f, 9.4f);
            return;
        }
        if (symbol == Symbol::EnergySaver) {
            // A small leaf inside the cell.
            draw(Path(factory.get(), g, {{4.4f, 9.6f}, {5.4f, 6.6f}, {8.4f, 6.2f}, {7.6f, 9.0f}, {4.4f, 9.6f}}, true), true);
            return;
        }
        int step = 0;
        bool charging = false;
        if (value <= static_cast<int>(Symbol::Battery10)) step = value - static_cast<int>(Symbol::Battery0);
        else if (value <= static_cast<int>(Symbol::BatteryCharging10)) { step = value - static_cast<int>(Symbol::BatteryCharging0); charging = true; }
        else step = value - static_cast<int>(Symbol::BatterySaver0);
        if (step > 0) {
            const float right = 4.0f + 7.5f * static_cast<float>(step) / 10.0f;
            context->FillRectangle(D2D1::RectF(g.P(4.0f, 6.5f).x, g.P(4.0f, 6.5f).y, g.P(right, 9.5f).x, g.P(right, 9.5f).y), brush);
        }
        if (charging) draw(Path(factory.get(), g, {{8.6f, 3.0f}, {6.2f, 8.4f}, {8.2f, 8.4f}, {7.2f, 13.0f}, {10.0f, 7.4f}, {8.0f, 7.4f}}, true), true);
        return;
    }
    switch (symbol) {
        case Symbol::WiFi: PaintWiFiGlyph(context, g.box, brush, 3, 1.4f); return;
        case Symbol::WiFiLevel1: PaintWiFiGlyph(context, g.box, brush, 0, 1.4f); return;
        case Symbol::WiFiLevel2: PaintWiFiGlyph(context, g.box, brush, 1, 1.4f); return;
        case Symbol::WiFiLevel3: PaintWiFiGlyph(context, g.box, brush, 2, 1.4f); return;
        case Symbol::WiFiWarning: PaintWiFiGlyph(context, g.box, brush, std::nullopt, 1.4f); return;
        case Symbol::Ethernet: PaintStatusGlyph(context, StatusGlyph::Ethernet, g.box, brush, 1.4f); return;
        case Symbol::Brightness: PaintStatusGlyph(context, StatusGlyph::Sun, g.box, brush, 1.4f); return;
        case Symbol::Volume0: PaintStatusGlyph(context, StatusGlyph::Speaker, g.box, brush, 1.4f); return;
        case Symbol::Volume1: PaintStatusGlyph(context, StatusGlyph::SpeakerLow, g.box, brush, 1.4f); return;
        case Symbol::Volume2: PaintStatusGlyph(context, StatusGlyph::SpeakerMedium, g.box, brush, 1.4f); return;
        case Symbol::Volume3: PaintStatusGlyph(context, StatusGlyph::SpeakerHigh, g.box, brush, 1.4f); return;
        case Symbol::VolumeMuted: PaintStatusGlyph(context, StatusGlyph::SpeakerMuted, g.box, brush, 1.4f); return;
        case Symbol::Settings: PaintGlyph(context, Glyph::Settings, g.box, brush, 1.4f); return;
        case Symbol::ChevronRight: PaintGlyph(context, Glyph::ChevronRight, g.box, brush, 1.4f); return;
        case Symbol::ChevronLeft: PaintGlyph(context, Glyph::ChevronLeft, g.box, brush, 1.4f); return;
        case Symbol::ChevronDown: draw(Path(factory.get(), g, {{3.5f, 5.75f}, {8.0f, 10.25f}, {12.5f, 5.75f}}, false), false); return;
        case Symbol::ChevronUp: draw(Path(factory.get(), g, {{3.5f, 10.25f}, {8.0f, 5.75f}, {12.5f, 10.25f}}, false), false); return;
        case Symbol::ChromeMinimize: line(3.0f, 8.0f, 13.0f, 8.0f); return;
        case Symbol::ChromeMaximize:
            context->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(g.P(3.0f, 3.0f).x, g.P(3.0f, 3.0f).y, g.P(13.0f, 13.0f).x, g.P(13.0f, 13.0f).y),
                                                            g.unit, g.unit), brush, stroke);
            return;
        case Symbol::ChromeRestore:
            context->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(g.P(3.0f, 5.5f).x, g.P(3.0f, 5.5f).y, g.P(10.5f, 13.0f).x, g.P(10.5f, 13.0f).y),
                                                            g.unit, g.unit), brush, stroke);
            draw(Path(factory.get(), g, {{5.5f, 3.0f}, {13.0f, 3.0f}, {13.0f, 10.5f}}, false), false);
            return;
        case Symbol::ChromeClose:
            line(3.5f, 3.5f, 12.5f, 12.5f);
            line(12.5f, 3.5f, 3.5f, 12.5f);
            return;
        case Symbol::Back: PaintGlyph(context, Glyph::ArrowLeft, g.box, brush, 1.4f); return;
        case Symbol::OpenExternal: PaintGlyph(context, Glyph::OpenExternal, g.box, brush, 1.4f); return;
        case Symbol::Check: PaintGlyph(context, Glyph::Check, g.box, brush, 1.4f); return;
        case Symbol::CaretDown: draw(Path(factory.get(), g, {{4.0f, 6.0f}, {12.0f, 6.0f}, {8.0f, 11.0f}}, true), true); return;
        case Symbol::CaretUp: draw(Path(factory.get(), g, {{4.0f, 10.0f}, {12.0f, 10.0f}, {8.0f, 5.0f}}, true), true); return;
        case Symbol::Airplane:
            // A swept-wing aircraft seen from above, nose to the right: one closed silhouette with a
            // rounded nose, a slim fuselage, swept main wings and a small tailplane.
            draw(Path(factory.get(), g,
                      {{14.6f, 8.0f}, {14.1f, 7.25f}, {13.1f, 7.0f}, {9.9f, 7.0f}, {6.8f, 1.6f}, {5.4f, 1.6f}, {7.1f, 7.0f},
                       {3.6f, 7.0f}, {2.4f, 4.9f}, {1.4f, 4.9f}, {2.0f, 8.0f}, {1.4f, 11.1f}, {2.4f, 11.1f}, {3.6f, 9.0f},
                       {7.1f, 9.0f}, {5.4f, 14.4f}, {6.8f, 14.4f}, {9.9f, 9.0f}, {13.1f, 9.0f}, {14.1f, 8.75f}},
                      true),
                 false);
            return;
        case Symbol::Bluetooth:
            draw(Path(factory.get(), g, {{4.6f, 5.0f}, {11.0f, 10.6f}, {8.0f, 13.4f}, {8.0f, 2.6f}, {11.0f, 5.4f}, {4.6f, 11.0f}}, false),
                 false);
            return;
        case Symbol::Accessibility:
            context->DrawEllipse(D2D1::Ellipse(g.P(8.0f, 3.2f), 1.4f * g.unit, 1.4f * g.unit), brush, stroke);
            draw(Path(factory.get(), g, {{2.6f, 5.6f}, {8.0f, 6.8f}, {13.4f, 5.6f}}, false), false);
            draw(Path(factory.get(), g, {{8.0f, 6.8f}, {8.0f, 9.6f}, {5.2f, 14.0f}}, false), false);
            line(8.0f, 9.6f, 10.8f, 14.0f);
            return;
        case Symbol::LiveCaptions: {
            const D2D1_ROUNDED_RECT frame{D2D1::RectF(g.P(1.8f, 3.4f).x, g.P(1.8f, 3.4f).y, g.P(14.2f, 12.6f).x, g.P(14.2f, 12.6f).y),
                                          1.6f * g.unit, 1.6f * g.unit};
            context->DrawRoundedRectangle(frame, brush, stroke);
            line(4.4f, 9.2f, 7.4f, 9.2f);
            line(9.0f, 9.2f, 11.6f, 9.2f);
            line(4.4f, 10.9f, 11.6f, 10.9f);
            return;
        }
        case Symbol::Mixer:
            line(1.8f, 4.0f, 14.2f, 4.0f);
            line(1.8f, 9.5f, 8.0f, 9.5f);
            context->FillEllipse(D2D1::Ellipse(g.P(10.0f, 4.0f), 1.7f * g.unit, 1.7f * g.unit), brush);
            context->FillEllipse(D2D1::Ellipse(g.P(4.6f, 9.5f), 1.7f * g.unit, 1.7f * g.unit), brush);
            draw(Path(factory.get(), g, {{9.4f, 10.8f}, {10.8f, 10.8f}, {12.6f, 9.0f}, {12.6f, 14.6f}, {10.8f, 12.8f}, {9.4f, 12.8f}}, true),
                 true);
            return;
        case Symbol::Play: draw(Path(factory.get(), g, {{4.6f, 2.8f}, {13.0f, 8.0f}, {4.6f, 13.2f}}, true), false); return;
        case Symbol::Pause:
            line(5.4f, 3.2f, 5.4f, 12.8f);
            line(10.6f, 3.2f, 10.6f, 12.8f);
            return;
        case Symbol::Previous:
            line(3.4f, 3.2f, 3.4f, 12.8f);
            draw(Path(factory.get(), g, {{12.8f, 3.2f}, {5.4f, 8.0f}, {12.8f, 12.8f}}, true), false);
            return;
        case Symbol::Next:
            line(12.6f, 3.2f, 12.6f, 12.8f);
            draw(Path(factory.get(), g, {{3.2f, 3.2f}, {10.6f, 8.0f}, {3.2f, 12.8f}}, true), false);
            return;
        default:
            return;  // no vector mark: the label carries the meaning instead of a misleading glyph
    }
}

}  // namespace aether::graphics
