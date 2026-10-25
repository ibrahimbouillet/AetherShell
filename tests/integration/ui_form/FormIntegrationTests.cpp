// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// ui_form integration tests on the real desktop with the test's own windows only. Pixel checks
// read our own windows over a private opaque backdrop (see FormTestSupport.h); inspection PNGs go
// to %TEMP%\AetherShellTests\ (form-*.png), never into the repository.
#include "AetherTest.h"
#include "FormTestSupport.h"

#include "graphics/FrameScheduler.h"
#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/WindowUtilities.h"
#include "ui/form/ConfirmDialog.h"
#include "ui/form/FormPaint.h"
#include "ui/form/FormScrollView.h"
#include "ui/form/FormSurface.h"

#include <commctrl.h>
#include <uiautomation.h>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace aether;
using namespace aether::ui::form;
using namespace aether::test::form;
using aether::graphics::ThemeMode;

namespace {

enum Id : int {
    kTitle = 100, kNav, kField, kApply, kSwitch, kCheck, kCombo, kSlider, kCanvasField, kHotkey, kProgress, kStatus,
    kHelper, kCheckNow, kList, kDocument, kBottom, kBusy, kScroll
};

struct ProbeForm {
    FormHost host;
    FormScrollView scroll;
    HWND title = nullptr, nav = nullptr, field = nullptr, apply = nullptr;
    HWND toggle = nullptr, check = nullptr, combo = nullptr, slider = nullptr, canvasField = nullptr, hotkey = nullptr;
    HWND progress = nullptr, status = nullptr, helper = nullptr, checkNow = nullptr, list = nullptr, document = nullptr;
    HWND bottom = nullptr, busy = nullptr;
    // DIP layout of the probe (form client 640 x 380).
    static constexpr float kWidth = 640.0f;
    static constexpr float kHeight = 380.0f;
    static constexpr D2D1_RECT_F kGlass{16.0f, 300.0f, 200.0f, 372.0f};  // nothing but the window material
    static constexpr D2D1_RECT_F kFieldFrame{16.0f, 208.0f, 200.0f, 240.0f};
    static constexpr D2D1_RECT_F kViewport{212.0f, 52.0f, 628.0f, 372.0f};
    static constexpr float kContent = 1000.0f;
};

bool InitControls() {
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_HOTKEY_CLASS};
    return ::InitCommonControlsEx(&init) != FALSE;
}

void Register(FormSurface& surface, HWND control, ControlRole role, const std::function<void(ControlSpec&)>& tune = {}) {
    REQUIRE(control != nullptr);
    ControlSpec spec;
    spec.role = role;
    if (tune) tune(spec);
    REQUIRE(surface.Register(control, spec).ok());
}

graphics::DesignTokens Tokens(ThemeMode mode, bool transparency) {
    auto tokens = graphics::MakeDesignTokens(mode);
    tokens.transparencyEnabled = transparency;
    return tokens;
}

