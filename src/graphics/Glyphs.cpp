// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/Glyphs.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace aether::graphics {
namespace {

constexpr float kGrid = 16.0f;

D2D1_POINT_2F P(float x, float y) noexcept { return D2D1::Point2F(x, y); }

// Records figures into one path geometry. Hollow figures are stroked, filled ones filled.
class PathBuilder {
public:
    explicit PathBuilder(ID2D1Factory* factory) {
        if (factory && SUCCEEDED(factory->CreatePathGeometry(geometry_.put())) && SUCCEEDED(geometry_->Open(sink_.put()))) {
            sink_->SetFillMode(D2D1_FILL_MODE_WINDING);
            ok_ = true;
        }
    }

    // An open polyline.
    void Polyline(std::initializer_list<D2D1_POINT_2F> points) {
        if (!ok_ || points.size() < 2) return;
        auto it = points.begin();
        sink_->BeginFigure(*it, D2D1_FIGURE_BEGIN_HOLLOW);
        for (++it; it != points.end(); ++it) sink_->AddLine(*it);
        sink_->EndFigure(D2D1_FIGURE_END_OPEN);
        empty_ = false;
    }

    void Circle(float cx, float cy, float radius, bool filled) {
        if (!ok_) return;
        sink_->BeginFigure(P(cx + radius, cy), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        sink_->AddArc(D2D1::ArcSegment(P(cx - radius, cy), D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                       D2D1_ARC_SIZE_SMALL));
        sink_->AddArc(D2D1::ArcSegment(P(cx + radius, cy), D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                       D2D1_ARC_SIZE_SMALL));
        sink_->EndFigure(D2D1_FIGURE_END_CLOSED);
        empty_ = false;
    }

    void RoundRect(float left, float top, float right, float bottom, float radius, bool filled) {
        if (!ok_) return;
        radius = std::clamp(radius, 0.0f, std::min(right - left, bottom - top) / 2.0f);
        const auto arc = [&](float x, float y) {
            if (radius > 0.0f)
                sink_->AddArc(D2D1::ArcSegment(P(x, y), D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                               D2D1_ARC_SIZE_SMALL));
            else sink_->AddLine(P(x, y));
        };
        sink_->BeginFigure(P(left + radius, top), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        sink_->AddLine(P(right - radius, top));
        arc(right, top + radius);
        sink_->AddLine(P(right, bottom - radius));
        arc(right - radius, bottom);
        sink_->AddLine(P(left + radius, bottom));
        arc(left, bottom - radius);
        sink_->AddLine(P(left, top + radius));
        arc(left + radius, top);
        sink_->EndFigure(D2D1_FIGURE_END_CLOSED);
        empty_ = false;
    }

    [[nodiscard]] ID2D1GeometrySink* Sink() noexcept {
        empty_ = false;
        return ok_ ? sink_.get() : nullptr;
    }

    // Null (without error) when nothing was recorded.
    [[nodiscard]] Result<winrt::com_ptr<ID2D1PathGeometry>> Finish() {
        if (!ok_) return MakeError(ErrorCode::Platform, "glyph geometry could not be created");
        ok_ = false;
        if (FAILED(sink_->Close())) return MakeError(ErrorCode::Platform, "glyph geometry could not be closed");
        if (empty_) return winrt::com_ptr<ID2D1PathGeometry>{};
        return geometry_;
    }

private:
    winrt::com_ptr<ID2D1PathGeometry> geometry_;
    winrt::com_ptr<ID2D1GeometrySink> sink_;
    bool ok_ = false;
    bool empty_ = true;
};

void BuildShape(Glyph glyph, PathBuilder& stroke, PathBuilder& fill) {
    switch (glyph) {
        case Glyph::Search:
            stroke.Circle(6.75f, 6.75f, 4.75f, false);
            stroke.Polyline({P(10.25f, 10.25f), P(14.25f, 14.25f)});
            break;
        case Glyph::TaskView: {
            // A front window and the visible part of a second window behind it.
            stroke.RoundRect(1.75f, 5.75f, 10.25f, 14.25f, 2.0f, false);
            if (auto* sink = stroke.Sink()) {
                const auto arc = [&](float x, float y) {
                    sink->AddArc(D2D1::ArcSegment(P(x, y), D2D1::SizeF(2.0f, 2.0f), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                                  D2D1_ARC_SIZE_SMALL));
                };
                sink->BeginFigure(P(5.75f, 5.75f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(P(5.75f, 3.75f));
                arc(7.75f, 1.75f);
                sink->AddLine(P(12.25f, 1.75f));
                arc(14.25f, 3.75f);
                sink->AddLine(P(14.25f, 8.25f));
                arc(12.25f, 10.25f);
                sink->AddLine(P(10.25f, 10.25f));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            break;
        }
        case Glyph::Dock:
            fill.RoundRect(2.25f, 4.0f, 5.75f, 7.5f, 1.0f, true);
            fill.RoundRect(6.25f, 4.0f, 9.75f, 7.5f, 1.0f, true);
            fill.RoundRect(10.25f, 4.0f, 13.75f, 7.5f, 1.0f, true);
            stroke.RoundRect(1.75f, 9.75f, 14.25f, 14.0f, 2.0f, false);
            break;
        case Glyph::TopBar:
            stroke.RoundRect(1.75f, 1.75f, 14.25f, 14.25f, 2.5f, false);
            stroke.Polyline({P(1.75f, 5.5f), P(14.25f, 5.5f)});
            break;
        case Glyph::Desktop:
            stroke.RoundRect(1.75f, 2.25f, 14.25f, 11.0f, 1.5f, false);
            stroke.Polyline({P(8.0f, 11.0f), P(8.0f, 13.75f)});
            stroke.Polyline({P(5.25f, 13.75f), P(10.75f, 13.75f)});
            break;
        case Glyph::Weather:
            if (auto* sink = stroke.Sink()) {
                sink->BeginFigure(P(4.5f, 13.25f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(P(4.5f, 7.75f), D2D1::SizeF(2.75f, 2.75f), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddArc(D2D1::ArcSegment(P(11.75f, 8.5f), D2D1::SizeF(4.0f, 4.0f), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddArc(D2D1::ArcSegment(P(11.75f, 13.25f), D2D1::SizeF(2.375f, 2.375f), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            break;
        case Glyph::Updates: {
            // A circular arrow: an open arc around the centre with its head at the start.
            constexpr float kRadius = 5.25f;
            const float startX = 8.0f + kRadius * std::cos(-0.5236f);
            const float startY = 8.0f + kRadius * std::sin(-0.5236f);
            const float endX = 8.0f + kRadius * std::cos(0.5236f);
            const float endY = 8.0f + kRadius * std::sin(0.5236f);
            if (auto* sink = stroke.Sink()) {
                sink->BeginFigure(P(startX, startY), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(P(endX, endY), D2D1::SizeF(kRadius, kRadius), 0.0f,
                                              D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            stroke.Polyline({P(startX - 2.75f, startY), P(startX, startY), P(startX, startY - 2.75f)});
            break;
        }
        case Glyph::General:
            // Three sliders.
            stroke.Polyline({P(1.75f, 4.0f), P(3.25f, 4.0f)});
            stroke.Polyline({P(6.75f, 4.0f), P(14.25f, 4.0f)});
            stroke.Circle(5.0f, 4.0f, 1.75f, false);
            stroke.Polyline({P(1.75f, 8.0f), P(8.25f, 8.0f)});
            stroke.Polyline({P(11.75f, 8.0f), P(14.25f, 8.0f)});
            stroke.Circle(10.0f, 8.0f, 1.75f, false);
            stroke.Polyline({P(1.75f, 12.0f), P(5.25f, 12.0f)});
            stroke.Polyline({P(8.75f, 12.0f), P(14.25f, 12.0f)});
            stroke.Circle(7.0f, 12.0f, 1.75f, false);
            break;
        case Glyph::About:
            stroke.Circle(8.0f, 8.0f, 6.25f, false);
            stroke.Polyline({P(8.0f, 7.5f), P(8.0f, 11.25f)});
            fill.Circle(8.0f, 5.0f, 0.95f, true);
            break;
        case Glyph::ChevronDown: stroke.Polyline({P(3.5f, 6.0f), P(8.0f, 10.5f), P(12.5f, 6.0f)}); break;
        case Glyph::ChevronUp: stroke.Polyline({P(3.5f, 10.0f), P(8.0f, 5.5f), P(12.5f, 10.0f)}); break;
        case Glyph::ChevronLeft: stroke.Polyline({P(10.0f, 3.5f), P(5.5f, 8.0f), P(10.0f, 12.5f)}); break;
        case Glyph::ChevronRight: stroke.Polyline({P(6.0f, 3.5f), P(10.5f, 8.0f), P(6.0f, 12.5f)}); break;
        case Glyph::Check: stroke.Polyline({P(2.75f, 8.5f), P(6.25f, 12.0f), P(13.25f, 4.5f)}); break;
        case Glyph::Close:
            stroke.Polyline({P(3.5f, 3.5f), P(12.5f, 12.5f)});
            stroke.Polyline({P(12.5f, 3.5f), P(3.5f, 12.5f)});
            break;
        case Glyph::ArrowUp:
            stroke.Polyline({P(8.0f, 13.5f), P(8.0f, 2.75f)});
            stroke.Polyline({P(3.75f, 7.0f), P(8.0f, 2.75f), P(12.25f, 7.0f)});
            break;
        case Glyph::ArrowDown:
            stroke.Polyline({P(8.0f, 2.5f), P(8.0f, 13.25f)});
            stroke.Polyline({P(3.75f, 9.0f), P(8.0f, 13.25f), P(12.25f, 9.0f)});
            break;
        case Glyph::ArrowLeft:
            stroke.Polyline({P(13.5f, 8.0f), P(2.75f, 8.0f)});
            stroke.Polyline({P(7.0f, 3.75f), P(2.75f, 8.0f), P(7.0f, 12.25f)});
            break;
        case Glyph::ArrowRight:
            stroke.Polyline({P(2.5f, 8.0f), P(13.25f, 8.0f)});
            stroke.Polyline({P(9.0f, 3.75f), P(13.25f, 8.0f), P(9.0f, 12.25f)});
            break;
    }
}

struct FactoryCache {
    winrt::com_ptr<ID2D1Factory> factory;
    winrt::com_ptr<ID2D1StrokeStyle> roundStroke;
    std::map<int, GlyphGeometry> glyphs;
    winrt::com_ptr<ID2D1PathGeometry> brand;
    winrt::com_ptr<ID2D1PathGeometry> start;
};

// A few factories at most (the shared device's factory, owner-drawn menus' DC render target factory).
thread_local std::vector<FactoryCache> factoryCaches;

FactoryCache* CacheFor(ID2D1Factory* factory) {
    if (!factory) return nullptr;
    for (auto& cache : factoryCaches) {
        if (cache.factory.get() == factory) return &cache;
    }
    if (factoryCaches.size() >= 4) factoryCaches.erase(factoryCaches.begin());
    FactoryCache cache;
    cache.factory.copy_from(factory);
    factoryCaches.push_back(std::move(cache));
    return &factoryCaches.back();
}

winrt::com_ptr<ID2D1Factory> FactoryOf(ID2D1RenderTarget* target) {
    winrt::com_ptr<ID2D1Factory> factory;
    if (target) target->GetFactory(factory.put());
    return factory;
}

// Maps the 16-DIP grid into the centre of `box`; returns the previous transform.
D2D1_MATRIX_3X2_F PushGrid(ID2D1RenderTarget* target, const D2D1_RECT_F& box, float& scale) {
    D2D1_MATRIX_3X2_F previous{};
    target->GetTransform(&previous);
    const float width = box.right - box.left;
    const float height = box.bottom - box.top;
    scale = std::min(width, height) / kGrid;
    const float x = box.left + (width - kGrid * scale) / 2.0f;
    const float y = box.top + (height - kGrid * scale) / 2.0f;
    target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y) * previous);
    return previous;
}

bool UsableBox(const D2D1_RECT_F& box) noexcept {
    return std::isfinite(box.left) && std::isfinite(box.top) && std::isfinite(box.right) && std::isfinite(box.bottom) &&
           box.right - box.left > 0.5f && box.bottom - box.top > 0.5f;
}

winrt::com_ptr<ID2D1SolidColorBrush> Solid(ID2D1RenderTarget* target, const Color& color) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(color.ToD2D(), brush.put());
    return brush;
}

Color Lighten(const Color& color, float amount) noexcept {
    return Color{color.r + (1.0f - color.r) * amount, color.g + (1.0f - color.g) * amount,
                 color.b + (1.0f - color.b) * amount, color.a};
}

}  // namespace

void ReleaseGlyphResources() noexcept { factoryCaches.clear(); }

Result<GlyphGeometry> BuildGlyphGeometry(ID2D1Factory* factory, Glyph glyph) {
    if (!factory) return MakeError(ErrorCode::InvalidArgument, "no Direct2D factory");
    try {
        PathBuilder stroke(factory);
        PathBuilder fill(factory);
        BuildShape(glyph, stroke, fill);
        auto strokeGeometry = stroke.Finish();
        if (!strokeGeometry) return strokeGeometry.error();
        auto fillGeometry = fill.Finish();
        if (!fillGeometry) return fillGeometry.error();
        return GlyphGeometry{std::move(*strokeGeometry), std::move(*fillGeometry)};
    } catch (...) {
        return MakeError(ErrorCode::Internal, "glyph geometry failed");
    }
}

Result<winrt::com_ptr<ID2D1PathGeometry>> BuildBrandMarkGeometry(ID2D1Factory* factory) {
    if (!factory) return MakeError(ErrorCode::InvalidArgument, "no Direct2D factory");
    PathBuilder builder(factory);
    if (auto* sink = builder.Sink()) {
        // The two cubic ribbons of the logo (resources/branding), in a 1000-unit design space whose
        // centre maps to the grid centre.
        const auto p = [](float a, float b) {
            constexpr float kScale = 0.02124f * kGrid / 18.0f;
            return P(kGrid / 2.0f + (a - 500.0f) * kScale, kGrid / 2.0f + (b - 500.0f) * kScale);
        };
        sink->BeginFigure(p(150, 610), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier(D2D1::BezierSegment(p(260, 340), p(445, 170), p(810, 240)));
        sink->AddLine(p(700, 390));
        sink->AddBezier(D2D1::BezierSegment(p(500, 350), p(365, 425), p(280, 640)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(p(245, 770), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(p(345, 600));
        sink->AddBezier(D2D1::BezierSegment(p(485, 655), p(615, 575), p(725, 375)));
        sink->AddLine(p(850, 375));
        sink->AddBezier(D2D1::BezierSegment(p(715, 700), p(515, 850), p(245, 770)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    return builder.Finish();
}

Result<winrt::com_ptr<ID2D1PathGeometry>> BuildStartMarkGeometry(ID2D1Factory* factory) {
    if (!factory) return MakeError(ErrorCode::InvalidArgument, "no Direct2D factory");
    PathBuilder builder(factory);
    constexpr float kRadius = 1.5f;
    builder.RoundRect(1.0f, 1.0f, 7.5f, 7.5f, kRadius, true);
    builder.RoundRect(8.5f, 1.0f, 15.0f, 7.5f, kRadius, true);
    builder.RoundRect(1.0f, 8.5f, 7.5f, 15.0f, kRadius, true);
    builder.RoundRect(8.5f, 8.5f, 15.0f, 15.0f, kRadius, true);
    return builder.Finish();
}

ID2D1StrokeStyle* RoundStrokeStyle(ID2D1Factory* factory) {
    auto* cache = CacheFor(factory);
    if (!cache) return nullptr;
    if (!cache->roundStroke) {
        const auto properties = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                            D2D1_LINE_JOIN_ROUND);
        factory->CreateStrokeStyle(properties, nullptr, 0, cache->roundStroke.put());
    }
    return cache->roundStroke.get();
}

void PaintGlyph(ID2D1RenderTarget* target, Glyph glyph, const D2D1_RECT_F& box, ID2D1Brush* brush, float strokeWidth) {
    if (!target || !brush || !UsableBox(box)) return;
    const auto factory = FactoryOf(target);
    auto* cache = CacheFor(factory.get());
    if (!cache) return;
    auto found = cache->glyphs.find(static_cast<int>(glyph));
    if (found == cache->glyphs.end()) {
        auto built = BuildGlyphGeometry(factory.get(), glyph);
        if (!built) return;
        found = cache->glyphs.emplace(static_cast<int>(glyph), std::move(*built)).first;
    }
    float scale = 1.0f;
    const auto previous = PushGrid(target, box, scale);
    if (found->second.fill) target->FillGeometry(found->second.fill.get(), brush);
    if (found->second.stroke) {
        const float width = std::isfinite(strokeWidth) && strokeWidth > 0.0f ? strokeWidth : 1.5f;
        target->DrawGeometry(found->second.stroke.get(), brush, width, RoundStrokeStyle(factory.get()));
    }
    target->SetTransform(previous);
}

void PaintBrandMark(ID2D1RenderTarget* target, const D2D1_RECT_F& box, const DesignTokens& tokens) {
    if (!target || !UsableBox(box)) return;
    const auto factory = FactoryOf(target);
    auto* cache = CacheFor(factory.get());
    if (!cache) return;
    if (!cache->brand) {
        auto built = BuildBrandMarkGeometry(factory.get());
        if (!built || !*built) return;
        cache->brand = std::move(*built);
    }
    float scale = 1.0f;
    const auto previous = PushGrid(target, box, scale);
    winrt::com_ptr<ID2D1Brush> brush;
    if (tokens.mode != ThemeMode::HighContrast) {
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, tokens.brandPrimary.ToD2D()}, {1.0f, tokens.brandSecondary.ToD2D()}};
        winrt::com_ptr<ID2D1GradientStopCollection> collection;
        winrt::com_ptr<ID2D1LinearGradientBrush> gradient;
        if (SUCCEEDED(target->CreateGradientStopCollection(stops, 2, collection.put())) &&
            SUCCEEDED(target->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(P(0.0f, 0.0f), P(kGrid, kGrid * 0.35f)), collection.get(),
                gradient.put()))) {
            brush = gradient;
        }
    }
    if (!brush) brush = Solid(target, tokens.textPrimary);
    if (brush) target->FillGeometry(cache->brand.get(), brush.get());
    target->SetTransform(previous);
}

Color StartMarkColor(const DesignTokens& tokens) noexcept {
    if (tokens.mode == ThemeMode::HighContrast) return tokens.textPrimary;
    if (tokens.systemAccent && tokens.accentSource == AccentSource::Windows) return tokens.accentRoles.fill;
    return tokens.mode == ThemeMode::Dark ? Color::FromHex(0x4CC2FF) : Color::FromHex(0x0078D4);
}

void PaintStartMark(ID2D1RenderTarget* target, const D2D1_RECT_F& box, const DesignTokens& tokens) {
    if (!target || !UsableBox(box)) return;
    const auto factory = FactoryOf(target);
    auto* cache = CacheFor(factory.get());
    if (!cache) return;
    if (!cache->start) {
        auto built = BuildStartMarkGeometry(factory.get());
        if (!built || !*built) return;
        cache->start = std::move(*built);
    }
    const Color color = StartMarkColor(tokens);
    float scale = 1.0f;
    const auto previous = PushGrid(target, box, scale);
    winrt::com_ptr<ID2D1Brush> brush;
    if (tokens.mode != ThemeMode::HighContrast) {
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, Lighten(color, 0.18f).ToD2D()}, {1.0f, color.ToD2D()}};
        winrt::com_ptr<ID2D1GradientStopCollection> collection;
        winrt::com_ptr<ID2D1LinearGradientBrush> gradient;
        if (SUCCEEDED(target->CreateGradientStopCollection(stops, 2, collection.put())) &&
            SUCCEEDED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(P(0.0f, 1.0f), P(0.0f, 15.0f)),
                                                        collection.get(), gradient.put()))) {
            brush = gradient;
        }
    }
    if (!brush) brush = Solid(target, color);
    if (brush) target->FillGeometry(cache->start.get(), brush.get());
    target->SetTransform(previous);
}

void PaintStatusBadge(ID2D1RenderTarget* target, StatusBadge badge, const D2D1_RECT_F& box, const DesignTokens& tokens) {
    if (!target || !UsableBox(box)) return;
    const bool highContrast = tokens.mode == ThemeMode::HighContrast;
    Color fill = tokens.info;
    switch (badge) {
        case StatusBadge::Info: fill = tokens.info; break;
        case StatusBadge::Success: fill = tokens.success; break;
        case StatusBadge::Warning: fill = tokens.caution; break;
        case StatusBadge::Error: fill = tokens.critical; break;
    }
    if (highContrast) fill = tokens.textPrimary;
    const Color white = Color::FromHex(0xFFFFFF);
    const Color black = Color::FromHex(0x000000);
    const Color mark = highContrast ? tokens.glyph.halo : (ContrastRatio(white, fill) >= ContrastRatio(black, fill) ? white : black);
    float scale = 1.0f;
    const auto previous = PushGrid(target, box, scale);
    const auto factory = FactoryOf(target);
    if (auto brush = Solid(target, fill)) target->FillEllipse(D2D1::Ellipse(P(8.0f, 8.0f), 7.0f, 7.0f), brush.get());
    if (auto brush = Solid(target, mark)) {
        auto* style = RoundStrokeStyle(factory.get());
        const auto line = [&](float x1, float y1, float x2, float y2) {
            target->DrawLine(P(x1, y1), P(x2, y2), brush.get(), 1.5f, style);
        };
        switch (badge) {
            case StatusBadge::Info:
                line(8.0f, 7.25f, 8.0f, 11.25f);
                target->FillEllipse(D2D1::Ellipse(P(8.0f, 4.75f), 0.95f, 0.95f), brush.get());
                break;
            case StatusBadge::Success:
                line(4.75f, 8.25f, 7.0f, 10.5f);
                line(7.0f, 10.5f, 11.25f, 5.75f);
                break;
            case StatusBadge::Warning:
                line(8.0f, 4.25f, 8.0f, 8.75f);
                target->FillEllipse(D2D1::Ellipse(P(8.0f, 11.25f), 0.95f, 0.95f), brush.get());
                break;
            case StatusBadge::Error:
                line(5.5f, 5.5f, 10.5f, 10.5f);
                line(10.5f, 5.5f, 5.5f, 10.5f);
                break;
        }
    }
    target->SetTransform(previous);
}

}  // namespace aether::graphics
