// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Headless tests of the form module: the material rule, the message-box fallback mapping and the
// pure Direct2D painters rendered into an offscreen WARP canvas.
#include "AetherTest.h"

#include "../graphics/D2DCanvas.h"
#include "graphics/TextCache.h"
#include "graphics/Theme.h"
#include "ui/form/ConfirmDialog.h"
#include "ui/form/FormPaint.h"
#include "ui/form/FormSurface.h"

#include <cmath>
#include <string>

using namespace aether;
using namespace aether::ui::form;
using aether::graphics::ThemeMode;
using aether::test::CanvasPixel;
using aether::test::D2DCanvas;

namespace {

int Byte(float channel) { return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f)); }

// Premultiplied BGRA expectation of a color.
bool Matches(const CanvasPixel& pixel, const graphics::Color& color, int tolerance) {
    const float a = std::clamp(color.a, 0.0f, 1.0f);
    return aether::test::Near(pixel, Byte(color.r * a), Byte(color.g * a), Byte(color.b * a), Byte(a), tolerance);
}

std::string Describe(const CanvasPixel& pixel) {
    return "rgba(" + std::to_string(pixel.r) + "," + std::to_string(pixel.g) + "," + std::to_string(pixel.b) + "," +
           std::to_string(pixel.a) + ")";
}

struct TextFixture {
    winrt::com_ptr<IDWriteFactory> factory;
    graphics::TextCache cache;
    bool Create(const graphics::DesignTokens& tokens) {
        if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(factory.put())))) {
            return false;
        }
        cache.Initialize(factory.get(), tokens.fontFamily, tokens.displayFontFamily);
        return true;
    }
};

}  // namespace

AETHER_TEST("ui_form: material rule truth table") {
    for (const bool transparency : {false, true}) {
        for (const bool backdrop : {false, true}) {
            for (const bool renderer : {false, true}) {
                CHECK_EQ(ResolveFormMaterial(ThemeMode::HighContrast, transparency, backdrop, renderer), FormMaterial::Native);
                for (const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
                    const auto material = ResolveFormMaterial(mode, transparency, backdrop, renderer);
                    if (!renderer) {
                        CHECK_EQ(material, FormMaterial::Native);
                    } else if (backdrop && transparency) {
                        CHECK_EQ(material, FormMaterial::Acrylic);
                    } else {
                        CHECK_EQ(material, FormMaterial::Solid);
                    }
                }
            }
        }
    }
    CHECK_EQ(ToString(FormMaterial::Acrylic), std::string_view("acrylic"));
    CHECK_EQ(ToString(FormMaterial::Solid), std::string_view("solid"));
    CHECK_EQ(ToString(FormMaterial::Native), std::string_view("native"));
}

AETHER_TEST("ui_form: message box fallback flags follow the button set") {
    ConfirmRequest request;
    request.primary = L"&Reset";
    request.cancel = L"Cancel";
    request.severity = Severity::Warning;
    request.defaultChoice = ConfirmChoice::Cancel;
    UINT flags = FallbackMessageBoxFlags(request);
    CHECK_EQ(flags & MB_TYPEMASK, static_cast<UINT>(MB_OKCANCEL));
    CHECK_EQ(flags & MB_ICONMASK, static_cast<UINT>(MB_ICONWARNING));
    CHECK_EQ(flags & MB_DEFMASK, static_cast<UINT>(MB_DEFBUTTON2));

    request.secondary = L"&Keep";
    request.severity = Severity::Error;
    flags = FallbackMessageBoxFlags(request);
    CHECK_EQ(flags & MB_TYPEMASK, static_cast<UINT>(MB_YESNOCANCEL));
    CHECK_EQ(flags & MB_ICONMASK, static_cast<UINT>(MB_ICONERROR));
    CHECK_EQ(flags & MB_DEFMASK, static_cast<UINT>(MB_DEFBUTTON3));

    request.defaultChoice = ConfirmChoice::Secondary;
    CHECK_EQ(FallbackMessageBoxFlags(request) & MB_DEFMASK, static_cast<UINT>(MB_DEFBUTTON2));
    request.defaultChoice = ConfirmChoice::Primary;
    CHECK_EQ(FallbackMessageBoxFlags(request) & MB_DEFMASK, static_cast<UINT>(MB_DEFBUTTON1));

    request.cancel.clear();
    request.severity = Severity::Info;
    flags = FallbackMessageBoxFlags(request);
    CHECK_EQ(flags & MB_TYPEMASK, static_cast<UINT>(MB_YESNO));
    CHECK_EQ(flags & MB_ICONMASK, static_cast<UINT>(MB_ICONINFORMATION));
    // A Cancel default without a cancel button falls to the last button (Secondary).
    request.defaultChoice = ConfirmChoice::Cancel;
    CHECK_EQ(FallbackMessageBoxFlags(request) & MB_DEFMASK, static_cast<UINT>(MB_DEFBUTTON2));

    request.secondary.clear();
    request.severity = Severity::None;
    flags = FallbackMessageBoxFlags(request);
    CHECK_EQ(flags & MB_TYPEMASK, static_cast<UINT>(MB_OK));
    CHECK_EQ(flags & MB_ICONMASK, 0u);
}