void Build(ProbeForm& f, const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
           graphics::FrameScheduler* scheduler = nullptr) {
    REQUIRE(InitControls());
    auto& host = f.host;
    REQUIRE(host.Create(ProbeForm::kWidth, ProbeForm::kHeight));
    REQUIRE(host.surface.Attach(host.hwnd, tokens, motion, scheduler).ok());
    auto& s = host.surface;
    const HWND form = host.hwnd;

    f.title = host.Add(form, L"STATIC", L"Form probe", SS_OWNERDRAW, kTitle);
    Register(s, f.title, ControlRole::Label, [](ControlSpec& spec) { spec.text = TextRole::Title; });
    s.SetFrame(f.title, host.PxRect(24, 8, 616, 44));

    f.nav = host.Add(form, L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | WS_TABSTOP, kNav);
    for (const wchar_t* page : {L"General", L"Dock", L"Top Bar", L"About"}) ::SendMessageW(f.nav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(page));
    ::SendMessageW(f.nav, LB_SETCURSEL, 0, 0);
    Register(s, f.nav, ControlRole::Navigation);
    s.SetNavigationGlyphs(f.nav, {graphics::Glyph::General, graphics::Glyph::Dock, graphics::Glyph::TopBar, graphics::Glyph::About});
    s.SetFrame(f.nav, host.PxRect(12, 52, 200, 52 + 4 * 36));

    f.field = host.Add(form, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kField, WS_EX_CLIENTEDGE);
    Register(s, f.field, ControlRole::Field);
    s.SetFrame(f.field, host.PxRect(ProbeForm::kFieldFrame.left, ProbeForm::kFieldFrame.top, ProbeForm::kFieldFrame.right,
                                    ProbeForm::kFieldFrame.bottom));

    f.apply = host.Add(form, L"BUTTON", L"&Apply", WS_TABSTOP | BS_PUSHBUTTON, kApply);
    Register(s, f.apply, ControlRole::Button, [](ControlSpec& spec) { spec.button = ButtonStyle::Primary; });
    s.SetFrame(f.apply, host.PxRect(16, 252, 200, 284));

    REQUIRE(f.scroll.Create(s, form, kScroll).ok());
    const auto& v = ProbeForm::kViewport;
    f.scroll.SetFrame(host.PxRect(v.left, v.top, v.right, v.bottom));
    f.scroll.SetContentHeight(host.Px(ProbeForm::kContent));
    const HWND canvas = f.scroll.Canvas();

    f.toggle = host.Add(canvas, L"BUTTON", L"Show &seconds", WS_TABSTOP | BS_AUTOCHECKBOX, kSwitch);
    Register(s, f.toggle, ControlRole::Switch, [](ControlSpec& spec) { spec.description = L"Adds seconds to the clock"; });
    s.SetFrame(f.toggle, host.PxRect(4, 4, 392, 56));
    f.check = host.Add(canvas, L"BUTTON", L"Enable feature", WS_TABSTOP | BS_AUTOCHECKBOX, kCheck);
    Register(s, f.check, ControlRole::Check);
    s.SetFrame(f.check, host.PxRect(16, 64, 300, 96));
    f.combo = host.Add(canvas, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kCombo);
    for (const wchar_t* item : {L"Light", L"Dark", L"System"}) ::SendMessageW(f.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    ::SendMessageW(f.combo, CB_SETCURSEL, 1, 0);
    Register(s, f.combo, ControlRole::ComboBox);
    s.SetFrame(f.combo, host.PxRect(16, 104, 196, 136));
    f.slider = host.Add(canvas, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, kSlider);
    ::SendMessageW(f.slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    ::SendMessageW(f.slider, TBM_SETPOS, TRUE, 40);
    Register(s, f.slider, ControlRole::Slider);
    s.SetFrame(f.slider, host.PxRect(212, 104, 388, 136));
    f.canvasField = host.Add(canvas, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kCanvasField, WS_EX_CLIENTEDGE);
    Register(s, f.canvasField, ControlRole::Field);
    s.SetFrame(f.canvasField, host.PxRect(16, 144, 196, 176));
    f.hotkey = host.Add(canvas, HOTKEY_CLASSW, L"", WS_TABSTOP, kHotkey);
    ::SendMessageW(f.hotkey, HKM_SETHOTKEY, MAKEWORD('K', HOTKEYF_CONTROL | HOTKEYF_SHIFT), 0);
    Register(s, f.hotkey, ControlRole::Field);
    s.SetFrame(f.hotkey, host.PxRect(212, 144, 388, 176));
    f.progress = host.Add(canvas, PROGRESS_CLASSW, L"", 0, kProgress);
    ::SendMessageW(f.progress, PBM_SETRANGE32, 0, 100);
    ::SendMessageW(f.progress, PBM_SETPOS, 60, 0);
    Register(s, f.progress, ControlRole::Progress);
    s.SetFrame(f.progress, host.PxRect(16, 186, 388, 194));
    f.status = host.Add(canvas, L"STATIC", L"Everything is up to date", SS_OWNERDRAW, kStatus);
    Register(s, f.status, ControlRole::Label, [](ControlSpec& spec) {
        spec.text = TextRole::Status;
        spec.severity = Severity::Success;
        spec.wrap = false;
    });
    s.SetFrame(f.status, host.PxRect(16, 200, 388, 224));
    f.helper = host.Add(canvas, L"STATIC", L"Helper text explains the setting above in a secondary color.", SS_OWNERDRAW, kHelper);
    Register(s, f.helper, ControlRole::Label, [](ControlSpec& spec) { spec.text = TextRole::Helper; });
    s.SetFrame(f.helper, host.PxRect(16, 228, 388, 250));
    f.checkNow = host.Add(canvas, L"BUTTON", L"Check &now", WS_TABSTOP | BS_PUSHBUTTON, kCheckNow);
    Register(s, f.checkNow, ControlRole::Button, [](ControlSpec& spec) { spec.glyph = graphics::Glyph::Updates; });
    s.SetFrame(f.checkNow, host.PxRect(16, 256, 176, 288));
    f.list = host.Add(canvas, L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP | WS_BORDER, kList);
    for (const wchar_t* item : {L"Terminal", L"Browser", L"Editor", L"Mail", L"Music"}) ::SendMessageW(f.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    ::SendMessageW(f.list, LB_SETCURSEL, 1, 0);
    Register(s, f.list, ControlRole::List);
    s.SetFrame(f.list, host.PxRect(0, 316, 396, 436));
    f.document = host.Add(canvas, L"EDIT", L"Release notes\r\n\r\nA multiline read-only document inside an opaque well. "
                                           L"It wraps its text and scrolls vertically like any native edit control.",
                          ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP, kDocument, WS_EX_CLIENTEDGE);
    Register(s, f.document, ControlRole::Document);
    s.SetFrame(f.document, host.PxRect(0, 448, 396, 548));
    f.busy = host.Add(canvas, L"STATIC", L"Checking", SS_OWNERDRAW, kBusy);  // hidden: must never animate
    ::ShowWindow(f.busy, SW_HIDE);
    Register(s, f.busy, ControlRole::Label, [](ControlSpec& spec) {
        spec.text = TextRole::Status;
        spec.severity = Severity::Busy;
    });
    s.SetFrame(f.busy, host.PxRect(16, 600, 300, 624));
    f.bottom = host.Add(canvas, L"BUTTON", L"Bottom", WS_TABSTOP | BS_PUSHBUTTON, kBottom);
    Register(s, f.bottom, ControlRole::Button);
    s.SetFrame(f.bottom, host.PxRect(0, 940, 160, 972));

    // kGlass (bottom left) stays bare window material for the backdrop checks.
    s.SetCards({FormCard{host.PxRect(0, 0, 396, 300), canvas, false}});
    s.SetDividers({FormDivider{RECT{0, host.Px(560), host.Px(396), host.Px(560) + 1}, canvas}});
}

RECT InForm(HWND form, HWND child) {
    RECT r{};
    ::GetWindowRect(child, &r);
    ::MapWindowPoints(nullptr, form, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

Capture RenderOffscreen(FormSurface& surface, HWND hwnd) {
    RECT client{};
    ::GetClientRect(hwnd, &client);
    Capture capture;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = client.right;
    info.bmiHeader.biHeight = -client.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC screen = ::GetDC(nullptr);
    HDC memory = ::CreateCompatibleDC(screen);
    HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (memory && bitmap && bits) {
        HGDIOBJ old = ::SelectObject(memory, bitmap);
        surface.PaintRegion(hwnd, memory, client);
        ::GdiFlush();
        capture.width = client.right;
        capture.height = client.bottom;
        capture.bgra.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) + static_cast<std::size_t>(client.right) * client.bottom * 4);
        ::SelectObject(memory, old);
    }
    if (bitmap) ::DeleteObject(bitmap);
    if (memory) ::DeleteDC(memory);
    ::ReleaseDC(nullptr, screen);
    return capture;
}

// Sub-rectangle of a capture as its own capture.
Capture Crop(const Capture& source, const RECT& rect) {
    Capture crop;
    crop.width = rect.right - rect.left;
    crop.height = rect.bottom - rect.top;
    if (crop.width <= 0 || crop.height <= 0) return Capture{};
    crop.bgra.resize(static_cast<std::size_t>(crop.width) * crop.height * 4);
    for (int y = 0; y < crop.height; ++y)
        for (int x = 0; x < crop.width; ++x) {
            const auto* p = source.Pixel(rect.left + x, rect.top + y);
            std::copy(p, p + 4, &crop.bgra[(static_cast<std::size_t>(y) * crop.width + x) * 4]);
        }
    return crop;
}

int CountNear(const Capture& capture, const RECT& area, const graphics::Color& color, int tolerance) {
    int count = 0;
    for (int y = area.top; y < area.bottom; ++y)
        for (int x = area.left; x < area.right; ++x) count += PixelNear(capture.Pixel(x, y), color, tolerance) ? 1 : 0;
    return count;
}

bool AllNear(const Capture& capture, const RECT& area, const graphics::Color& color, int tolerance, std::string& worst) {
    for (int y = area.top; y < area.bottom; ++y)
        for (int x = area.left; x < area.right; ++x) {
            if (!PixelNear(capture.Pixel(x, y), color, tolerance)) {
                worst = DescribePixel(capture.Pixel(x, y)) + " at " + std::to_string(x) + "," + std::to_string(y);
                return false;
            }
        }
    return true;
}

RECT Deflated(RECT r, int dx, int dy) {
    ::InflateRect(&r, -dx, -dy);
    return r;
}

void WaitSettled(FormSurface& surface, DWORD limit = 2000) {
    Pump(limit, [&] { return !surface.Animating(); });
    Pump(60);
}

struct Scene {
    BackdropGuard backdrop;
    RECT guard{};
};

bool MakeBackdrop(Scene& scene, const FormHost& host) {
    // A private margin around the window for the backdrop blur, kept inside the safe band so the
    // owner's own Top Bar and Dock never fall inside the guarded area on small screens.
    const RECT bounds = host.WindowBounds();
    const RECT safe = SafeArea();
    scene.guard = bounds;
    ::InflateRect(&scene.guard, 80, 80);
    scene.guard.left = std::min(bounds.left, std::max(scene.guard.left, safe.left));
    scene.guard.top = std::min(bounds.top, std::max(scene.guard.top, safe.top));
    scene.guard.right = std::max(bounds.right, std::min(scene.guard.right, safe.right));
    scene.guard.bottom = std::max(bounds.bottom, std::min(scene.guard.bottom, safe.bottom));
    scene.backdrop.hwnd = CreateTestBackdrop(scene.guard);
    return scene.backdrop.hwnd != nullptr;
}

// Field interior checks: the well around the editor (painted by its parent) and the editor itself.
void CheckFieldInterior(const Capture& a, const Capture& b, HWND form, HWND editor, const RECT& frame, const graphics::Color& fill,
                        const char* label) {
    const RECT edit = InForm(form, editor);
    const RECT well{frame.left + 4, frame.top + 6, std::max(frame.left + 5, edit.left - 3), frame.bottom - 6};
    const RECT inner{edit.right - 24, edit.top + 2, edit.right - 4, edit.bottom - 2};
    std::string worst;
    CHECK_EQ(MaxDifference(a, b, well), 0);
    CHECK_EQ(MaxDifference(a, b, inner), 0);
    const bool wellOk = AllNear(a, well, fill, 2, worst);
    if (!wellOk) TEST_NOTE(std::string(label) + " well pixel " + worst);
    CHECK(wellOk);
    const bool innerOk = AllNear(a, inner, fill, 2, worst);
    if (!innerOk) TEST_NOTE(std::string(label) + " editor pixel " + worst);
    CHECK(innerOk);
}

void RunMaterialProbe(ThemeMode mode, const char* name) {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    const bool acrylicPossible = SystemBackdropSupported() && platform::SystemTransparencyEnabled();
    graphics::MotionPolicy materialMotion;
    materialMotion.reduced = true;  // material equality is independent of the separate motion gates
    ProbeForm f;
    Build(f, Tokens(mode, acrylicPossible), materialMotion);
    Scene scene;
    REQUIRE(MakeBackdrop(scene, f.host));
    f.host.Show();
    ::SendMessageW(f.field, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"Typed text"));
    Pump(900);
    {
        PBRANGE range{};
        ::SendMessageW(f.progress, PBM_GETRANGE, FALSE, reinterpret_cast<LPARAM>(&range));
        TEST_NOTE("progress position " + std::to_string(::SendMessageW(f.progress, PBM_GETPOS, 0, 0)) + " range " +
                  std::to_string(range.iLow) + ".." + std::to_string(range.iHigh));
        CHECK_EQ(::SendMessageW(f.progress, PBM_GETPOS, 0, 0), 60);
    }
    const auto& tokens = f.host.surface.Tokens();
    const HWND form = f.host.hwnd;
    const RECT client = f.host.ClientOnScreen();
    const auto& g = ProbeForm::kGlass;
    const RECT glass = Deflated(f.host.PxRect(g.left, g.top, g.right, g.bottom), 6, 6);
    const RECT fieldFrame = f.host.surface.Frame(f.field);
    RECT canvasFieldFrame = f.host.surface.Frame(f.canvasField);
    ::MapWindowPoints(f.scroll.Canvas(), form, reinterpret_cast<POINT*>(&canvasFieldFrame), 2);
    RECT hotkeyFrame = f.host.surface.Frame(f.hotkey);
    ::MapWindowPoints(f.scroll.Canvas(), form, reinterpret_cast<POINT*>(&hotkeyFrame), 2);

    if (!acrylicPossible) {
        TEST_NOTE("Acrylic skipped: the system backdrop needs Windows 11 22H2+ with transparency effects on");
    } else {
        CHECK_EQ(f.host.surface.Material(), FormMaterial::Acrylic);
        // Our half of the material, independent of window activation: the client scene is
        // premultiplied-translucent over the glass and opaque inside text fields.
        const auto offscreen = RenderOffscreen(f.host.surface, form);
        REQUIRE(offscreen.Valid());
        int translucent = 0, samples = 0;
        for (int y = glass.top; y < glass.bottom; y += 3) {
            for (int x = glass.left; x < glass.right; x += 3) {
                ++samples;
                if (offscreen.Pixel(x, y)[3] < 250) ++translucent;
            }
        }
        TEST_NOTE("translucent client samples over the glass " + std::to_string(translucent) + " of " + std::to_string(samples));
        CHECK(translucent * 10 >= samples * 9);
        const RECT well{fieldFrame.left + 4, fieldFrame.top + 6, fieldFrame.left + 8, fieldFrame.bottom - 6};
        bool opaqueWell = true;
        for (int y = well.top; y < well.bottom; ++y)
            for (int x = well.left; x < well.right; ++x) opaqueWell = opaqueWell && offscreen.Pixel(x, y)[3] == 255;
        CHECK(opaqueWell);
        // Windows draws an inactive window's Acrylic as its flat fallback. A test may not take the
        // foreground from the owner's applications, so the live see-through check only runs when
        // this window is active; otherwise the fallback is recorded honestly.
        const bool active = ::GetForegroundWindow() == form;
        if (!active) TEST_NOTE("form inactive: Windows shows the inactive Acrylic fallback; live transparency not sampled");
        SetBackdropPhase(scene.backdrop.hwnd, false);
        Pump(400);
        const auto first = CaptureWithRetry(scene.backdrop.hwnd, scene.guard, client);
        SetBackdropPhase(scene.backdrop.hwnd, true);
        Pump(500);
        const auto second = CaptureWithRetry(scene.backdrop.hwnd, scene.guard, client);
        if (!first.Valid() || !second.Valid()) {
            TEST_NOTE("inconclusive: a foreign window kept overlapping the private backdrop");
        } else {
            const auto path = InspectionPath(std::string("form-acrylic-") + name);
            CHECK(SavePng(first, path));
            CHECK(SavePng(second, InspectionPath(std::string("form-acrylic-") + name + "-phase2")));
            TEST_NOTE("saved " + path.string());
            const double glassDelta = MeanDifference(first, second, glass);
            TEST_NOTE("acrylic glass mean RGB delta between backdrop phases " + std::to_string(glassDelta));
            if (active) CHECK(glassDelta > 4.0);  // genuine transparency: the scene behind shows through
            CheckFieldInterior(first, second, form, f.field, fieldFrame, tokens.form.fieldFill, "form field");
            CheckFieldInterior(first, second, form, f.canvasField, canvasFieldFrame, tokens.form.fieldFill, "canvas field");
            CheckFieldInterior(first, second, form, f.hotkey, hotkeyFrame, tokens.form.fieldFill, "hotkey");
            const RECT edit = InForm(form, f.field);
            const int textPixels = CountNear(first, RECT{edit.left, edit.top, edit.left + f.host.Px(60), edit.bottom},
                                             tokens.form.fieldText, 60);
            TEST_NOTE("text-colored pixels in the typed field " + std::to_string(textPixels));
            CHECK(textPixels > 10);
        }
    }

    // Solid (transparency off): an honest opaque window, identical over either backdrop phase.
    f.host.surface.SetTheme(Tokens(mode, false), materialMotion);
    CHECK_EQ(f.host.surface.Material(), FormMaterial::Solid);
    // The switch toggles to its accent state; after settling the scene owns every pixel.
    ::SendMessageW(f.toggle, BM_CLICK, 0, 0);
    WaitSettled(f.host.surface);
    SetBackdropPhase(scene.backdrop.hwnd, false);
    Pump(500);
    const auto first = CaptureWithRetry(scene.backdrop.hwnd, scene.guard, client);
    SetBackdropPhase(scene.backdrop.hwnd, true);
    Pump(500);
    const auto second = CaptureWithRetry(scene.backdrop.hwnd, scene.guard, client);
    if (!first.Valid() || !second.Valid()) {
        TEST_NOTE("inconclusive: a foreign window kept overlapping the private backdrop");
        return;
    }
    const auto path = InspectionPath(std::string("form-solid-") + name);
    CHECK(SavePng(first, path));
    CHECK(SavePng(second, InspectionPath(std::string("form-solid-") + name + "-phase2")));
    TEST_NOTE("saved " + path.string());
    // Windows rounds the window's bottom corners: those few client pixels show the real backdrop.
    const int corner = f.host.Px(12.0f);
    int solidDelta = MaxDifference(first, second, RECT{0, 0, first.width, first.height - corner});
    solidDelta = std::max(solidDelta, MaxDifference(first, second, RECT{corner, first.height - corner, first.width - corner, first.height}));
    TEST_NOTE("solid max RGB delta between backdrop phases " + std::to_string(solidDelta));
    CHECK(solidDelta <= 1);
    std::string worst;
    const bool glassOk = AllNear(first, glass, tokens.form.windowSolid, 6, worst);  // grain only with transparency on
    if (!glassOk) TEST_NOTE("solid background pixel " + worst);
    CHECK(glassOk);
    CheckFieldInterior(first, second, form, f.field, fieldFrame, tokens.form.fieldFill, "solid field");
    CheckFieldInterior(first, second, form, f.hotkey, hotkeyFrame, tokens.form.fieldFill, "solid hotkey");

    // Scene authority: what is on screen for each scene-painted control equals an offscreen render
    // of the same region (no stock pixels survived).
    for (HWND control : {f.title, f.nav, f.apply, f.toggle, f.check, f.combo, f.slider, f.progress, f.status, f.checkNow}) {
        const RECT r = InForm(form, control);
        RECT visible{};
        RECT viewport = InForm(form, f.scroll.Viewport());
        if (::IsChild(f.scroll.Canvas(), control)) {
            if (!::IntersectRect(&visible, &r, &viewport)) continue;
        } else {
            visible = r;
        }
        // Compare the current scene, bracketed by identical offscreen renders, rather than a
        // capture made before another backdrop phase and all preceding controls were inspected.
        Capture screen, offscreen;
        RECT capturePx = r;
        ::MapWindowPoints(form, nullptr, reinterpret_cast<POINT*>(&capturePx), 2);
        bool stable = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            const auto before = RenderOffscreen(f.host.surface, control);
            screen = CaptureWithRetry(scene.backdrop.hwnd, scene.guard, capturePx);
            offscreen = RenderOffscreen(f.host.surface, control);
            if (!before.Valid() || !screen.Valid() || !offscreen.Valid()) break;
            stable = MaxDifference(before, offscreen, RECT{0, 0, offscreen.width, offscreen.height}) == 0;
            if (stable) break;
            Pump(100);
        }
        REQUIRE(stable);
        REQUIRE(screen.Valid());
        REQUIRE(offscreen.Valid());
        RECT compare{visible.left - r.left, visible.top - r.top, visible.right - r.left, visible.bottom - r.top};
        const double delta = MeanDifference(screen, offscreen, compare);
        const int worstDelta = MaxDifference(screen, offscreen, compare);
        if (delta > 0.5 || worstDelta > 24) {
            TEST_NOTE("control " + std::to_string(::GetDlgCtrlID(control)) + " screen/offscreen mean " + std::to_string(delta) +
                      " max " + std::to_string(worstDelta));
            const auto suffix = std::string("form-scene-") + name + "-" + std::to_string(::GetDlgCtrlID(control));
            CHECK(SavePng(screen, InspectionPath(suffix + "-screen")));
            CHECK(SavePng(offscreen, InspectionPath(suffix + "-offscreen")));
        }
        CHECK(delta <= 0.5);
    }
    // The switch track (where the knob was) is the accent now.
    RECT toggle = InForm(form, f.toggle);
    const auto track = paint::SwitchTrackRect(D2D1::RectF(0, 0, f.host.surface.ToDip(static_cast<float>(toggle.right - toggle.left)),
                                                          f.host.surface.ToDip(static_cast<float>(toggle.bottom - toggle.top))),
                                              tokens);
    const auto* knobPixel = first.Pixel(toggle.left + f.host.Px(track.left + 4.0f), toggle.top + f.host.Px((track.top + track.bottom) / 2.0f));
    TEST_NOTE("switch track after BM_CLICK " + DescribePixel(knobPixel));
    CHECK(PixelNear(knobPixel, tokens.accentRoles.fill, 3));
}

}  // namespace

