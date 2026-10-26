// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The quick-controls symbol family: code points per installed icon font, battery/volume selection,
// glyph-run rendering of installed fonts (ink centred inside the box) and the vector fallback.
#include "AetherTest.h"

#include "D2DCanvas.h"

#include "graphics/Glyphs.h"
#include "graphics/SystemSymbols.h"

#include <dwrite.h>

#include <algorithm>
#include <string>

using namespace aether;
using namespace aether::graphics;
using test::D2DCanvas;

namespace {

struct Ink {
    int pixels = 0;
    int left = 1 << 20, top = 1 << 20, right = -1, bottom = -1;
};

Ink Measure(const D2DCanvas& canvas, UINT size) {
    Ink ink;
    for (UINT y = 0; y < size; ++y) {
        for (UINT x = 0; x < size; ++x) {
            if (canvas.At(x, y).a < 64) continue;
            ++ink.pixels;
            ink.left = std::min(ink.left, static_cast<int>(x));
            ink.top = std::min(ink.top, static_cast<int>(y));
            ink.right = std::max(ink.right, static_cast<int>(x));
            ink.bottom = std::max(ink.bottom, static_cast<int>(y));
        }
    }
    return ink;
}

winrt::com_ptr<IDWriteFactory> WriteFactory() {
    winrt::com_ptr<IDWriteFactory> factory;
    (void)::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.put()));
    return factory;
}

constexpr Symbol kPanelSymbols[] = {Symbol::WiFi, Symbol::Bluetooth, Symbol::Airplane, Symbol::Accessibility, Symbol::EnergySaver,
                                    Symbol::LiveCaptions, Symbol::Brightness, Symbol::Volume3, Symbol::VolumeMuted, Symbol::Mixer,
                                    Symbol::Settings, Symbol::ChevronRight, Symbol::CaretDown,
                                    static_cast<Symbol>(static_cast<int>(Symbol::BatteryCharging0) + 5)};

}  // namespace

AETHER_TEST("system symbols: Fluent and MDL2 code points follow the published icon lists") {
    CHECK(SymbolCodePoint(Symbol::WiFi, SymbolFamily::Fluent) == std::optional<char32_t>(0xE701));
    CHECK(SymbolCodePoint(Symbol::Bluetooth, SymbolFamily::Fluent) == std::optional<char32_t>(0xE702));
    CHECK(SymbolCodePoint(Symbol::Brightness, SymbolFamily::Fluent) == std::optional<char32_t>(0xE706));
    CHECK(SymbolCodePoint(Symbol::Airplane, SymbolFamily::Fluent) == std::optional<char32_t>(0xE709));
    CHECK(SymbolCodePoint(Symbol::Airplane, SymbolFamily::Mdl2) == std::optional<char32_t>(0xE709));
    // MDL2's ED1E is not a captions mark: its closed-caption glyph is used instead.
    CHECK(SymbolCodePoint(Symbol::LiveCaptions, SymbolFamily::Fluent) == std::optional<char32_t>(0xED1E));
    CHECK(SymbolCodePoint(Symbol::LiveCaptions, SymbolFamily::Mdl2) == std::optional<char32_t>(0xE7F0));
    CHECK(SymbolCodePoint(Symbol::Battery0, SymbolFamily::Fluent) == std::optional<char32_t>(0xEBA0));
    CHECK(SymbolCodePoint(Symbol::Battery10, SymbolFamily::Fluent) == std::optional<char32_t>(0xEBAA));
    CHECK(SymbolCodePoint(Symbol::BatteryCharging0, SymbolFamily::Fluent) == std::optional<char32_t>(0xEBAB));
    CHECK(SymbolCodePoint(Symbol::BatterySaver10, SymbolFamily::Fluent) == std::optional<char32_t>(0xEBC0));
    CHECK_FALSE(SymbolCodePoint(Symbol::WiFi, SymbolFamily::Vector).has_value());
    CHECK_FALSE(SymbolCodePoint(Symbol::Count, SymbolFamily::Fluent).has_value());
    for (std::size_t i = 0; i < kSymbolCount; ++i) CHECK(SymbolCodePoint(static_cast<Symbol>(i), SymbolFamily::Fluent).has_value());
}

