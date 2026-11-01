// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "welcome/Handwriting.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>

namespace aether::welcome {
namespace {

struct GridPoint { int x = 0, y = 0; };
using GridPath = std::vector<GridPoint>;
struct GlyphData {
    wchar_t character = 0;
    int advance = 0;
    bool lowercase = false;
    std::vector<GridPath> paths;
    GridPath lead;  // how the pen arrives when joined from the previous letter
};
struct MarkData { int code = 0; std::vector<GridPath> paths; };
struct Tables {
    std::vector<GlyphData> glyphs;
    std::vector<MarkData> marks;
};

const Tables& Data() {
#include "welcome/HandwritingStrokes.inc"
    return tables;
}

constexpr int kCedilla = 0x0327;
constexpr float kBaseline = 0.0f;       // grid height of the line the letters stand on
constexpr float kXHeight = 50.0f;       // grid height of a lowercase letter
constexpr float kWritingSeconds = 1.5f;                            // D59: the whole writing, whatever the name's length
constexpr float kGridSpeed = 170.0f;                               // the pace within it: only proportions matter
constexpr float kSpacing = 2.5f;                                   // DIPs between sampled points
constexpr float kLineGap = 12.0f;                                  // grid units between two lines
constexpr float kJoinMerge = 5.0f;      // grid units: a letter starting this close to the last one's end joins it
// D56, the look: a calligrapher's pen over the same paths. The hand leans forward, long letters reach well
// above and below small ones, and a down stroke swells while an up stroke is a hairline.
constexpr float kSlant = 0.26f;         // forward lean, as a share of the height
constexpr float kAdvance = 0.92f;       // letters sit a little closer than they were drawn
constexpr float kAscenderReach = 1.42f, kDescenderReach = 1.35f;
constexpr float kThickWidth = 4.6f, kHairWidth = 1.1f;  // grid units
constexpr float kSwell = 1.6f;          // how late in the turn downward a stroke starts to swell
constexpr float kWidthReach = 3.5f;     // grid units over which the width changes smoothly
constexpr float kShortStroke = 30.0f;   // grid units: dots, crossbars and accents keep some body
constexpr float kDotStroke = 8.0f;      // grid units: and a dot is a dab of the pen
constexpr float kFallbackReveal = 0.9f;                            // seconds

const GlyphData* FindGlyph(wchar_t character) {
    const auto& glyphs = Data().glyphs;
    const auto found = std::find_if(glyphs.begin(), glyphs.end(), [&](const GlyphData& glyph) { return glyph.character == character; });
    return found == glyphs.end() ? nullptr : &*found;
}
const MarkData* FindMark(int code) {
    const auto& marks = Data().marks;
    const auto found = std::find_if(marks.begin(), marks.end(), [&](const MarkData& mark) { return mark.code == code; });
    return found == marks.end() ? nullptr : &*found;
}
bool IsCombining(wchar_t character) { return character >= 0x0300 && character <= 0x036F; }

// Letters without a decomposition are written as the letters they stand for.
std::wstring Expand(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const wchar_t c : text) {
        switch (c) {
            case L'æ': result += L"ae"; break;
            case L'Æ': result += L"Ae"; break;
            case L'œ': result += L"oe"; break;
            case L'Œ': result += L"Oe"; break;
            case L'ß': result += L"ss"; break;
            case L'ø': result += L'o'; break;
            case L'Ø': result += L'O'; break;
            case L'ł': result += L'l'; break;
            case L'Ł': result += L'L'; break;
            case L'đ': result += L'd'; break;
            case L'Đ': result += L'D'; break;
            case L'ı': result += L'i'; break;
            case L'’': result += L'\''; break;
            case L' ': result += L' '; break;
            default: result += c; break;
        }
    }
    return result;
}

std::wstring Decompose(std::wstring_view text) {
    const auto expanded = Expand(text);
    if (expanded.empty()) return {};
    const int needed = ::NormalizeString(NormalizationD, expanded.data(), static_cast<int>(expanded.size()), nullptr, 0);
    if (needed <= 0) return expanded;
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    const int written = ::NormalizeString(NormalizationD, expanded.data(), static_cast<int>(expanded.size()), result.data(), needed);
    if (written <= 0) return expanded;
    result.resize(static_cast<std::size_t>(written));
    return result;
}

struct RawPoint { float x = 0.0f, y = 0.0f; };
using RawStroke = std::vector<RawPoint>;
struct RawLine { std::vector<RawStroke> strokes; float width = 0.0f; };

// Pen paths in grid units, y upwards. Mirrors compose() in tools/handwriting/strokes.py.
RawLine ComposeLine(std::wstring_view text) {
    RawLine line;
    std::vector<RawStroke> later;
    bool joinable = false;
    float x = 0.0f;
    const auto finishWord = [&] {
        line.strokes.insert(line.strokes.end(), later.begin(), later.end());
        later.clear();
        joinable = false;
    };
    const auto place = [](const GridPath& path, float originX, float originY) {
        RawStroke stroke;
        stroke.reserve(path.size());
        for (const auto& point : path) stroke.push_back({originX + static_cast<float>(point.x), originY + static_cast<float>(point.y)});
        return stroke;
    };
    const auto decomposed = Decompose(text);
    for (std::size_t i = 0; i < decomposed.size();) {
        const wchar_t character = decomposed[i++];
        std::vector<int> marks;
        while (i < decomposed.size() && IsCombining(decomposed[i])) marks.push_back(static_cast<int>(decomposed[i++]));
        const GlyphData* glyph = FindGlyph(character);
        if (!glyph) continue;  // CanWrite decides beforehand; nothing here may crash on odd input
        if (!glyph->lowercase && glyph->paths.size() <= 1 && (character == L' ' || character == L'-' || character == L'\'' || character == L'.')) finishWord();
        for (std::size_t index = 0; index < glyph->paths.size(); ++index) {
            const bool dot = (character == L'i' || character == L'j') && index == 1;
            if (dot && !marks.empty()) continue;  // an accent takes the place of the dot
            auto placed = place(glyph->paths[index], x, 0.0f);
            if (glyph->lowercase && index == 0) {
                if (joinable && !line.strokes.empty()) {
                    auto& current = line.strokes.back();
                    auto joined = place(glyph->lead, x, 0.0f);
                    joined.insert(joined.end(), placed.begin(), placed.end());
                    // A letter usually starts where the last one ended; a doubled point would knot the spline.
                    const std::size_t skip = !joined.empty() && !current.empty() &&
                        std::hypot(joined.front().x - current.back().x, joined.front().y - current.back().y) < kJoinMerge ? 1 : 0;
                    current.insert(current.end(), joined.begin() + static_cast<std::ptrdiff_t>(skip), joined.end());
                } else {
                    line.strokes.push_back(std::move(placed));
                }
            } else if (glyph->lowercase) {
                later.push_back(std::move(placed));
            } else {
                line.strokes.push_back(std::move(placed));  // a capital's strokes keep their own order
            }
        }
        float top = 50.0f;
        if (!glyph->paths.empty() && character != L'i' && character != L'j')
            for (const auto& point : glyph->paths.front()) top = std::max(top, static_cast<float>(point.y));
        const float centre = x + static_cast<float>(glyph->advance) * 0.46f;
        for (const int code : marks) {
            const MarkData* mark = FindMark(code);
            if (!mark) continue;
            const float originY = code == kCedilla ? 0.0f : top + 8.0f;
            for (const auto& path : mark->paths) later.push_back(place(path, centre, originY));
        }
        x += static_cast<float>(glyph->advance);
        joinable = glyph->lowercase || (!glyph->paths.empty() && glyph->paths.size() == 1 && character != L'-' && character != L'\'' && character != L'.');
    }
    finishWord();
    line.width = x;
    return line;
}

// The hand's lean and proportions: grid units in, grid units out, y still upwards.
RawPoint Lean(RawPoint point) {
    float y = point.y;
    if (y > kXHeight) y = kXHeight + (y - kXHeight) * kAscenderReach;
    if (y < kBaseline) y *= kDescenderReach;
    return {point.x * kAdvance + y * kSlant, y};
}

// Catmull-Rom through the authored points, then the lean, `perSegment` samples at a time.
RawStroke Spline(const RawStroke& stroke, const std::function<int(float)>& perSegment) {
    RawStroke result;
    if (stroke.empty()) return result;
    const auto at = [&](std::size_t index) { return stroke[std::min(index, stroke.size() - 1)]; };
    if (stroke.size() == 1) { result.push_back(Lean(stroke.front())); return result; }
    for (std::size_t i = 0; i + 1 < stroke.size(); ++i) {
        const auto p0 = at(i ? i - 1 : 0), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        // The curve is longer than its chord: sample by a generous estimate of its length.
        const float span = std::hypot(p2.x - p1.x, p2.y - p1.y) + 0.5f * (std::hypot(p1.x - p0.x, p1.y - p0.y) + std::hypot(p3.x - p2.x, p3.y - p2.y));
        const int steps = std::max(1, perSegment(span));
        for (int step = 0; step < steps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(steps), t2 = t * t, t3 = t2 * t;
            const float x = 0.5f * (2.0f * p1.x + (p2.x - p0.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (3.0f * p1.x - p0.x - 3.0f * p2.x + p3.x) * t3);
            const float y = 0.5f * (2.0f * p1.y + (p2.y - p0.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (3.0f * p1.y - p0.y - 3.0f * p2.y + p3.y) * t3);
            result.push_back(Lean({x, y}));
        }
    }
    result.push_back(Lean(stroke.back()));
    return result;
}

// Where a composed line's ink really reaches, in grid units, y upwards.
struct InkBox { float left = 0.0f, right = 0.0f, bottom = 0.0f, top = 0.0f; };
InkBox BoxOf(const RawLine& line) {
    InkBox box{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
    for (const auto& stroke : line.strokes)
        for (const auto& point : Spline(stroke, [](float) { return 8; })) {
            box.left = std::min(box.left, point.x); box.right = std::max(box.right, point.x);
            box.bottom = std::min(box.bottom, point.y); box.top = std::max(box.top, point.y);
        }
    if (box.right < box.left) return {};
    return box;
}

void Blur(std::vector<float>& values, int passes) {
    if (values.size() < 3) return;
    for (int pass = 0; pass < passes; ++pass) {
        auto smoothed = values;
        for (std::size_t i = 1; i + 1 < values.size(); ++i) smoothed[i] = 0.25f * values[i - 1] + 0.5f * values[i] + 0.25f * values[i + 1];
        values.swap(smoothed);
    }
}

// Timing and width from the shape of the line: the part of the model that makes it read as a hand.
void AnimatePen(WelcomeScene& scene, float unit) {
    const float speed = kGridSpeed * unit;
    // Smoothing has the same reach along a letter at any size.
    const float gridStep = kSpacing / unit;
    const int passes = std::clamp(static_cast<int>(2.0f * (kWidthReach / gridStep) * (kWidthReach / gridStep)), 3, 64);
    const float turnScale = unit;  // a turn per sample shrinks as the writing grows; the model reads it per grid step
    const float axisX = kSlant / std::hypot(kSlant, 1.0f), axisY = 1.0f / std::hypot(kSlant, 1.0f);
    scene.penWidth = std::max(2.0f, kThickWidth * unit);
    const float thick = scene.penWidth, hair = std::max(1.2f, kHairWidth * unit);
    float clock = 0.0f, inkTotal = 0.0f;
    PenPoint previousEnd{};
    bool hasPrevious = false;
    int strokeIndex = 0;
    for (auto& stroke : scene.strokes) {
        auto& points = stroke.points;
        if (points.empty()) continue;
        if (hasPrevious) {
            // A real lift: the pen pauses, travels above the page and lands.
            const float travel = std::hypot(points.front().x - previousEnd.x, points.front().y - previousEnd.y);
            clock += 0.07f + travel / (speed * 2.6f);
            ++scene.penLifts;
        }
        float length = 0.0f;
        for (std::size_t i = 1; i < points.size(); ++i) length += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
        const float gridLength = length / unit;
        std::vector<float> pace(points.size(), 1.0f), swell(points.size(), 0.0f);
        float along = 0.0f;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& before = points[i ? i - 1 : 0];
            const auto& after = points[std::min(i + 1, points.size() - 1)];
            float bend = 0.0f;
            if (i > 0 && i + 1 < points.size()) {
                const float ax = points[i].x - points[i - 1].x, ay = points[i].y - points[i - 1].y;
                const float bx = points[i + 1].x - points[i].x, by = points[i + 1].y - points[i].y;
                bend = std::abs(std::atan2(ax * by - ay * bx, ax * bx + ay * by));
            }
            // Along the hand's lean, with y downwards on the page: down strokes swell, all others are fine.
            const float dx = after.x - before.x, dy = after.y - before.y, distance = std::hypot(dx, dy);
            const float down = distance > 0.0f ? (dy * axisY - dx * axisX) / distance : 0.0f;
            swell[i] = std::pow(std::max(0.0f, down), kSwell);
            // Tight curves slow the hand, as does the weight of a down stroke; a slight rhythm keeps it from looking plotted.
            const float grid = along / unit;
            const float rhythm = 1.0f + 0.13f * std::sin(grid * 0.019f + static_cast<float>(strokeIndex) * 1.7f) + 0.06f * std::sin(grid * 0.061f);
            pace[i] = rhythm * (1.2f - 0.5f * swell[i]) / (1.0f + 5.5f * bend * turnScale);
            if (i > 0) along += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
            const float fromEdge = std::min(along, length - along);
            pace[i] *= 0.55f + 0.45f * std::clamp(fromEdge / (14.0f * unit), 0.0f, 1.0f);  // it starts and lands gently
        }
        // Smooth them so the ink's width and speed change like a hand's, not point by point.
        Blur(pace, passes);
        Blur(swell, passes);
        points.front().time = clock;
        along = 0.0f;
        // Short marks keep some body: a dot is a dab of the pen, a crossbar is more than a hairline.
        const float body = gridLength < kDotStroke ? 0.62f : gridLength < kShortStroke ? 0.42f : 0.0f;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (i > 0) {
                const float step = std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
                clock += step / (speed * std::max(0.12f, pace[i]));
                inkTotal += step;
                along += step;
                points[i].time = clock;
            }
            float width = std::max(hair + (thick - hair) * swell[i], thick * body);
            // A stroke enters and leaves the page on a fine point, except a dab.
            if (body < 0.6f) {
                const float taper = std::min(along, length - along) / (std::min(14.0f, gridLength / 3.0f) * unit);
                width *= 0.35f + 0.65f * std::pow(std::clamp(taper, 0.0f, 1.0f), 0.7f);
            }
            points[i].width = width;
        }
        previousEnd = points.back();
        hasPrevious = true;
        ++strokeIndex;
    }
    scene.inkLength = inkTotal;
    const float wanted = kWritingSeconds;
    const float scale = clock > 0.0f ? wanted / clock : 1.0f;
    for (auto& stroke : scene.strokes) for (auto& point : stroke.points) point.time *= scale;
    scene.inkDuration = clock > 0.0f ? wanted : 0.0f;
    scene.duration = scene.inkDuration;
}

// Places a line's ink with the top-left of its box at (left, top), in DIPs, y downwards.
void AddLine(WelcomeScene& scene, const RawLine& line, const InkBox& box, float unit, float left, float top) {
    for (const auto& raw : line.strokes) {
        PenStroke stroke;
        for (const auto& point : Spline(raw, [unit](float span) { return std::clamp(static_cast<int>(std::ceil(span * unit / kSpacing)), 1, 512); }))
            stroke.points.push_back({left + (point.x - box.left) * unit, top + (box.top - point.y) * unit, 0.0f, 0.0f});
        if (!stroke.points.empty()) scene.strokes.push_back(std::move(stroke));
    }
}

}  // namespace

bool CanWrite(std::wstring_view text) {
    try {
        const auto decomposed = Decompose(text);
        return std::all_of(decomposed.begin(), decomposed.end(), [](wchar_t c) { return IsCombining(c) ? FindMark(static_cast<int>(c)) != nullptr : FindGlyph(c) != nullptr; });
    } catch (...) { return false; }
}

Result<WelcomeScene> ComposeScene(std::wstring_view greeting, std::wstring_view name, float maxWidth, float maxHeight) try {
    if (!std::isfinite(maxWidth) || !std::isfinite(maxHeight) || maxWidth < 40.0f || maxHeight < 40.0f)
        return MakeError(ErrorCode::InvalidArgument, "Welcome scene bounds are invalid");
    const auto trimmed = [&] {
        std::wstring_view value = name;
        while (!value.empty() && (value.front() == L' ' || value.front() == L'\t')) value.remove_prefix(1);
        while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) value.remove_suffix(1);
        return std::wstring(value);
    }();
    WelcomeScene scene;
    const bool written = trimmed.empty() || CanWrite(trimmed);
    const float inkHeight = written ? maxHeight : maxHeight * 0.6f;  // a name in readable type goes below it
    // As large as the screen allows: one line, or two when that makes the writing larger.
    const auto layoutOf = [&](const std::vector<RawLine>& candidate, std::vector<InkBox>& boxes) {
        boxes.clear();
        float width = 0.0f, height = kLineGap * static_cast<float>(candidate.size() - 1);
        for (const auto& line : candidate) {
            const auto box = BoxOf(line);
            boxes.push_back(box);
            width = std::max(width, box.right - box.left);
            height += box.top - box.bottom;
        }
        return std::min(maxWidth / std::max(width, 1.0f), inkHeight / std::max(height, 1.0f));
    };
    std::vector<RawLine> lines{ComposeLine(written && !trimmed.empty() ? std::wstring(greeting) + L" " + trimmed : std::wstring(greeting))};
    std::vector<InkBox> boxes;
    float unit = layoutOf(lines, boxes);
    if (written && !trimmed.empty()) {
        const std::vector<RawLine> stacked{ComposeLine(greeting), ComposeLine(trimmed)};
        std::vector<InkBox> stackedBoxes;
        if (const float larger = layoutOf(stacked, stackedBoxes); larger > unit) {
            lines = stacked;
            boxes = std::move(stackedBoxes);
            unit = larger;
        }
    }
    unit = std::max(unit, 0.1f);
    float widest = 0.0f;
    for (const auto& box : boxes) widest = std::max(widest, (box.right - box.left) * unit);
    float top = 0.0f;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float lineWidth = (boxes[i].right - boxes[i].left) * unit;
        AddLine(scene, lines[i], boxes[i], unit, (widest - lineWidth) * 0.5f, top);
        top += (boxes[i].top - boxes[i].bottom + kLineGap) * unit;
    }
    top -= kLineGap * unit;
    scene.width = widest;
    scene.height = top;
    AnimatePen(scene, unit);
    if (!written) {
        // Readable type, sized like the ink, revealed after the greeting is written.
        scene.fallbackName = trimmed;
        scene.fallbackSize = std::clamp(54.0f * unit, std::min(18.0f, maxHeight * 0.25f), maxHeight * 0.25f);
        scene.fallbackHeight = scene.fallbackSize * 1.5f;
        scene.fallbackWidth = std::max(widest, std::min(maxWidth, scene.fallbackSize * 0.72f * static_cast<float>(trimmed.size()) + scene.fallbackSize));
        scene.width = scene.fallbackWidth;
        const float inkOffset = (scene.width - widest) * 0.5f;
        for (auto& stroke : scene.strokes) for (auto& point : stroke.points) point.x += inkOffset;
        scene.fallbackTop = top;
        scene.height = top + scene.fallbackHeight;
        scene.duration = scene.inkDuration + kFallbackReveal;
    }
    return scene;
} catch (...) { return MakeError(ErrorCode::Internal, "Welcome scene could not be composed"); }