AETHER_TEST("ui_form integration: dark acrylic is translucent, fields opaque, solid identical") {
    RunMaterialProbe(ThemeMode::Dark, "dark");
}

AETHER_TEST("ui_form integration: light acrylic is translucent, fields opaque, solid identical") {
    RunMaterialProbe(ThemeMode::Light, "light");
}

AETHER_TEST("ui_form integration: notifications, switch toggling and native dropdown state") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    ProbeForm f;
    Build(f, Tokens(ThemeMode::Dark, true), graphics::MotionPolicy{});
    f.host.Show();
    Pump(300);
    auto& s = f.host.surface;
    const auto& tokens = s.Tokens();

    // Switch: BM_CLICK toggles the native state, BN_CLICKED reaches the form through the canvas.
    CHECK_EQ(::SendMessageW(f.toggle, BM_GETCHECK, 0, 0), BST_UNCHECKED);
    RECT client{};
    ::GetClientRect(f.toggle, &client);
    const auto row = D2D1::RectF(0, 0, s.ToDip(static_cast<float>(client.right)), s.ToDip(static_cast<float>(client.bottom)));
    const auto track = paint::SwitchTrackRect(row, tokens);
    const int sampleX = f.host.Px(track.left + 4.0f);
    const int sampleY = f.host.Px((track.top + track.bottom) / 2.0f);
    const auto before = RenderOffscreen(s, f.toggle);
    ::SendMessageW(f.toggle, BM_CLICK, 0, 0);
    CHECK_EQ(::SendMessageW(f.toggle, BM_GETCHECK, 0, 0), BST_CHECKED);
    CHECK_EQ(f.host.Count(WM_COMMAND, kSwitch, BN_CLICKED), 1);
    CHECK(s.Animating());  // the knob slides
    WaitSettled(s);
    CHECK_FALSE(s.Animating());
    const auto after = RenderOffscreen(s, f.toggle);
    const auto premultiplied = [](const graphics::Color& c) {
        return graphics::Color{c.r * c.a, c.g * c.a, c.b * c.a, 1.0f};
    };
    TEST_NOTE("switch track before " + DescribePixel(before.Pixel(sampleX, sampleY)) + " after " + DescribePixel(after.Pixel(sampleX, sampleY)));
    CHECK_FALSE(PixelNear(before.Pixel(sampleX, sampleY), premultiplied(tokens.accentRoles.fill), 20));
    CHECK(PixelNear(after.Pixel(sampleX, sampleY), premultiplied(tokens.accentRoles.fill), 3));

    // EN_CHANGE from an editor inside the canvas.
    ::SendMessageW(f.canvasField, WM_CHAR, L'x', 1);
    CHECK(f.host.Count(WM_COMMAND, kCanvasField, EN_CHANGE) >= 1);
    // Track bar notifications travel to the form as well.
    ::SendMessageW(f.slider, TBM_SETPOSNOTIFY, 0, 70);
    CHECK(f.host.Count(WM_HSCROLL, kSlider, TB_THUMBPOSITION) + f.host.Count(WM_HSCROLL, kSlider, TB_ENDTRACK) >= 1);
    CHECK_EQ(::SendMessageW(f.slider, TBM_GETPOS, 0, 0), 70);
    // Selection changes of a list inside the canvas reach the form (LBN_SELCHANGE via a click).
    RECT item{};
    ::SendMessageW(f.list, LB_GETITEMRECT, 3, reinterpret_cast<LPARAM>(&item));
    ::SendMessageW(f.list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(item.left + 10, item.top + 4));
    ::SendMessageW(f.list, WM_LBUTTONUP, 0, MAKELPARAM(item.left + 10, item.top + 4));
    CHECK_EQ(::SendMessageW(f.list, LB_GETCURSEL, 0, 0), 3);
    CHECK(f.host.Count(WM_COMMAND, kList, LBN_SELCHANGE) >= 1);

    // Keep the real list and its native UIA provider on every opening route.
    ::SendMessageW(f.combo, CB_SHOWDROPDOWN, TRUE, 0);
    CHECK_EQ(::SendMessageW(f.combo, CB_GETDROPPEDSTATE, 0, 0), TRUE);
    ::SendMessageW(f.combo, CB_SHOWDROPDOWN, FALSE, 0);
    Pump(100);
    CHECK_EQ(::SendMessageW(f.combo, CB_GETDROPPEDSTATE, 0, 0), FALSE);
    // Keyboard selection on the closed combo stays native, with CBN_SELCHANGE.
    const int changes = f.host.Count(WM_COMMAND, kCombo, CBN_SELCHANGE);
    ::SendMessageW(f.combo, WM_KEYDOWN, VK_DOWN, 0);
    CHECK_EQ(::SendMessageW(f.combo, CB_GETCURSEL, 0, 0), 2);
    CHECK_EQ(f.host.Count(WM_COMMAND, kCombo, CBN_SELCHANGE), changes + 1);

    // Navigation: the indicator slides to the new row and settles.
    ::SendMessageW(f.nav, LB_SETCURSEL, 2, 0);
    ::InvalidateRect(f.nav, nullptr, FALSE);
    CHECK(s.Animating());
    WaitSettled(s);
    CHECK_FALSE(s.Animating());

    // Switch description is the accessible help text (verified through UIA in the scroll test too).
    CHECK(s.Spec(f.toggle) != nullptr);
    CHECK(s.Spec(f.toggle)->description == L"Adds seconds to the clock");
}