AETHER_TEST("ui_form: message box results map back to choices") {
    ConfirmRequest request;
    request.primary = L"Quit";
    request.secondary = L"Save";
    request.cancel = L"Cancel";
    CHECK_EQ(ChoiceFromMessageBox(request, IDYES), ConfirmChoice::Primary);
    CHECK_EQ(ChoiceFromMessageBox(request, IDNO), ConfirmChoice::Secondary);
    CHECK_EQ(ChoiceFromMessageBox(request, IDCANCEL), ConfirmChoice::Cancel);
    CHECK_EQ(ChoiceFromMessageBox(request, 0), ConfirmChoice::Cancel);  // MessageBoxW failed: never confirm

    request.secondary.clear();
    CHECK_EQ(ChoiceFromMessageBox(request, IDOK), ConfirmChoice::Primary);
    CHECK_EQ(ChoiceFromMessageBox(request, IDCANCEL), ConfirmChoice::Cancel);
    CHECK_EQ(ChoiceFromMessageBox(request, IDNO), ConfirmChoice::Cancel);

    request.cancel.clear();
    CHECK_EQ(ChoiceFromMessageBox(request, IDOK), ConfirmChoice::Primary);
    CHECK_EQ(ChoiceFromMessageBox(request, IDCANCEL), ConfirmChoice::Primary);  // the last (only) button
    CHECK_EQ(ChoiceFromMessageBox(request, 0), ConfirmChoice::Cancel);
}

AETHER_TEST("ui_form: confirmation without a primary label is rejected") {
    ConfirmRequest request;
    request.title = L"Nothing to confirm";
    const auto result = ShowConfirmation(request, graphics::MakeDesignTokens(ThemeMode::Dark), graphics::MotionPolicy{});
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::InvalidArgument);
}

AETHER_TEST("ui_form: access keys are parsed like native controls") {
    auto parsed = paint::ParseMnemonic(L"&Reset defaults");
    CHECK_EQ(parsed.text, std::wstring(L"Reset defaults"));
    CHECK_EQ(parsed.index, 0);
    parsed = paint::ParseMnemonic(L"Save && e&xit");
    CHECK_EQ(parsed.text, std::wstring(L"Save & exit"));
    CHECK_EQ(parsed.index, 8);
    parsed = paint::ParseMnemonic(L"Trailing&");
    CHECK_EQ(parsed.text, std::wstring(L"Trailing"));
    CHECK_EQ(parsed.index, -1);
    parsed = paint::ParseMnemonic(L"A&B", false);
    CHECK_EQ(parsed.text, std::wstring(L"A&B"));
    CHECK_EQ(parsed.index, -1);
}