std::optional<PenPosition> PenAt(const WelcomeScene& scene, float time) noexcept {
    if (scene.strokes.empty() || !std::isfinite(time) || time < 0.0f || time >= scene.inkDuration) return std::nullopt;
    const PenPoint* before = nullptr;
    for (const auto& stroke : scene.strokes) {
        if (stroke.points.empty()) continue;
        if (time < stroke.points.front().time) {
            if (!before) return std::nullopt;
            const auto& next = stroke.points.front();
            const float span = next.time - before->time;
            const float t = span > 0.0f ? (time - before->time) / span : 1.0f;
            return PenPosition{before->x + (next.x - before->x) * t, before->y + (next.y - before->y) * t, true};
        }
        if (time <= stroke.points.back().time) {
            const auto after = std::lower_bound(stroke.points.begin(), stroke.points.end(), time, [](const PenPoint& point, float value) { return point.time < value; });
            if (after == stroke.points.begin()) return PenPosition{after->x, after->y, false};
            const auto previous = after - 1;
            const float span = after->time - previous->time;
            const float t = span > 0.0f ? (time - previous->time) / span : 1.0f;
            return PenPosition{previous->x + (after->x - previous->x) * t, previous->y + (after->y - previous->y) * t, false};
        }
        before = &stroke.points.back();
    }
    return std::nullopt;
}

std::wstring GreetingWord(settings::GreetingLanguage language, LANGID displayLanguage) {
    switch (language) {
        case settings::GreetingLanguage::French: return L"Salut";
        case settings::GreetingLanguage::English: return L"Hello";
        case settings::GreetingLanguage::Automatic: break;
    }
    return PRIMARYLANGID(displayLanguage) == LANG_FRENCH ? L"Salut" : L"Hello";
}

}  // namespace aether::welcome