AETHER_TEST("ui_form integration: scaled list rows fit text and reduced motion settles in-flight feedback") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    ProbeForm f;
    auto tokens = Tokens(ThemeMode::Dark, true);
    Build(f, tokens, graphics::MotionPolicy{});
    f.host.Show();
    Pump(150);
    auto& surface = f.host.surface;
    const LRESULT originalList = ::SendMessageW(f.list, LB_GETITEMHEIGHT, 0, 0);
    const LRESULT originalNav = ::SendMessageW(f.nav, LB_GETITEMHEIGHT, 0, 0);
    tokens.textScale = 2.25f;
    surface.SetTheme(tokens, graphics::MotionPolicy{});
    const int textHeight = static_cast<int>(std::ceil(surface.MeasureText(L"Ag", TextRole::Body, 500.0f).height * surface.ToPx(1.0f)));
    CHECK(::SendMessageW(f.list, LB_GETITEMHEIGHT, 0, 0) > originalList);
    CHECK(::SendMessageW(f.nav, LB_GETITEMHEIGHT, 0, 0) > originalNav);
    CHECK(::SendMessageW(f.list, LB_GETITEMHEIGHT, 0, 0) >= textHeight + f.host.Px(12.0f));
    CHECK(::SendMessageW(f.nav, LB_GETITEMHEIGHT, 0, 0) >= textHeight + f.host.Px(12.0f));
    ::SendMessageW(f.toggle, BM_CLICK, 0, 0);
    f.scroll.ScrollTo(100, true);
    REQUIRE(surface.Animating());
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    surface.SetTheme(tokens, reduced);
    CHECK_FALSE(surface.Animating());
    CHECK_EQ(f.scroll.Position(), 100);
    Pump(100);
    CHECK_FALSE(surface.Animating());
}

