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

    // A closed hollow polygon (its corners take the round line join when stroked).
    void Polygon(std::initializer_list<D2D1_POINT_2F> points) {
        if (!ok_ || points.size() < 3) return;
        auto it = points.begin();
        sink_->BeginFigure(*it, D2D1_FIGURE_BEGIN_HOLLOW);
        for (++it; it != points.end(); ++it) sink_->AddLine(*it);
        sink_->EndFigure(D2D1_FIGURE_END_CLOSED);
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

    void Curve(D2D1_POINT_2F start, D2D1_POINT_2F first, D2D1_POINT_2F second, D2D1_POINT_2F end) {
        if (!ok_) return;
        sink_->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
        sink_->AddBezier(D2D1::BezierSegment(first, second, end));
        sink_->EndFigure(D2D1_FIGURE_END_OPEN); empty_ = false;
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
        case Glyph::Settings: {
            // Eight rounded teeth around a hub: flat tooth tops on the outer radius, arcs between them.
            constexpr float kOuter = 6.5f;
            constexpr float kInner = 4.9f;
            constexpr float kDegrees = 3.14159265f / 180.0f;
            const auto at = [](float radius, float degrees) {
                return P(8.0f + radius * std::cos(degrees * kDegrees), 8.0f + radius * std::sin(degrees * kDegrees));
            };
            if (auto* sink = stroke.Sink()) {
                sink->BeginFigure(at(kInner, -90.0f - 16.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                for (int tooth = 0; tooth < 8; ++tooth) {
                    const float centre = -90.0f + 45.0f * static_cast<float>(tooth);
                    sink->AddLine(at(kOuter, centre - 9.0f));
                    sink->AddLine(at(kOuter, centre + 9.0f));
                    sink->AddLine(at(kInner, centre + 16.0f));
                    sink->AddArc(D2D1::ArcSegment(at(kInner, centre + 45.0f - 16.0f), D2D1::SizeF(kInner, kInner), 0.0f,
                                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                }
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            stroke.Circle(8.0f, 8.0f, 2.25f, false);
            break;
        }
        case Glyph::Appearance:
            stroke.Circle(8.0f, 8.0f, 6.25f, false);
            if (auto* sink = fill.Sink()) {
                sink->BeginFigure(P(8.0f, 1.75f), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddArc(D2D1::ArcSegment(P(8.0f, 14.25f), D2D1::SizeF(6.25f, 6.25f), 0.0f,
                                              D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            break;
        case Glyph::Power:
            if (auto* sink = stroke.Sink()) {
                // An open ring with its gap at the top, and the switch line through the gap.
                sink->BeginFigure(P(8.0f + 5.75f * 0.5f, 8.0f - 5.75f * 0.8660254f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(P(8.0f - 5.75f * 0.5f, 8.0f - 5.75f * 0.8660254f), D2D1::SizeF(5.75f, 5.75f),
                                              0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            stroke.Polyline({P(8.0f, 1.75f), P(8.0f, 7.25f)});
            break;
        case Glyph::Taskbar:
            stroke.RoundRect(1.75f, 2.75f, 14.25f, 13.25f, 1.5f, false);
            stroke.Polyline({P(1.75f, 10.25f), P(14.25f, 10.25f)});
            fill.RoundRect(4.0f, 11.25f, 5.5f, 12.25f, 0.5f, true);
            break;
        case Glyph::Plus:
            stroke.Polyline({P(8.0f, 3.0f), P(8.0f, 13.0f)});
            stroke.Polyline({P(3.0f, 8.0f), P(13.0f, 8.0f)});
            break;
        case Glyph::Minus: stroke.Polyline({P(3.0f, 8.0f), P(13.0f, 8.0f)}); break;
        case Glyph::Folder:
            stroke.Polygon({P(1.75f, 4.0f), P(6.25f, 4.0f), P(7.75f, 5.5f), P(14.25f, 5.5f), P(14.25f, 12.75f),
                            P(1.75f, 12.75f)});
            break;
        case Glyph::Download:
            stroke.Polyline({P(8.0f, 1.75f), P(8.0f, 10.25f)});
            stroke.Polyline({P(4.5f, 6.75f), P(8.0f, 10.25f), P(11.5f, 6.75f)});
            stroke.Polyline({P(2.25f, 10.75f), P(2.25f, 13.75f), P(13.75f, 13.75f), P(13.75f, 10.75f)});
            break;
        case Glyph::Shield:
            if (auto* sink = stroke.Sink()) {
                sink->BeginFigure(P(8.0f, 1.75f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(P(13.25f, 3.75f));
                sink->AddLine(P(13.25f, 7.75f));
                sink->AddBezier(D2D1::BezierSegment(P(13.25f, 11.0f), P(10.75f, 13.25f), P(8.0f, 14.25f)));
                sink->AddBezier(D2D1::BezierSegment(P(5.25f, 13.25f), P(2.75f, 11.0f), P(2.75f, 7.75f)));
                sink->AddLine(P(2.75f, 3.75f));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            stroke.Polyline({P(5.5f, 8.0f), P(7.25f, 9.75f), P(10.5f, 6.5f)});
            break;
        case Glyph::Warning:
            stroke.Polygon({P(8.0f, 2.0f), P(14.5f, 13.25f), P(1.5f, 13.25f)});
            stroke.Polyline({P(8.0f, 6.25f), P(8.0f, 9.25f)});
            fill.Circle(8.0f, 11.25f, 0.85f, true);
            break;
        case Glyph::Location:
            if (auto* sink = stroke.Sink()) {
                sink->BeginFigure(P(8.0f, 14.25f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddBezier(D2D1::BezierSegment(P(5.0f, 10.75f), P(3.25f, 8.5f), P(3.25f, 6.5f)));
                sink->AddArc(D2D1::ArcSegment(P(12.75f, 6.5f), D2D1::SizeF(4.75f, 4.75f), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddBezier(D2D1::BezierSegment(P(12.75f, 8.5f), P(11.0f, 10.75f), P(8.0f, 14.25f)));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            stroke.Circle(8.0f, 6.5f, 1.75f, false);
            break;
        case Glyph::OpenExternal:
            stroke.Polyline({P(12.75f, 9.25f), P(12.75f, 13.25f), P(2.75f, 13.25f), P(2.75f, 3.25f), P(6.75f, 3.25f)});
            stroke.Polyline({P(9.25f, 2.75f), P(13.25f, 2.75f), P(13.25f, 6.75f)});
            stroke.Polyline({P(13.25f, 2.75f), P(7.5f, 8.5f)});
            break;
        case Glyph::Reset: {
            // The Updates arrow mirrored: counter-clockwise, with its head on the left.
            constexpr float kRadius = 5.25f;
            const float startX = 8.0f - kRadius * std::cos(-0.5236f);
            const float startY = 8.0f + kRadius * std::sin(-0.5236f);
            const float endX = 8.0f - kRadius * std::cos(0.5236f);
            const float endY = 8.0f + kRadius * std::sin(0.5236f);
            if (auto* sink = stroke.Sink()) {
                sink->BeginFigure(P(startX, startY), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(P(endX, endY), D2D1::SizeF(kRadius, kRadius), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            stroke.Polyline({P(startX + 2.75f, startY), P(startX, startY), P(startX, startY - 2.75f)});
            break;
        }
    }
}

void Question(PathBuilder& stroke, PathBuilder& fill, float x = 11.0f, float y = 9.0f) {
    stroke.Curve(P(x, y + 1), P(x, y - 1), P(x + 3, y - 1), P(x + 3, y + 1));
    stroke.Curve(P(x + 3, y + 1), P(x + 3, y + 2), P(x + 1.5f, y + 2), P(x + 1.5f, y + 3));
    fill.Circle(x + 1.5f, y + 5, 0.6f, true);
}
void Cloud(PathBuilder& stroke, float lift = 0) {
    if (auto* sink = stroke.Sink()) {
        sink->BeginFigure(P(4, 11 - lift), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddBezier(D2D1::BezierSegment(P(0.5f, 11 - lift), P(0.5f, 6 - lift), P(4.5f, 6.5f - lift)));
        sink->AddBezier(D2D1::BezierSegment(P(5, 2 - lift), P(11, 2 - lift), P(11.5f, 7 - lift)));
        sink->AddBezier(D2D1::BezierSegment(P(15, 6 - lift), P(15.5f, 11 - lift), P(12, 11 - lift)));
        sink->AddLine(P(4, 11 - lift)); sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
}
void BuildStatusShape(StatusGlyph glyph, PathBuilder& stroke, PathBuilder& fill) {
    const auto line = [&](float a, float b, float c, float d) { stroke.Polyline({P(a,b),P(c,d)}); };
    switch (glyph) {
        case StatusGlyph::Speaker: case StatusGlyph::SpeakerLow: case StatusGlyph::SpeakerMedium:
        case StatusGlyph::SpeakerHigh: case StatusGlyph::SpeakerMuted: case StatusGlyph::SpeakerUnknown:
            stroke.Polyline({P(1.5f,6),P(4.25f,6),P(7.5f,3.25f),P(7.5f,12.75f),P(4.25f,10),P(1.5f,10),P(1.5f,6)});
            if (glyph == StatusGlyph::SpeakerMuted) { line(10.5f,6,14,10); line(10.5f,10,14,6); }
            else if (glyph == StatusGlyph::SpeakerUnknown) Question(stroke,fill,10,5.5f);
            else {
                const int count = glyph == StatusGlyph::SpeakerLow ? 1 : glyph == StatusGlyph::SpeakerMedium ? 2 : glyph == StatusGlyph::SpeakerHigh ? 3 : 0;
                for (int i = 0; i < count; ++i) {
                    const float x = 9.5f + static_cast<float>(i)*1.75f;
                    const float extent = 1.5f + static_cast<float>(i)*1.25f;
                    stroke.Curve(P(x,8-extent),P(x+1.5f,8-extent*0.4f),P(x+1.5f,8+extent*0.4f),P(x,8+extent));
                }
            }
            break;
        case StatusGlyph::Microphone: case StatusGlyph::MicrophoneMuted: case StatusGlyph::MicrophoneUnknown:
            stroke.RoundRect(5.5f,1.5f,10.5f,10,2.5f,false);
            stroke.Curve(P(3.25f,7.5f),P(3.25f,14),P(12.75f,14),P(12.75f,7.5f));
            line(8,12.5f,8,14.5f); line(5.25f,14.5f,10.75f,14.5f);
            if (glyph == StatusGlyph::MicrophoneMuted) line(2,14,14,2);
            else if (glyph == StatusGlyph::MicrophoneUnknown) Question(stroke,fill,11,8.5f);
            break;
        case StatusGlyph::Battery:
            stroke.RoundRect(1.25f,4.5f,13.25f,11.5f,1.75f,false); line(15,6.75f,15,9.25f); break;
        case StatusGlyph::Plug:
            line(5.5f,1.75f,5.5f,5.5f); line(10.5f,1.75f,10.5f,5.5f);
            stroke.Polyline({P(3.5f,5.5f),P(12.5f,5.5f),P(12.5f,8),P(11,10.5f),P(8,11.5f),P(5,10.5f),P(3.5f,8),P(3.5f,5.5f)});
            line(8,11.5f,8,14.25f); break;
        case StatusGlyph::Ethernet:
            stroke.RoundRect(1.75f,2.5f,14.25f,11,2,false); line(8,11,8,14); line(4.5f,14,11.5f,14); break;
        case StatusGlyph::NetworkNone: case StatusGlyph::NetworkOther:
            stroke.Circle(8,8,6,false);
            stroke.Curve(P(8,2),P(3,5),P(3,11),P(8,14));
            stroke.Curve(P(8,2),P(13,5),P(13,11),P(8,14)); line(2,8,14,8);
            if (glyph == StatusGlyph::NetworkNone) line(2,14,14,2); break;
        case StatusGlyph::NetworkUnknown: case StatusGlyph::Unknown:
            Question(stroke,fill,6.5f,4.0f); break;
        case StatusGlyph::Sun:
            stroke.Circle(8,8,3,false);
            for (int i = 0; i < 8; ++i) {
                const float angle = static_cast<float>(i)*0.78539816f;
                line(8+std::cos(angle)*5,8+std::sin(angle)*5,8+std::cos(angle)*6.5f,8+std::sin(angle)*6.5f);
            }
            break;
        case StatusGlyph::Moon:
            if (auto* sink = stroke.Sink()) {
                sink->BeginFigure(P(10.5f,2),D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddBezier(D2D1::BezierSegment(P(2,1),P(0.5f,11),P(7.5f,13.75f)));
                sink->AddBezier(D2D1::BezierSegment(P(10.5f,15),P(13,12.5f),P(14,10)));
                sink->AddBezier(D2D1::BezierSegment(P(8,12),P(5,5),P(10.5f,2)));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            break;
        case StatusGlyph::PartlyCloudyDay: case StatusGlyph::PartlyCloudyNight:
            if (glyph == StatusGlyph::PartlyCloudyDay) {
                stroke.Curve(P(2.25f,6),P(1,2),P(6,1),P(7,4));
                line(1.25f,2,2,2.75f);line(4.5f,1,4.5f,1.75f);line(7.5f,1.75f,7,2.5f);
            } else stroke.Curve(P(7,1.5f),P(2,1.5f),P(1,6),P(3.5f,7));
            Cloud(stroke); break;
        case StatusGlyph::Cloud: Cloud(stroke); break;
        case StatusGlyph::Fog: case StatusGlyph::Drizzle: case StatusGlyph::Rain:
        case StatusGlyph::Snow: case StatusGlyph::Thunder:
            Cloud(stroke,0.75f);
            if (glyph == StatusGlyph::Fog) { line(2.75f,12.25f,13.25f,12.25f);line(4,14.5f,12,14.5f); }
            else if (glyph == StatusGlyph::Drizzle) for (float x : {4.5f,8.0f,11.5f}) fill.Circle(x,13.5f,0.65f,true);
            else if (glyph == StatusGlyph::Rain) for (float x : {4.5f,8.5f,12.5f}) line(x,12.25f,x-1,14.5f);
            else if (glyph == StatusGlyph::Snow) for (float x : {5.5f,10.5f}) {line(x,12.25f,x,14.75f);line(x-1.1f,13.5f,x+1.1f,13.5f);}
            else stroke.Polyline({P(9.75f,10.75f),P(7.25f,13),P(10.25f,13),P(8.25f,15)});
            break;
    }
}

struct FactoryCache {
    winrt::com_ptr<ID2D1Factory> factory;
    winrt::com_ptr<ID2D1StrokeStyle> roundStroke;
    std::map<int, GlyphGeometry> glyphs;
    std::map<int, WiFiGlyphGeometry> wifi;
    std::map<int, GlyphGeometry> status;
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

Result<GlyphGeometry> BuildStatusGlyphGeometry(ID2D1Factory* factory, StatusGlyph glyph) {
    if (!factory) return MakeError(ErrorCode::InvalidArgument, "no Direct2D factory");
    if (glyph < StatusGlyph::Speaker || glyph > StatusGlyph::Unknown) glyph = StatusGlyph::Unknown;
    try {
        PathBuilder stroke(factory), fill(factory); BuildStatusShape(glyph,stroke,fill);
        auto lines = stroke.Finish(); if (!lines) return lines.error();
        auto solids = fill.Finish(); if (!solids) return solids.error();
        return GlyphGeometry{std::move(*lines),std::move(*solids)};
    } catch (...) { return MakeError(ErrorCode::Internal,"status glyph geometry failed"); }
}

Result<WiFiGlyphGeometry> BuildWiFiGlyphGeometry(ID2D1Factory* factory, std::optional<int> activeWaves) {
    if (!factory) return MakeError(ErrorCode::InvalidArgument, "no Direct2D factory");
    try {
        const bool known = activeWaves && *activeWaves >= 0 && *activeWaves <= 3;
        PathBuilder active(factory), fill(factory), inactive(factory);
        const auto arc = [&](int level, float left, float top, float right, float peak) {
            auto& builder = known && *activeWaves >= level ? active : inactive;
            if (auto* sink = builder.Sink()) {
                const float span = right - left;
                sink->BeginFigure(P(left, top), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddBezier(D2D1::BezierSegment(P(left + span * 0.28f, peak),
                    P(right - span * 0.28f, peak), P(right, top)));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
        };
        arc(3, 1.25f, 5.25f, 14.75f, 0.75f);
        arc(2, 3.75f, 8.0f, 12.25f, 5.0f);
        arc(1, 6.0f, 10.5f, 10.0f, 9.25f);
        if (known && *activeWaves > 0) fill.Circle(8.0f, 13.5f, 1.3f, true);
        else active.Circle(8.0f, 13.5f, 0.9f, false);
        if (!known) {
            // Unknown strength is a question mark, not a disconnected cross or invented full bars.
            if (auto* sink = active.Sink()) {
                sink->BeginFigure(P(11.75f, 11.25f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddBezier(D2D1::BezierSegment(P(11.75f, 9.75f), P(14.25f, 9.75f), P(14.25f, 11.25f)));
                sink->AddBezier(D2D1::BezierSegment(P(14.25f, 12.0f), P(13.0f, 12.0f), P(13.0f, 13.0f)));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            fill.Circle(13.0f, 14.75f, 0.5f, true);
        }
        auto strokeGeometry = active.Finish(); if (!strokeGeometry) return strokeGeometry.error();
        auto fillGeometry = fill.Finish(); if (!fillGeometry) return fillGeometry.error();
        auto inactiveGeometry = inactive.Finish(); if (!inactiveGeometry) return inactiveGeometry.error();
        return WiFiGlyphGeometry{GlyphGeometry{std::move(*strokeGeometry), std::move(*fillGeometry)}, std::move(*inactiveGeometry)};
    } catch (...) {
        return MakeError(ErrorCode::Internal, "Wi-Fi glyph geometry failed");
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

void PaintWiFiGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                    std::optional<int> activeWaves, float strokeWidth) {
    if (!target || !brush || !UsableBox(box)) return;
    const auto factory = FactoryOf(target);
    auto* cache = CacheFor(factory.get());
    if (!cache) return;
    const int key = activeWaves && *activeWaves >= 0 && *activeWaves <= 3 ? *activeWaves : -1;
    auto found = cache->wifi.find(key);
    if (found == cache->wifi.end()) {
        auto built = BuildWiFiGlyphGeometry(factory.get(), key < 0 ? std::nullopt : std::optional<int>(key));
        if (!built) return;
        found = cache->wifi.emplace(key, std::move(*built)).first;
    }
    const auto& geometry = found->second;
    const float width = std::isfinite(strokeWidth) && strokeWidth > 0.0f ? strokeWidth : 1.5f;
    const float opacity = brush->GetOpacity();
    float scale = 1.0f;
    const auto previous = PushGrid(target, box, scale);
    auto* stroke = RoundStrokeStyle(factory.get());
    if (geometry.inactiveStroke) {
        brush->SetOpacity(opacity * 0.28f);
        target->DrawGeometry(geometry.inactiveStroke.get(), brush, width, stroke);
        brush->SetOpacity(opacity);
    }
    if (geometry.active.fill) target->FillGeometry(geometry.active.fill.get(), brush);
    if (geometry.active.stroke) target->DrawGeometry(geometry.active.stroke.get(), brush, width, stroke);
    target->SetTransform(previous);
}

void PaintStatusGlyph(ID2D1RenderTarget* target, StatusGlyph glyph, const D2D1_RECT_F& box,
                      ID2D1Brush* brush, float strokeWidth) {
    if (!target || !brush || !UsableBox(box)) return;
    if (glyph < StatusGlyph::Speaker || glyph > StatusGlyph::Unknown) glyph = StatusGlyph::Unknown;
    const auto factory = FactoryOf(target); auto* cache = CacheFor(factory.get()); if (!cache) return;
    auto found = cache->status.find(static_cast<int>(glyph));
    if (found == cache->status.end()) {
        auto geometry = BuildStatusGlyphGeometry(factory.get(),glyph); if (!geometry) return;
        found = cache->status.emplace(static_cast<int>(glyph),std::move(*geometry)).first;
    }
    auto* stroke = RoundStrokeStyle(factory.get());
    float scale = 1; const auto previous = PushGrid(target,box,scale);
    if (found->second.fill) target->FillGeometry(found->second.fill.get(),brush);
    if (found->second.stroke) target->DrawGeometry(found->second.stroke.get(),brush,
        std::isfinite(strokeWidth) && strokeWidth > 0 ? strokeWidth : 1.5f,stroke);
    target->SetTransform(previous);
}

void PaintNetworkGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                       NetworkGlyphKind kind, std::optional<int> signalBars, bool limited, float strokeWidth) {
    if (!target || !brush || !UsableBox(box)) return;
    if (signalBars && (*signalBars < 0 || *signalBars > 5)) signalBars.reset();
    if (kind == NetworkGlyphKind::WiFi) PaintWiFiGlyph(target,box,brush,
        signalBars ? std::optional<int>((*signalBars*3+4)/5) : std::nullopt,strokeWidth);
    else if (kind != NetworkGlyphKind::Cellular) PaintStatusGlyph(target,
        kind == NetworkGlyphKind::Ethernet ? StatusGlyph::Ethernet : kind == NetworkGlyphKind::None ? StatusGlyph::NetworkNone :
        kind == NetworkGlyphKind::Other ? StatusGlyph::NetworkOther : StatusGlyph::NetworkUnknown,box,brush,strokeWidth);
    const auto factory = FactoryOf(target); auto* stroke = RoundStrokeStyle(factory.get());
    const float opacity = brush->GetOpacity(); float scale = 1; const auto previous = PushGrid(target,box,scale);
    if (kind == NetworkGlyphKind::Cellular) {
        for (int i = 0; i < 5; ++i) {
            brush->SetOpacity(signalBars && i < *signalBars ? opacity : opacity*0.28f);
            const float x = 2.0f+static_cast<float>(i)*2.9f;
            target->DrawLine(P(x,13.75f),P(x,12.0f-static_cast<float>(i)*2.15f),brush,1.75f,stroke);
        }
        brush->SetOpacity(opacity);
    }
    if (limited) target->DrawEllipse(D2D1::Ellipse(P(13,3),1.5f,1.5f),brush,1,stroke);
    target->SetTransform(previous);
    if (kind == NetworkGlyphKind::Cellular && !signalBars) PaintStatusGlyph(target,StatusGlyph::Unknown,
        D2D1::RectF(box.left,box.top,box.right,box.bottom),brush,strokeWidth);
}

void PaintVolumeGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                      std::optional<int> percent, std::optional<bool> muted, float strokeWidth) {
    if (percent && (*percent < 0 || *percent > 100)) percent.reset();
    const auto glyph = muted == true ? StatusGlyph::SpeakerMuted : !muted || !percent ? StatusGlyph::SpeakerUnknown :
        *percent == 0 ? StatusGlyph::Speaker : *percent <= 33 ? StatusGlyph::SpeakerLow :
        *percent <= 66 ? StatusGlyph::SpeakerMedium : StatusGlyph::SpeakerHigh;
    PaintStatusGlyph(target,glyph,box,brush,strokeWidth);
}

void PaintMicrophoneGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                          std::optional<bool> muted, float strokeWidth) {
    PaintStatusGlyph(target,!muted ? StatusGlyph::MicrophoneUnknown : *muted ? StatusGlyph::MicrophoneMuted :
        StatusGlyph::Microphone,box,brush,strokeWidth);
}

Color BatteryStatusColor(const DesignTokens& tokens, std::optional<int> percent, std::optional<bool> lowOrCritical) noexcept {
    if (tokens.mode == ThemeMode::HighContrast) return tokens.textPrimary;
    if (percent && (*percent < 0 || *percent > 100)) percent.reset();
    if (lowOrCritical == true || (percent && *percent <= 5)) return tokens.critical;
    if (percent == 100) return tokens.success;
    return tokens.textPrimary;
}

void PaintBatteryGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1SolidColorBrush* brush,
                       const DesignTokens& tokens, std::optional<int> percent, std::optional<bool> lowOrCritical,
                       bool charging, bool externalPower, float strokeWidth) {
    if (!target || !brush || !UsableBox(box)) return;
    if (percent && (*percent < 0 || *percent > 100)) percent.reset();
    const auto original = brush->GetColor();
    struct RestoreColor { ID2D1SolidColorBrush* brush; D2D1_COLOR_F color; ~RestoreColor() { brush->SetColor(color); } } restore{brush,original};
    auto color = BatteryStatusColor(tokens,percent,lowOrCritical); color.a *= original.a;
    // Zero has no fill to color. A critical outline keeps the real warning visible without
    // inventing a nonzero charge; the same cue supports an independently reported Windows flag.
    const bool critical = lowOrCritical == true || (percent && *percent <= 5);
    if (tokens.mode == ThemeMode::HighContrast || critical) brush->SetColor(color.ToD2D());
    if (!percent && externalPower && !charging && lowOrCritical != true) {
        PaintStatusGlyph(target,StatusGlyph::Plug,box,brush,strokeWidth); brush->SetColor(original); return;
    }
    PaintStatusGlyph(target,StatusGlyph::Battery,box,brush,strokeWidth);
    const auto factory = FactoryOf(target); auto* stroke = RoundStrokeStyle(factory.get());
    float scale = 1; const auto previous = PushGrid(target,box,scale);
    if (percent && *percent > 0) {
        brush->SetColor(color.ToD2D()); const float width = 9.0f*static_cast<float>(*percent)/100;
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(2.75f,6,2.75f+width,10),std::min(0.75f,width/2),0.75f),brush);
    }
    if (charging) {
        // Two tones keep a real charging mark readable across both the filled and empty portions.
        for (const bool halo : {true,false}) {
            brush->SetColor((halo ? tokens.glass.tint : tokens.textPrimary).WithAlpha(original.a).ToD2D());
            const float width = halo ? 2.25f : 1.0f;
            target->DrawLine(P(8.5f,5.5f),P(6.5f,8.5f),brush,width,stroke);
            target->DrawLine(P(6.5f,8.5f),P(9.5f,8.5f),brush,width,stroke);
            target->DrawLine(P(9.5f,8.5f),P(7.5f,11),brush,width,stroke);
        }
    }
    brush->SetColor(original); target->SetTransform(previous);
    if (!percent && !charging) PaintStatusGlyph(target,StatusGlyph::Unknown,
        D2D1::RectF(box.left+2,box.top,box.right-2,box.bottom),brush,strokeWidth);
}

void PaintWeatherGlyph(ID2D1RenderTarget* target, const D2D1_RECT_F& box, ID2D1Brush* brush,
                       WeatherSymbol category, std::optional<bool> isDay, bool cached, float strokeWidth) {
    StatusGlyph glyph = StatusGlyph::Unknown;
    switch (category) {
        case WeatherSymbol::Clear: if (isDay) glyph = *isDay ? StatusGlyph::Sun : StatusGlyph::Moon; break;
        case WeatherSymbol::PartlyCloudy: if (isDay) glyph = *isDay ? StatusGlyph::PartlyCloudyDay : StatusGlyph::PartlyCloudyNight; break;
        case WeatherSymbol::Cloudy: glyph = StatusGlyph::Cloud; break;
        case WeatherSymbol::Fog: glyph = StatusGlyph::Fog; break;
        case WeatherSymbol::Drizzle: glyph = StatusGlyph::Drizzle; break;
        case WeatherSymbol::Rain: glyph = StatusGlyph::Rain; break;
        case WeatherSymbol::Snow: glyph = StatusGlyph::Snow; break;
        case WeatherSymbol::Thunder: glyph = StatusGlyph::Thunder; break;
        case WeatherSymbol::Unknown: break;
    }
    PaintStatusGlyph(target,glyph,box,brush,strokeWidth);
    if (!cached || !target || !brush || !UsableBox(box)) return;
    const auto factory = FactoryOf(target); auto* stroke = RoundStrokeStyle(factory.get());
    float scale = 1; const auto previous = PushGrid(target,box,scale);
    target->DrawEllipse(D2D1::Ellipse(P(13.75f,13.75f),1.25f,1.25f),brush,0.75f,stroke);
    target->DrawLine(P(13.75f,13),P(13.75f,13.75f),brush,0.6f,stroke);
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