AETHER_TEST("ui_form: label text styles follow the form type scale and text scale") {
    auto tokens = graphics::MakeDesignTokens(ThemeMode::Light);
    tokens.textScale = 1.5f;
    const auto title = paint::StyleFor(tokens, TextRole::Title, true);
    CHECK_NEAR(title.size, tokens.form.titleSize * 1.5f, 1e-4);
    CHECK(title.display);
    CHECK_EQ(title.weight, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    const auto helper = paint::StyleFor(tokens, TextRole::Helper, false, true);
    CHECK_NEAR(helper.size, tokens.form.helperSize * 1.5f, 1e-4);
    CHECK(helper.ellipsis);
    CHECK_EQ(helper.alignment, DWRITE_TEXT_ALIGNMENT_TRAILING);
    CHECK(paint::TextColorFor(tokens, TextRole::Helper, true).r == tokens.textSecondary.r);
    CHECK(paint::TextColorFor(tokens, TextRole::Body, false).r == tokens.textDisabled.r);
}

AETHER_TEST("ui_form: switch colors for off, on and disabled") {
    for (const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
        const auto tokens = graphics::MakeDesignTokens(mode);
        D2DCanvas canvas;
        REQUIRE(canvas.Create(80, 40));
        const auto track = D2D1::RectF(10.0f, 10.0f, 10.0f + tokens.form.switchWidth, 10.0f + tokens.form.switchHeight);
        const float cy = (track.top + track.bottom) / 2.0f;
        const UINT leftKnob = static_cast<UINT>(track.left + tokens.form.switchHeight / 2.0f);
        const UINT rightKnob = static_cast<UINT>(track.right - tokens.form.switchHeight / 2.0f);
        paint::Visual visual;

        canvas.Begin();
        paint::PaintSwitch(canvas.Context(), track, 0.0f, visual, tokens);
        REQUIRE(canvas.End());
        const auto offKnob = canvas.At(leftKnob, static_cast<UINT>(cy));
        const auto offRight = canvas.At(rightKnob, static_cast<UINT>(cy));
        TEST_NOTE("off knob " + Describe(offKnob) + " off track " + Describe(offRight));
        CHECK(Matches(offKnob, tokens.form.switchOffStroke, 3));
        CHECK(offRight.a < 8);  // the off track is an outline, not a fill

        canvas.Begin();
        paint::PaintSwitch(canvas.Context(), track, 1.0f, visual, tokens);
        REQUIRE(canvas.End());
        const auto onKnob = canvas.At(rightKnob, static_cast<UINT>(cy));
        const auto onLeft = canvas.At(leftKnob, static_cast<UINT>(cy));
        TEST_NOTE("on knob " + Describe(onKnob) + " on track " + Describe(onLeft));
        CHECK(Matches(onKnob, tokens.accentRoles.textOnAccent, 3));
        CHECK(Matches(onLeft, tokens.accentRoles.fill, 3));

        visual.enabled = false;
        canvas.Begin();
        paint::PaintSwitch(canvas.Context(), track, 1.0f, visual, tokens);
        REQUIRE(canvas.End());
        CHECK(Matches(canvas.At(leftKnob, static_cast<UINT>(cy)), tokens.accentRoles.fillDisabled, 3));
        canvas.Begin();
        paint::PaintSwitch(canvas.Context(), track, 0.0f, visual, tokens);
        REQUIRE(canvas.End());
        CHECK(Matches(canvas.At(leftKnob, static_cast<UINT>(cy)), tokens.textDisabled, 3));

        // The palette itself: halfway is a blend, never a jump.
        const auto half = paint::SwitchPalette(tokens, 0.5f, paint::Visual{});
        CHECK(half.trackFill.a > 0.3f && half.trackFill.a < 0.8f);
    }
}

AETHER_TEST("ui_form: field wells are opaque in the exact field fill") {
    for (const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
        const auto tokens = graphics::MakeDesignTokens(mode);
        D2DCanvas canvas;
        REQUIRE(canvas.Create(120, 40, 144.0f));
        canvas.Begin();
        paint::PaintFieldWell(canvas.Context(), D2D1::RectF(4.0f, 4.0f, 76.0f, 24.0f), true, true, false, 1.0f / 1.5f, tokens);
        REQUIRE(canvas.End());
        const auto inside = canvas.At(60, 20);  // DIPs (40, 13.3) at 150 %
        TEST_NOTE("field interior " + Describe(inside));
        CHECK(Matches(inside, tokens.form.fieldFill, 1));
        CHECK_EQ(inside.a, 255);
        // The focused well carries the accent line along its bottom edge.
        CHECK(Matches(canvas.At(60, 34), tokens.accentRoles.fill, 4));

        canvas.Begin();
        paint::PaintFieldWell(canvas.Context(), D2D1::RectF(4.0f, 4.0f, 76.0f, 24.0f), false, false, false, 1.0f / 1.5f, tokens);
        REQUIRE(canvas.End());
        CHECK(Matches(canvas.At(60, 20), tokens.form.fieldFillDisabled, 1));
        CHECK(!Matches(canvas.At(60, 34), tokens.accentRoles.fill, 40));
    }
}

AETHER_TEST("ui_form: focus ring is drawn only when requested") {
    const auto tokens = graphics::MakeDesignTokens(ThemeMode::Dark);
    TextFixture text;
    REQUIRE(text.Create(tokens));
    D2DCanvas canvas;
    REQUIRE(canvas.Create(160, 60));
    const auto rect = D2D1::RectF(10.0f, 10.0f, 150.0f, 42.0f);
    paint::Visual visual;
    canvas.Begin();
    paint::PaintButton(canvas.Context(), text.cache, rect, L"&Apply", std::nullopt, ButtonStyle::Secondary, visual, tokens);
    REQUIRE(canvas.End());
    const auto without = canvas.At(80, 8);  // inside the focus margin above the button
    visual.focusRing = true;
    canvas.Begin();
    paint::PaintButton(canvas.Context(), text.cache, rect, L"&Apply", std::nullopt, ButtonStyle::Secondary, visual, tokens);
    REQUIRE(canvas.End());
    const auto with = canvas.At(80, 8);
    TEST_NOTE("margin without ring " + Describe(without) + " with ring " + Describe(with));
    CHECK_EQ(without.a, 0);
    CHECK(with.a > 150);
    // The ring stays within the focus margin (the window edge) and never paints beyond it.
    CHECK_EQ(canvas.At(80, 6).a, 0);
}

AETHER_TEST("ui_form: button styles are visually distinct") {
    const auto tokens = graphics::MakeDesignTokens(ThemeMode::Light);
    TextFixture text;
    REQUIRE(text.Create(tokens));
    D2DCanvas canvas;
    REQUIRE(canvas.Create(140, 50));
    const auto rect = D2D1::RectF(4.0f, 4.0f, 136.0f, 36.0f);
    const auto render = [&](ButtonStyle style, bool enabled) {
        paint::Visual visual;
        visual.enabled = enabled;
        canvas.Begin();
        paint::PaintButton(canvas.Context(), text.cache, rect, L"", std::nullopt, style, visual, tokens);
        (void)canvas.End();
        return canvas.At(20, 20);
    };
    const auto primary = render(ButtonStyle::Primary, true);
    const auto secondary = render(ButtonStyle::Secondary, true);
    const auto subtle = render(ButtonStyle::Subtle, true);
    const auto disabled = render(ButtonStyle::Primary, false);
    TEST_NOTE("primary " + Describe(primary) + " secondary " + Describe(secondary) + " subtle " + Describe(subtle) +
              " disabled " + Describe(disabled));
    CHECK(Matches(primary, tokens.accentRoles.fill, 2));
    CHECK(Matches(secondary, tokens.form.buttonFill, 2));
    CHECK_EQ(subtle.a, 0);  // transparent until hovered
    CHECK(Matches(disabled, tokens.accentRoles.fillDisabled, 2));
    // Danger uses the secondary surface with critical text.
    const auto danger = paint::ButtonPalette(tokens, ButtonStyle::Danger, paint::Visual{});
    CHECK(danger.text.r == tokens.critical.r && danger.text.g == tokens.critical.g);
    CHECK(danger.fill.a == tokens.form.buttonFill.a);
    // Hover is a different surface than rest.
    paint::Visual hover;
    hover.hover = 1.0f;
    CHECK(paint::ButtonPalette(tokens, ButtonStyle::Secondary, hover).fill.a != tokens.form.buttonFill.a);
}

AETHER_TEST("ui_form: acrylic background stays translucent, solid is opaque") {
    for (const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
        const auto tokens = graphics::MakeDesignTokens(mode);
        D2DCanvas canvas;
        REQUIRE(canvas.Create(64, 64));
        canvas.Begin();
        paint::PaintBackground(canvas.Context(), D2D1::RectF(0.0f, 0.0f, 64.0f, 64.0f), tokens, FormMaterial::Acrylic);
        REQUIRE(canvas.End());
        double alpha = 0.0;
        for (UINT y = 0; y < 64; ++y)
            for (UINT x = 0; x < 64; ++x) alpha += canvas.At(x, y).a;
        alpha /= 64.0 * 64.0 * 255.0;
        TEST_NOTE("acrylic mean alpha " + std::to_string(alpha));
        CHECK(std::fabs(alpha - tokens.form.windowTintAlpha) < 0.05);

        canvas.Begin();
        paint::PaintBackground(canvas.Context(), D2D1::RectF(0.0f, 0.0f, 64.0f, 64.0f), tokens, FormMaterial::Solid);
        REQUIRE(canvas.End());
        for (UINT y = 0; y < 64; y += 7)
            for (UINT x = 0; x < 64; x += 7) CHECK_EQ(canvas.At(x, y).a, 255);
    }
}

AETHER_TEST("ui_form: neutral Windows accent progress keeps visible determinate and activity segments") {
    D2DCanvas canvas;
    REQUIRE(canvas.Create(200, 40));
    const auto pixelColor = [](const CanvasPixel& pixel) {
        return graphics::Color{static_cast<float>(pixel.r) / 255.0f, static_cast<float>(pixel.g) / 255.0f,
                               static_cast<float>(pixel.b) / 255.0f, 1.0f};
    };
    for (const auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
        auto tokens = graphics::MakeDesignTokens(mode);
        tokens.accentRoles.fill = graphics::Color::FromHex(0x6B6B6B);
        const auto colors = paint::ProgressPalette(tokens, true);
        CHECK(graphics::ContrastRatio(colors.fill, colors.track) >= 3.0f);
        CHECK_NEAR(colors.fill.r, tokens.accentRoles.fill.r, 1e-6f);
        CHECK_NEAR(colors.fill.g, tokens.accentRoles.fill.g, 1e-6f);
        CHECK_NEAR(colors.fill.b, tokens.accentRoles.fill.b, 1e-6f);
        // The opaque track keeps this result independent of the bright/dark scene behind it.
        for (const float backdrop : {0.0f, 1.0f}) {
            for (int state = 0; state < 3; ++state) {
                canvas.Begin();
                canvas.Context()->Clear(D2D1::ColorF(backdrop, backdrop, backdrop, 1.0f));
                const std::optional<double> fraction = state == 0 ? std::optional<double>{0.5} : std::nullopt;
                paint::PaintProgress(canvas.Context(), D2D1::RectF(0.0f, 16.0f, 200.0f, 24.0f), fraction,
                                     state == 2 ? 0.5 : -1.0, true, tokens);
                REQUIRE(canvas.End());
                const auto active = canvas.At(state == 0 ? 50 : 100, 20);
                const auto track = canvas.At(175, 20);
                CHECK_EQ(active.a, 255);
                CHECK_EQ(track.a, 255);
                CHECK(Matches(active, tokens.accentRoles.fill, 1));
                CHECK(graphics::ContrastRatio(pixelColor(active), pixelColor(track)) >= 3.0f);
            }
        }
    }
}

AETHER_TEST("ui_form: check box, progress and list rows render their states") {
    const auto tokens = graphics::MakeDesignTokens(ThemeMode::Dark);
    TextFixture text;
    REQUIRE(text.Create(tokens));
    D2DCanvas canvas;
    REQUIRE(canvas.Create(200, 40));
    const auto row = D2D1::RectF(0.0f, 0.0f, 200.0f, 40.0f);
    canvas.Begin();
    paint::PaintCheckRow(canvas.Context(), text.cache, row, L"Enabled", L"", 1, paint::Visual{}, tokens);
    REQUIRE(canvas.End());
    const auto box = paint::CheckBoxRect(row, tokens, 20.0f);
    // A corner of the checked box (away from the check mark) is the accent fill.
    CHECK(Matches(canvas.At(static_cast<UINT>(box.left + 3.0f), static_cast<UINT>(box.bottom - 3.0f)), tokens.accentRoles.fill, 3));

    canvas.Begin();
    paint::PaintProgress(canvas.Context(), D2D1::RectF(0.0f, 16.0f, 200.0f, 24.0f), 0.5, 0.0, true, tokens);
    REQUIRE(canvas.End());
    CHECK(Matches(canvas.At(50, 20), tokens.accentRoles.fill, 3));
    CHECK(!Matches(canvas.At(150, 20), tokens.accentRoles.fill, 30));
    // Reduced motion: a static centred segment, never an invented value.
    canvas.Begin();
    paint::PaintProgress(canvas.Context(), D2D1::RectF(0.0f, 16.0f, 200.0f, 24.0f), std::nullopt, -1.0, true, tokens);
    REQUIRE(canvas.End());
    CHECK(Matches(canvas.At(100, 20), tokens.accentRoles.fill, 3));
    CHECK(!Matches(canvas.At(10, 20), tokens.accentRoles.fill, 30));

    canvas.Begin();
    paint::PaintListRow(canvas.Context(), text.cache, row, L"", false, false, false, true, tokens);
    REQUIRE(canvas.End());
    const auto plain = canvas.At(100, 20);
    CHECK(Matches(plain, tokens.form.fieldFill, 1));
    canvas.Begin();
    paint::PaintListRow(canvas.Context(), text.cache, row, L"", true, false, false, true, tokens);
    REQUIRE(canvas.End());
    const auto selected = canvas.At(100, 20);
    CHECK_EQ(selected.a, 255);  // composited onto the opaque fill
    CHECK(!Matches(selected, tokens.form.fieldFill, 4));
}