AETHER_TEST("ui_form integration: theme switches keep text, selection, focus and scroll position") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    ProbeForm f;
    Build(f, Tokens(ThemeMode::Dark, true), graphics::MotionPolicy{});
    f.host.Show();
    Pump(300);
    auto& s = f.host.surface;
    ::SendMessageW(f.canvasField, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"Draft value"));
    ::SetFocus(f.canvasField);
    ::SendMessageW(f.canvasField, EM_SETSEL, 2, 7);
    ::SendMessageW(f.combo, CB_SETCURSEL, 2, 0);
    ::SendMessageW(f.list, LB_SETCURSEL, 3, 0);
    ::SendMessageW(f.nav, LB_SETCURSEL, 1, 0);
    f.scroll.ScrollTo(f.host.Px(150));
    Pump(100);
    const HWND focus = ::GetFocus();
    CHECK(focus == f.canvasField);
    const int position = f.scroll.Position();
    CHECK(position > 0);
    const auto expectState = [&](const char* step) {
        wchar_t text[64]{};
        ::GetWindowTextW(f.canvasField, text, 64);
        DWORD start = 0;
        DWORD end = 0;
        ::SendMessageW(f.canvasField, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        const bool ok = std::wstring(text) == L"Draft value" && start == 2 && end == 7 && ::GetFocus() == focus &&
                        ::SendMessageW(f.combo, CB_GETCURSEL, 0, 0) == 2 && ::SendMessageW(f.list, LB_GETCURSEL, 0, 0) == 3 &&
                        ::SendMessageW(f.nav, LB_GETCURSEL, 0, 0) == 1 && f.scroll.Position() == position;
        if (!ok) TEST_NOTE(std::string("state lost after ") + step);
        CHECK(ok);
    };
    const auto layered = [](HWND hwnd) { return (::GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0; };
    CHECK(layered(f.canvasField));
    CHECK_FALSE(::GetWindowLongPtrW(f.canvasField, GWL_EXSTYLE) & WS_EX_CLIENTEDGE);

    s.SetTheme(Tokens(ThemeMode::Light, true), graphics::MotionPolicy{});
    Pump(200);
    expectState("light");
    CHECK(layered(f.canvasField));

    s.SetTheme(graphics::MakeDesignTokens(ThemeMode::HighContrast), graphics::MotionPolicy{});
    Pump(200);
    expectState("high contrast");
    CHECK_EQ(s.Material(), FormMaterial::Native);
    CHECK_FALSE(layered(f.canvasField));  // stock editor again
    CHECK(::GetWindowLongPtrW(f.canvasField, GWL_EXSTYLE) & WS_EX_CLIENTEDGE);
    CHECK(::GetWindowLongPtrW(f.list, GWL_STYLE) & WS_BORDER);

    s.SetTheme(Tokens(ThemeMode::Dark, true), graphics::MotionPolicy{});
    Pump(200);
    expectState("dark");
    CHECK(layered(f.canvasField));
    CHECK(s.Material() != FormMaterial::Native);
}

AETHER_TEST("ui_form integration: settled forms are idle (no timers, no frames)") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    ProbeForm f;
    Build(f, Tokens(ThemeMode::Dark, true), graphics::MotionPolicy{});
    f.host.Show();
    auto& s = f.host.surface;
    WaitSettled(s);
    // Feedback animations run and stop.
    ::SendMessageW(f.toggle, BM_CLICK, 0, 0);
    ::SendMessageW(f.nav, LB_SETCURSEL, 3, 0);
    s.FadeIn(f.scroll.Canvas());
    CHECK(s.Animating());
    WaitSettled(s);
    CHECK_FALSE(s.Animating());
    auto stats = Pump(500);
    TEST_NOTE("WM_TIMER messages while settled: " + std::to_string(stats.timers));
    CHECK_EQ(stats.timers, 0);
    CHECK_FALSE(s.Animating());
    stats = Pump(400);
    CHECK_EQ(stats.timers, 0);
    CHECK_EQ(stats.nativeTimers, 0);  // native progress timers were killed and never recur

    // An indeterminate progress bar animates only while visible.
    s.SetProgress(f.progress, std::nullopt);
    CHECK(s.Animating());
    Pump(200);
    ::ShowWindow(f.progress, SW_HIDE);
    Pump(100);
    CHECK_FALSE(s.Animating());
    stats = Pump(500);
    CHECK_EQ(stats.timers, 0);
    ::ShowWindow(f.progress, SW_SHOW);
    Pump(50);
    CHECK(s.Animating());
    s.SetProgress(f.progress, 0.25);
    CHECK_EQ(::SendMessageW(f.progress, PBM_GETPOS, 0, 0), 25);
    WaitSettled(s);
    stats = Pump(500);
    CHECK_EQ(stats.timers, 0);

    // Reduced motion: state changes are instant.
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    s.SetTheme(s.Tokens(), reduced);
    ::SendMessageW(f.toggle, BM_CLICK, 0, 0);
    CHECK_FALSE(s.Animating());
    s.SetProgress(f.progress, std::nullopt);
    CHECK_FALSE(s.Animating());  // a static segment, no sweep
    stats = Pump(400);
    CHECK_EQ(stats.timers, 0);
}