AETHER_TEST("system symbols: battery and speaker glyphs follow known readings only") {
    CHECK(BatterySymbol(std::nullopt, false, false) == Symbol::BatteryUnknown);
    CHECK(BatterySymbol(101, false, false) == Symbol::BatteryUnknown);
    CHECK(BatterySymbol(0, false, false) == Symbol::Battery0);
    CHECK(BatterySymbol(1, false, false) == static_cast<Symbol>(static_cast<int>(Symbol::Battery0) + 1));  // never empty-looking
    CHECK(BatterySymbol(100, false, false) == Symbol::Battery10);
    CHECK(BatterySymbol(48, true, false) == static_cast<Symbol>(static_cast<int>(Symbol::BatteryCharging0) + 5));
    CHECK(BatterySymbol(48, false, true) == static_cast<Symbol>(static_cast<int>(Symbol::BatterySaver0) + 5));
    CHECK(VolumeSymbol(std::nullopt, std::nullopt) == Symbol::Volume0);  // no waves claimed for an unknown level
    CHECK(VolumeSymbol(80, true) == Symbol::VolumeMuted);
    CHECK(VolumeSymbol(0, false) == Symbol::Volume0);
    CHECK(VolumeSymbol(20, false) == Symbol::Volume1);
    CHECK(VolumeSymbol(50, false) == Symbol::Volume2);
    CHECK(VolumeSymbol(90, false) == Symbol::Volume3);
}

AETHER_TEST("system symbols: installed glyphs are centred inside their box at 100 and 200 percent") {
    auto factory = WriteFactory();
    REQUIRE(factory != nullptr);
    SymbolRenderer renderer;
    renderer.Initialize(factory.get());
    const auto family = renderer.FamilyOf(Symbol::Airplane);
    TEST_NOTE("airplane family: " + std::string(family == SymbolFamily::Fluent ? "Segoe Fluent Icons"
                                                : family == SymbolFamily::Mdl2   ? "Segoe MDL2 Assets"
                                                                                 : "vector"));
    for (const float dpi : {96.0f, 192.0f}) {
        const UINT size = static_cast<UINT>(32.0f * dpi / 96.0f);
        D2DCanvas canvas;
        REQUIRE(canvas.Create(size, size, dpi));
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        for (const Symbol symbol : kPanelSymbols) {
            canvas.Begin();
            if (!brush) REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), brush.put())));
            renderer.Draw(canvas.Context(), symbol, D2D1::RectF(8, 8, 24, 24), brush.get(), 16.0f);
            REQUIRE(canvas.End());
            const auto ink = Measure(canvas, size);
            const float scale = dpi / 96.0f;
            const bool drawn = ink.pixels > static_cast<int>(6 * scale * scale);
            // Ink stays inside the 16-DIP box (plus one pixel of antialiasing) around its centre.
            const bool inside = ink.left >= static_cast<int>(7 * scale) && ink.top >= static_cast<int>(7 * scale) &&
                                ink.right <= static_cast<int>(25 * scale) && ink.bottom <= static_cast<int>(25 * scale);
            if (!drawn || !inside)
                TEST_NOTE("symbol " + std::to_string(static_cast<int>(symbol)) + " ink " + std::to_string(ink.pixels) + " box " +
                          std::to_string(ink.left) + "," + std::to_string(ink.top) + "-" + std::to_string(ink.right) + "," + std::to_string(ink.bottom));
            CHECK(drawn);
            CHECK(inside);
        }
    }
}

AETHER_TEST("system symbols: the vector fallback draws every panel mark inside its box") {
    struct GlyphGuard {
        ~GlyphGuard() { ReleaseGlyphResources(); }
    } guard;
    D2DCanvas canvas;
    REQUIRE(canvas.Create(32, 32));
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    for (const Symbol symbol : kPanelSymbols) {
        canvas.Begin();
        if (!brush) REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), brush.put())));
        PaintSymbolVector(canvas.Context(), symbol, D2D1::RectF(8, 8, 24, 24), brush.get());
        REQUIRE(canvas.End());
        const auto ink = Measure(canvas, 32);
        if (ink.pixels <= 6) TEST_NOTE("vector symbol " + std::to_string(static_cast<int>(symbol)) + " drew " + std::to_string(ink.pixels));
        CHECK(ink.pixels > 6);
        CHECK(ink.left >= 6);
        CHECK(ink.top >= 6);
        CHECK(ink.right <= 25);
        CHECK(ink.bottom <= 25);
    }
}