AETHER_TEST("ui_form integration: scheduler-driven animation settles and goes idle") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    platform::MessageLoop loop;
    graphics::FrameScheduler scheduler;
    REQUIRE(scheduler.Initialize(loop).ok());
    {
        ProbeForm f;
        Build(f, Tokens(ThemeMode::Light, true), graphics::MotionPolicy{}, &scheduler);
        f.host.Show();
        ::SendMessageW(f.toggle, BM_CLICK, 0, 0);
        CHECK(f.host.surface.Animating());
        int timers = 0;
        const double start = graphics::FrameScheduler::Now();
        loop.RunNested([&] { return graphics::FrameScheduler::Now() - start > 0.8; },
                       [&](MSG& message) {
                           if (message.message == WM_TIMER && FormOwnsTimer(message.hwnd)) {
                               wchar_t name[64]{};
                               ::GetClassNameW(message.hwnd, name, 64);
                               // The native progress bar's pre-registration timers fire once each.
                               if (std::wstring_view(name) != PROGRESS_CLASSW) ++timers;
                           }
                           return false;
                       });
        CHECK_FALSE(f.host.surface.Animating());
        CHECK_EQ(timers, 0);  // frames came from the scheduler, not from a timer
    }
    scheduler.Shutdown();
}

namespace {

void NeedHr(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " failed");
}

}  // namespace

AETHER_TEST("ui_form integration: native combo UI Automation expands, selects and collapses") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    ProbeForm f;
    Build(f, Tokens(ThemeMode::Dark, true), graphics::MotionPolicy{});
    auto comboSpec = *f.host.surface.Spec(f.combo);
    comboSpec.accessibleName = L"Display language";
    f.host.surface.SetSpec(f.combo, comboSpec);
    f.host.Show();
    Pump(100);
    const HWND combo = f.combo;
    std::atomic<bool> finished{false};
    std::string failure;
    ExpandCollapseState expanded = ExpandCollapseState_LeafNode;
    ExpandCollapseState collapsed = ExpandCollapseState_LeafNode;
    int selectedCount = 0;
    std::thread client([&] {
        const HRESULT init = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            NeedHr(init, "CoInitializeEx");
            winrt::com_ptr<IUIAutomation> automation;
            NeedHr(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()), "CUIAutomation");
            winrt::com_ptr<IUIAutomationElement> element;
            NeedHr(automation->ElementFromHandle(combo, element.put()), "ElementFromHandle(combo)");
            BSTR name = nullptr;
            NeedHr(element->get_CurrentName(&name), "combo accessible name");
            const bool named = name && std::wstring_view(name) == L"Display language";
            ::SysFreeString(name);
            if (!named) throw std::runtime_error("combo explicit label is unavailable");
            winrt::com_ptr<IUIAutomationExpandCollapsePattern> expand;
            NeedHr(element->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, __uuidof(IUIAutomationExpandCollapsePattern), expand.put_void()), "combo ExpandCollapse");
            NeedHr(expand->Expand(), "Expand");
            NeedHr(expand->get_CurrentExpandCollapseState(&expanded), "expanded state");
            VARIANT type{};
            type.vt = VT_I4;
            type.lVal = UIA_ListItemControlTypeId;
            winrt::com_ptr<IUIAutomationCondition> condition;
            NeedHr(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, condition.put()), "list-item condition");
            winrt::com_ptr<IUIAutomationElementArray> choices;
            NeedHr(element->FindAll(TreeScope_Subtree, condition.get(), choices.put()), "combo list items");
            int count = 0;
            NeedHr(choices->get_Length(&count), "list length");
            if (count < 2) throw std::runtime_error("combo has no accessible choice subtree");
            winrt::com_ptr<IUIAutomationElement> second;
            NeedHr(choices->GetElement(1, second.put()), "second choice");
            winrt::com_ptr<IUIAutomationSelectionItemPattern> selection;
            NeedHr(second->GetCurrentPatternAs(UIA_SelectionItemPatternId, __uuidof(IUIAutomationSelectionItemPattern), selection.put_void()), "choice SelectionItem");
            NeedHr(selection->Select(), "Select");
            BOOL selected = FALSE;
            NeedHr(selection->get_CurrentIsSelected(&selected), "selected state");
            selectedCount = selected ? 1 : 0;
            NeedHr(expand->Collapse(), "Collapse");
            NeedHr(expand->get_CurrentExpandCollapseState(&collapsed), "collapsed state");
        } catch (const std::exception& ex) {
            failure = ex.what();
        }
        if (SUCCEEDED(init)) ::CoUninitialize();
        finished.store(true);
    });
    Pump(20000, [&] { return finished.load(); });
    client.join();
    if (!failure.empty()) TEST_NOTE("UIA combo: " + failure);
    REQUIRE(failure.empty());
    CHECK_EQ(expanded, ExpandCollapseState_Expanded);
    CHECK_EQ(collapsed, ExpandCollapseState_Collapsed);
    CHECK_EQ(selectedCount, 1);
    CHECK_EQ(::SendMessageW(combo, CB_GETCURSEL, 0, 0), 1);
    CHECK_EQ(::SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0), FALSE);
}

AETHER_TEST("ui_form integration: scroll view wheel, codes, reveal and UI Automation") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    ProbeForm f;
    Build(f, Tokens(ThemeMode::Dark, true), graphics::MotionPolicy{});
    f.host.Show();
    Pump(300);
    auto& scroll = f.scroll;
    const int page = scroll.Page();
    CHECK_EQ(page, f.host.Px(ProbeForm::kViewport.bottom) - f.host.Px(ProbeForm::kViewport.top));
    CHECK_EQ(scroll.Range(), f.host.Px(ProbeForm::kContent) - page);
    CHECK_EQ(scroll.GutterPx(), f.host.Px(8.0f + 2.0f * 3.0f));
    CHECK(::IsWindowVisible(::GetWindow(scroll.Viewport(), GW_CHILD)) != FALSE);

    // Wheel on the viewport and on a control inside the canvas (DefWindowProc forwards it up).
    int notified = 0;
    scroll.scrolled = [&] { ++notified; };
    const RECT viewport = InForm(f.host.hwnd, scroll.Viewport());
    POINT center{(viewport.left + viewport.right) / 2, (viewport.top + viewport.bottom) / 2};
    ::ClientToScreen(f.host.hwnd, &center);
    ::SendMessageW(scroll.Viewport(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(center.x, center.y));
    Pump(500);
    const int afterWheel = scroll.Position();
    CHECK(afterWheel > 0);
    CHECK(notified > 0);
    ::SendMessageW(f.check, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(center.x, center.y));
    Pump(500);
    CHECK(scroll.Position() > afterWheel);

    // Legacy scroll codes.
    CHECK(scroll.HandleScrollCode(SB_BOTTOM));
    CHECK_EQ(scroll.Position(), scroll.Range());
    ::SendMessageW(scroll.Viewport(), WM_VSCROLL, SB_TOP, 0);
    CHECK_EQ(scroll.Position(), 0);
    CHECK_FALSE(scroll.HandleScrollCode(SB_THUMBTRACK));
    CHECK(scroll.HandleScrollCode(SB_THUMBTRACK, 40));
    CHECK_EQ(scroll.Position(), 40);
    scroll.ScrollTo(0);

    // EnsureVisible / RevealFocus bring the bottom control into view.
    const RECT bottom = f.host.surface.Frame(f.bottom);
    CHECK(scroll.EnsureVisible(bottom, 12));
    CHECK(bottom.bottom + 12 <= scroll.Position() + page);
    CHECK_FALSE(scroll.EnsureVisible(bottom, 12));
    scroll.ScrollTo(0);
    ::SetFocus(f.bottom);
    Pump(150);  // keyboard/programmatic focus posts a reveal request to the viewport
    CHECK(scroll.Position() > 0);
    CHECK(bottom.bottom <= scroll.Position() + page);
    scroll.ScrollTo(0);
    CHECK(scroll.RevealFocus(12));
    CHECK(bottom.bottom + 12 <= scroll.Position() + page);

    // The canvas moved, so its controls are painted at their new position in the form.
    RECT moved = InForm(f.host.hwnd, f.bottom);
    CHECK(moved.bottom <= viewport.bottom + 1);

    // UI Automation Scroll pattern through an in-process MTA client.
    scroll.ScrollTo(300);
    const int range = scroll.Range();
    const HWND hwnd = scroll.Viewport();
    const HWND toggle = f.toggle;
    std::atomic<bool> finished{false};
    std::string failure;
    double percent = -2.0;
    double viewSize = -2.0;
    BOOL vertical = FALSE;
    BOOL horizontal = TRUE;
    double horizontalPercent = 0.0;
    HRESULT invalidHorizontal = S_OK;
    std::wstring help;
    std::thread client([&] {
        const HRESULT init = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            NeedHr(init, "CoInitializeEx");
            winrt::com_ptr<IUIAutomation> automation;
            NeedHr(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()),
                   "CUIAutomation");
            winrt::com_ptr<IUIAutomationElement> element;
            NeedHr(automation->ElementFromHandle(hwnd, element.put()), "ElementFromHandle");
            winrt::com_ptr<IUIAutomationScrollPattern> pattern;
            NeedHr(element->GetCurrentPatternAs(UIA_ScrollPatternId, __uuidof(IUIAutomationScrollPattern), pattern.put_void()),
                   "GetCurrentPatternAs");
            if (!pattern) throw std::runtime_error("no scroll pattern");
            NeedHr(pattern->get_CurrentVerticallyScrollable(&vertical), "VerticallyScrollable");
            NeedHr(pattern->get_CurrentHorizontallyScrollable(&horizontal), "HorizontallyScrollable");
            NeedHr(pattern->get_CurrentVerticalScrollPercent(&percent), "VerticalScrollPercent");
            NeedHr(pattern->get_CurrentHorizontalScrollPercent(&horizontalPercent), "HorizontalScrollPercent");
            NeedHr(pattern->get_CurrentVerticalViewSize(&viewSize), "VerticalViewSize");
            NeedHr(pattern->SetScrollPercent(UIA_ScrollPatternNoScroll, 50.0), "SetScrollPercent");
            invalidHorizontal = pattern->SetScrollPercent(10.0, 50.0);
            NeedHr(pattern->Scroll(ScrollAmount_NoAmount, ScrollAmount_SmallIncrement), "Scroll");
            winrt::com_ptr<IUIAutomationElement> toggleElement;
            NeedHr(automation->ElementFromHandle(toggle, toggleElement.put()), "ElementFromHandle(toggle)");
            BSTR text = nullptr;
            NeedHr(toggleElement->get_CurrentHelpText(&text), "HelpText");
            help = text ? text : L"";
            ::SysFreeString(text);
        } catch (const std::exception& ex) {
            failure = ex.what();
        }
        if (SUCCEEDED(init)) ::CoUninitialize();
        finished.store(true);
    });
    Pump(20000, [&] { return finished.load(); });
    client.join();
    if (!failure.empty()) TEST_NOTE("UIA client: " + failure);
    REQUIRE(failure.empty());
    CHECK(vertical != FALSE);
    CHECK(horizontal == FALSE);
    CHECK_NEAR(horizontalPercent, UIA_ScrollPatternNoScroll, 1e-9);
    CHECK_NEAR(percent, 100.0 * 300 / range, 0.01);
    CHECK_NEAR(viewSize, 100.0 * page / f.host.Px(ProbeForm::kContent), 0.01);
    CHECK(FAILED(invalidHorizontal));
    CHECK_EQ(scroll.Position(), std::min(range, static_cast<int>(std::lround(range * 0.5)) + f.host.Px(48.0f)));
    TEST_NOTE("switch help text through UIA: " + aether::test::NarrowForDisplay(help));
    CHECK(help == L"Adds seconds to the clock");
}

namespace {

struct DialogProbe {
    bool visible = false;
    bool ownerDisabled = false;
    bool focusOnDefault = false;
    HWND dialog = nullptr;
};

bool OwnWindow(HWND hwnd) {
    DWORD pid = 0;
    if (hwnd) ::GetWindowThreadProcessId(hwnd, &pid);
    return hwnd && pid == ::GetCurrentProcessId();
}

// Keyboard input goes to the focused control; while the dialog is inactive (the test process does
// not own the foreground) it is posted to the control that would have focus.
HWND FocusOr(HWND dialog, int id) {
    const HWND focus = ::GetFocus();
    return focus && ::IsChild(dialog, focus) ? focus : ::GetDlgItem(dialog, id);
}

ConfirmRequest MakeRequest(HWND owner) {
    ConfirmRequest request;
    request.owner = owner;
    request.windowTitle = L"Aether Shell test";
    request.title = L"Reset all settings?";
    request.message = L"Every setting returns to its default value. Pinned applications are kept.";
    request.detail = L"This cannot be undone.";
    request.severity = Severity::Warning;
    request.primary = L"&Reset";
    request.secondary = L"&Keep";
    request.cancel = L"Cancel";
    request.dangerousPrimary = true;
    request.defaultChoice = ConfirmChoice::Primary;
    return request;
}

ConfirmChoice RunDialog(const ConfirmRequest& request, const graphics::DesignTokens& tokens, graphics::FrameScheduler* scheduler,
                        DialogProbe& probe, const std::function<void(HWND)>& act) {
    SetConfirmationTestHook([&](HWND dialog) {
        probe.dialog = dialog;
        probe.visible = ::IsWindowVisible(dialog) != FALSE;
        probe.ownerDisabled = request.owner && !::IsWindowEnabled(request.owner);
        // The default button has focus once the dialog is active; an inactive dialog (another
        // application owns the foreground) must not take activation by setting focus.
        probe.focusOnDefault = ::GetActiveWindow() != dialog || ::GetFocus() == ::GetDlgItem(dialog, IDOK);
        act(dialog);
    });
    auto result = ShowConfirmation(request, tokens, graphics::MotionPolicy{}, scheduler);
    SetConfirmationTestHook({});
    REQUIRE(result.ok());
    return result.value();
}

}  // namespace

AETHER_TEST("ui_form integration: confirmation dialog keyboard, buttons, owner and quit") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    REQUIRE(InitControls());
    FormHost owner;
    REQUIRE(owner.Create(560.0f, 360.0f, L"Confirmation owner"));
    REQUIRE(owner.surface.Attach(owner.hwnd, Tokens(ThemeMode::Dark, true), graphics::MotionPolicy{}).ok());
    Scene scene;
    REQUIRE(MakeBackdrop(scene, owner));
    owner.Show();
    Pump(200);
    const HWND foreground = ::GetForegroundWindow();
    const auto dark = Tokens(ThemeMode::Dark, SystemBackdropSupported() && platform::SystemTransparencyEnabled());
    const auto request = MakeRequest(owner.hwnd);

    // Enter chooses the default (initially focused) button; inspection PNG of the dialog.
    DialogProbe probe;
    auto choice = RunDialog(request, dark, nullptr, probe, [&](HWND dialog) {
        Pump(700);
        RECT bounds{};
        ::GetWindowRect(dialog, &bounds);
        const auto capture = CaptureWithRetry(scene.backdrop.hwnd, scene.guard, bounds);
        if (capture.Valid()) {
            const auto path = InspectionPath("form-confirmation-dark");
            CHECK(SavePng(capture, path));
            TEST_NOTE("saved " + path.string());
        } else {
            TEST_NOTE("confirmation capture inconclusive (foreign overlay)");
        }
        ::PostMessageW(FocusOr(dialog, IDOK), WM_KEYDOWN, VK_RETURN, 0);
    });
    CHECK(probe.visible);
    CHECK(probe.ownerDisabled);
    CHECK(probe.focusOnDefault);
    CHECK_EQ(choice, ConfirmChoice::Primary);
    CHECK(::IsWindowEnabled(owner.hwnd) != FALSE);
    CHECK_FALSE(::IsWindow(probe.dialog));
    {
        const auto own = [](HWND hwnd) { DWORD pid = 0; ::GetWindowThreadProcessId(hwnd, &pid); return pid == ::GetCurrentProcessId(); };
        const HWND now = ::GetForegroundWindow();
        TEST_NOTE(std::string("foreground before: ") + (foreground == owner.hwnd ? "owner" : own(foreground) ? "own other" : foreground ? "foreign" : "none") +
                  ", after: " + (now == owner.hwnd ? "owner" : own(now) ? "own other" : now ? "foreign" : "none"));
    }
    // Never took activation from another application (which may itself change meanwhile).
    CHECK(!OwnWindow(::GetForegroundWindow()) || OwnWindow(foreground));

    TEST_NOTE(std::string("foreground own before step: ") + (OwnWindow(::GetForegroundWindow()) ? "yes" : "no"));
    // Escape chooses Cancel.
    choice = RunDialog(request, dark, nullptr, probe, [](HWND dialog) {
        TEST_NOTE(std::string("escape step shown, foreground own: ") + (OwnWindow(::GetForegroundWindow()) ? "yes" : "no") +
                  ", dialog active: " + (::GetActiveWindow() == dialog ? "yes" : "no"));
        ::PostMessageW(FocusOr(dialog, IDOK), WM_KEYDOWN, VK_ESCAPE, 0);
    });
    TEST_NOTE(std::string("escape step closed, foreground own: ") + (OwnWindow(::GetForegroundWindow()) ? "yes" : "no"));
    CHECK_EQ(choice, ConfirmChoice::Cancel);
    CHECK(::IsWindowEnabled(owner.hwnd) != FALSE);

    TEST_NOTE(std::string("foreground own before step: ") + (OwnWindow(::GetForegroundWindow()) ? "yes" : "no"));
    // The secondary button's click notification chooses Secondary (BN_CLICKED: a simulated BM_CLICK
    // would activate the dialog like a real click and take the foreground from the owner's apps).
    choice = RunDialog(request, dark, nullptr, probe, [](HWND dialog) { ::PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(3, BN_CLICKED), reinterpret_cast<LPARAM>(::GetDlgItem(dialog, 3))); });
    CHECK_EQ(choice, ConfirmChoice::Secondary);

    TEST_NOTE(std::string("foreground own before step: ") + (OwnWindow(::GetForegroundWindow()) ? "yes" : "no"));
    // The close box chooses Cancel; a Cancel default is what Enter picks.
    choice = RunDialog(request, dark, nullptr, probe, [](HWND dialog) { ::PostMessageW(dialog, WM_SYSCOMMAND, SC_CLOSE, 0); });
    CHECK_EQ(choice, ConfirmChoice::Cancel);
    auto cancelDefault = request;
    cancelDefault.defaultChoice = ConfirmChoice::Cancel;
    choice = RunDialog(cancelDefault, dark, nullptr, probe, [](HWND dialog) { ::PostMessageW(FocusOr(dialog, IDCANCEL), WM_KEYDOWN, VK_RETURN, 0); });
    CHECK_EQ(choice, ConfirmChoice::Cancel);

    TEST_NOTE(std::string("foreground own before step: ") + (OwnWindow(::GetForegroundWindow()) ? "yes" : "no"));
    // Without a cancel button, Escape chooses the last button.
    auto noCancel = request;
    noCancel.cancel.clear();
    choice = RunDialog(noCancel, dark, nullptr, probe, [](HWND dialog) { ::PostMessageW(dialog, WM_KEYDOWN, VK_ESCAPE, 0); });
    CHECK_EQ(choice, ConfirmChoice::Secondary);

    TEST_NOTE(std::string("foreground own before step: ") + (OwnWindow(::GetForegroundWindow()) ? "yes" : "no"));
    // WM_QUIT ends the dialog with Cancel and is re-posted for the outer loop.
    choice = RunDialog(request, dark, nullptr, probe, [](HWND) { ::PostQuitMessage(7); });
    CHECK_EQ(choice, ConfirmChoice::Cancel);
    MSG quit{};
    CHECK(::PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE);
    CHECK_EQ(quit.wParam, static_cast<WPARAM>(7));
    CHECK(::IsWindowEnabled(owner.hwnd) != FALSE);

    TEST_NOTE(std::string("foreground own before step: ") + (OwnWindow(::GetForegroundWindow()) ? "yes" : "no"));
    // The scheduler's nested loop keeps frames running; light theme inspection PNG.
    platform::MessageLoop loop;
    graphics::FrameScheduler scheduler;
    REQUIRE(scheduler.Initialize(loop).ok());
    auto light = Tokens(ThemeMode::Light, SystemBackdropSupported() && platform::SystemTransparencyEnabled());
    choice = RunDialog(request, light, &scheduler, probe, [&](HWND dialog) {
        Pump(700);
        RECT bounds{};
        ::GetWindowRect(dialog, &bounds);
        const auto capture = CaptureWithRetry(scene.backdrop.hwnd, scene.guard, bounds);
        if (capture.Valid()) {
            const auto path = InspectionPath("form-confirmation-light");
            CHECK(SavePng(capture, path));
            TEST_NOTE("saved " + path.string());
        }
        ::PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(::GetDlgItem(dialog, IDCANCEL)));
    });
    CHECK_EQ(choice, ConfirmChoice::Cancel);
    CHECK(probe.ownerDisabled);
    CHECK(::IsWindowEnabled(owner.hwnd) != FALSE);
    scheduler.Shutdown();
    CHECK(!OwnWindow(::GetForegroundWindow()) || OwnWindow(foreground));

    // A long, text-scaled confirmation scrolls its body while every action stays in the work area.
    auto longRequest = request;
    longRequest.message.clear();
    for (int line = 0; line < 100; ++line) longRequest.message += L"Readable confirmation detail remains available.\r\n";
    auto enlarged = dark;
    enlarged.textScale = 2.25f;
    choice = RunDialog(longRequest, enlarged, nullptr, probe, [](HWND dialog) {
        MONITORINFO info{sizeof(info)};
        CHECK(::GetMonitorInfoW(::MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &info) != FALSE);
        RECT bounds{};
        ::GetWindowRect(dialog, &bounds);
        CHECK(bounds.left >= info.rcWork.left - 1);
        CHECK(bounds.top >= info.rcWork.top - 1);
        CHECK(bounds.right <= info.rcWork.right + 1);
        CHECK(bounds.bottom <= info.rcWork.bottom + 1);
        for (const int id : {IDOK, 3, IDCANCEL}) {
            RECT button{};
            REQUIRE(::GetWindowRect(::GetDlgItem(dialog, id), &button) != FALSE);
            CHECK(button.left >= bounds.left);
            CHECK(button.right <= bounds.right);
            CHECK(button.bottom <= info.rcWork.bottom);
        }
        const HWND viewport = ::GetDlgItem(dialog, 1000);
        REQUIRE(viewport != nullptr);
        HWND canvas = ::FindWindowExW(viewport, nullptr, L"AetherShell.FormScroll.Canvas", nullptr);
        REQUIRE(canvas != nullptr);
        RECT before{}, after{};
        ::GetWindowRect(canvas, &before);
        ::SendMessageW(viewport, WM_VSCROLL, SB_BOTTOM, 0);
        ::GetWindowRect(canvas, &after);
        CHECK(after.top < before.top);
        ::PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(::GetDlgItem(dialog, IDCANCEL)));
    });
    CHECK_EQ(choice, ConfirmChoice::Cancel);
}
