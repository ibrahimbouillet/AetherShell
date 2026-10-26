// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Moving-frame coherence of the shared form scroll path (FR-037 SF-01/SF-02). A dense own-window
// fixture scrolls with real wheel bursts, a reversal and fractional (touchpad-like) deltas while a
// second thread captures the composed screen continuously. Every captured frame is checked for
// registration between three independently presented layers of each content row:
//   * a magenta alignment bar drawn by the form decorator (painted by whichever window repaints it),
//   * a Primary button (a registered child window that paints its own visual),
//   * the text of a native EDIT (an opaque layered child inside a drawn well).
// In a coherent frame all three keep their rest offsets. Captures cover only the test's own window
// over its private opaque backdrop (FormTestSupport.h); inspection PNGs stay in
// %TEMP%\AetherShellTests\ (form-motion-*.png) and never enter the repository.
#include "AetherTest.h"
#include "FormTestSupport.h"

#include "graphics/FrameScheduler.h"
#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/WindowUtilities.h"
#include "ui/form/FormScrollView.h"
#include "ui/form/FormSurface.h"

#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::ui::form;
using namespace aether::test::form;
using aether::graphics::ThemeMode;

namespace {

constexpr float kFormWidth = 600.0f;
constexpr float kFormHeight = 400.0f;
constexpr D2D1_RECT_F kViewport{16.0f, 12.0f, 584.0f, 388.0f};
constexpr int kRows = 18;
constexpr float kRowPitch = 120.0f;
// Canvas DIP geometry of each row's probes (all share the vertical band kProbeTop..kProbeBottom).
constexpr float kBarLeft = 16.0f, kBarRight = 28.0f;
constexpr float kButtonLeft = 40.0f, kButtonRight = 200.0f;
constexpr float kFieldLeft = 300.0f, kFieldRight = 520.0f;
constexpr float kProbeTop = 44.0f, kProbeBottom = 76.0f;

float RowTop(int row) { return 8.0f + static_cast<float>(row) * kRowPitch; }

// The application's nested loop sleeps until a message or a frame arrives; an idle form requests
// neither. A periodic waitable timer keeps the test's script and deadline checks running.
void RunNested(platform::MessageLoop& loop, const std::function<bool()>& done) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
    REQUIRE(timer != nullptr);
    LARGE_INTEGER due{};
    due.QuadPart = -10'000;  // 1 ms, then every 4 ms
    REQUIRE(::SetWaitableTimer(timer, &due, 4, nullptr, nullptr, FALSE) != FALSE);
    const auto id = loop.AddWaitHandle(timer, [] {});
    loop.RunNested(done);
    loop.RemoveWaitHandle(id);
    ::CancelWaitableTimer(timer);
    ::CloseHandle(timer);
}

struct MotionForm {
    FormHost host;
    FormScrollView scroll;
};

void Register(FormSurface& surface, HWND control, ControlRole role, const std::function<void(ControlSpec&)>& tune = {}) {
    REQUIRE(control != nullptr);
    ControlSpec spec;
    spec.role = role;
    if (tune) tune(spec);
    REQUIRE(surface.Register(control, spec).ok());
}

void BuildMotionForm(MotionForm& f, const graphics::DesignTokens& tokens, graphics::FrameScheduler& scheduler) {
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    REQUIRE(::InitCommonControlsEx(&init) != FALSE);
    auto& host = f.host;
    REQUIRE(host.Create(kFormWidth, kFormHeight, L"Aether motion probe"));
    REQUIRE(host.surface.Attach(host.hwnd, tokens, graphics::MotionPolicy{}, &scheduler).ok());
    auto& s = host.surface;
    REQUIRE(f.scroll.Create(s, host.hwnd, 900).ok());
    f.scroll.SetFrame(host.PxRect(kViewport.left, kViewport.top, kViewport.right, kViewport.bottom));
    f.scroll.SetContentHeight(host.Px(RowTop(kRows) + 8.0f));
    const HWND canvas = f.scroll.Canvas();
    std::vector<FormCard> cards;
    for (int row = 0; row < kRows; ++row) {
        const float top = RowTop(row);
        HWND label = host.Add(canvas, L"STATIC", (L"Content row " + std::to_wstring(row + 1)).c_str(), SS_OWNERDRAW, 1000 + row);
        Register(s, label, ControlRole::Label, [](ControlSpec& spec) { spec.text = TextRole::Strong; spec.wrap = false; });
        s.SetFrame(label, host.PxRect(40.0f, top + 10.0f, 280.0f, top + 34.0f));
        HWND button = host.Add(canvas, L"BUTTON", L"Action", WS_TABSTOP | BS_PUSHBUTTON, 2000 + row);
        Register(s, button, ControlRole::Button, [](ControlSpec& spec) { spec.button = ButtonStyle::Primary; });
        s.SetFrame(button, host.PxRect(kButtonLeft, top + kProbeTop, kButtonRight, top + kProbeBottom));
        HWND field = host.Add(canvas, L"EDIT", L"MMMMMMMMMMMM", WS_TABSTOP | ES_AUTOHSCROLL, 3000 + row, WS_EX_CLIENTEDGE);
        Register(s, field, ControlRole::Field);
        s.SetFrame(field, host.PxRect(kFieldLeft, top + kProbeTop, kFieldRight, top + kProbeBottom));
        if (row % 3 == 0) {
            HWND check = host.Add(canvas, L"BUTTON", L"Option", WS_TABSTOP | BS_AUTOCHECKBOX, 4000 + row);
            Register(s, check, ControlRole::Check);
            s.SetFrame(check, host.PxRect(40.0f, top + 80.0f, 280.0f, top + 108.0f));
        } else if (row % 3 == 1) {
            HWND slider = host.Add(canvas, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 5000 + row);
            ::SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            ::SendMessageW(slider, TBM_SETPOS, TRUE, 30 + row);
            Register(s, slider, ControlRole::Slider);
            s.SetFrame(slider, host.PxRect(300.0f, top + 80.0f, 520.0f, top + 108.0f));
        } else {
            HWND helper = host.Add(canvas, L"STATIC", L"Helper text stays registered with its card while the page moves.",
                                   SS_OWNERDRAW, 6000 + row);
            Register(s, helper, ControlRole::Label, [](ControlSpec& spec) { spec.text = TextRole::Helper; spec.wrap = false; });
            s.SetFrame(helper, host.PxRect(40.0f, top + 82.0f, 520.0f, top + 104.0f));
        }
        cards.push_back(FormCard{host.PxRect(8.0f, top + 2.0f, 536.0f, top + 112.0f), canvas, false});
    }
    s.SetCards(std::move(cards));
    // The alignment bars live in canvas coordinates; whichever window repaints a bar's pixels draws it
    // at that window's current idea of the canvas position. Clipped to the viewport like About's mark.
    s.SetDecorator([&f](ID2D1DeviceContext* target, const D2D1_RECT_F&, const graphics::DesignTokens&) {
        const HWND canvas = f.scroll.Canvas();
        if (!canvas) return;
        const POINT origin = f.host.surface.ClientOriginPx(canvas);
        const float scale = f.host.surface.ToPx(1.0f);
        const float ox = static_cast<float>(origin.x) / scale;
        const float oy = static_cast<float>(origin.y) / scale;
        const RECT frame = f.scroll.Frame();
        winrt::com_ptr<ID2D1SolidColorBrush> magenta;
        if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 1.0f, 1.0f), magenta.put()))) return;
        target->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(frame.left) / scale, static_cast<float>(frame.top) / scale,
                                                static_cast<float>(frame.right) / scale, static_cast<float>(frame.bottom) / scale),
                                    D2D1_ANTIALIAS_MODE_ALIASED);
        for (int row = 0; row < kRows; ++row) {
            const float top = RowTop(row);
            target->FillRectangle(D2D1::RectF(ox + kBarLeft, oy + top + kProbeTop, ox + kBarRight, oy + top + kProbeBottom), magenta.get());
        }
        target->PopAxisAlignedClip();
    });
}

// --- frame analysis ---------------------------------------------------------------------------------

struct Probe {
    int barX0 = 0, barX1 = 0;        // capture pixels
    int buttonX0 = 0, buttonX1 = 0;  // a stretch of the button fill free of its label
    int fieldX0 = 0, fieldX1 = 0;
    int barHeight = 0;
    std::uint8_t accent[3]{};        // calibrated button fill (BGR)
    std::uint8_t well[3]{};          // calibrated field background (BGR)
    int textOffset = 0;              // rest offset of the field text top below the bar top
};

bool Near(const std::uint8_t* pixel, const std::uint8_t* reference, int tolerance) {
    return std::abs(pixel[0] - reference[0]) <= tolerance && std::abs(pixel[1] - reference[1]) <= tolerance &&
           std::abs(pixel[2] - reference[2]) <= tolerance;
}
bool Magenta(const std::uint8_t* pixel) { return pixel[2] > 200 && pixel[1] < 70 && pixel[0] > 200; }

// Rows whose majority of pixels in [x0, x1) satisfy `test`, merged into runs [top, bottom).
template <class Test>
std::vector<std::pair<int, int>> Runs(const Capture& c, int x0, int x1, Test test) {
    std::vector<std::pair<int, int>> runs;
    int start = -1;
    for (int y = 0; y <= c.height; ++y) {
        bool hit = false;
        if (y < c.height) {
            int count = 0;
            for (int x = x0; x < x1; ++x) count += test(c.Pixel(x, y)) ? 1 : 0;
            hit = count * 2 > (x1 - x0);
        }
        if (hit && start < 0) start = y;
        if (!hit && start >= 0) {
            runs.emplace_back(start, y);
            start = -1;
        }
    }
    return runs;
}

std::optional<int> TextTop(const Capture& c, const Probe& p, int from, int to) {
    for (int y = std::max(0, from); y < std::min(c.height, to); ++y) {
        int count = 0;
        for (int x = p.fieldX0; x < p.fieldX1; ++x) {
            const auto* pixel = c.Pixel(x, y);
            const int difference = std::abs(pixel[0] - p.well[0]) + std::abs(pixel[1] - p.well[1]) + std::abs(pixel[2] - p.well[2]);
            if (difference > 180) ++count;
        }
        if (count >= 3) return y;
    }
    return std::nullopt;
}

struct FrameResult {
    int rows = 0;       // complete rows checked
    int violations = 0; // misregistered, missing, stretched or stray pieces
    std::string first;  // description of the first violation
};

FrameResult Analyse(const Capture& c, const Probe& p) {
    FrameResult result;
    const auto note = [&](const std::string& text) {
        ++result.violations;
        if (result.first.empty()) result.first = text;
    };
    const auto bars = Runs(c, p.barX0, p.barX1, Magenta);
    const auto buttons = Runs(c, p.buttonX0, p.buttonX1, [&](const std::uint8_t* pixel) { return Near(pixel, p.accent, 40); });
    std::vector<bool> matched(buttons.size(), false);
    for (const auto& [top, bottom] : bars) {
        if (top <= 0 || bottom >= c.height) continue;  // clipped at the viewport edge
        ++result.rows;
        if (std::abs((bottom - top) - p.barHeight) > 2) note("bar height " + std::to_string(bottom - top) + " at y " + std::to_string(top));
        std::optional<std::size_t> button;
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            if (std::abs(buttons[i].first - top) <= p.barHeight) {
                if (!button || std::abs(buttons[i].first - top) < std::abs(buttons[*button].first - top)) button = i;
            }
        }
        if (!button) {
            note("button missing beside bar at y " + std::to_string(top));
        } else {
            matched[*button] = true;
            const int offset = buttons[*button].first - top;
            if (std::abs(offset) > 1) note("button offset " + std::to_string(offset) + " at y " + std::to_string(top));
        }
        const auto text = TextTop(c, p, top - p.barHeight / 2, bottom + p.barHeight / 2);
        if (!text) {
            note("field text missing beside bar at y " + std::to_string(top));
        } else if (std::abs(*text - top - p.textOffset) > 1) {
            note("field text offset " + std::to_string(*text - top) + " (rest " + std::to_string(p.textOffset) + ") at y " +
                 std::to_string(top));
        }
    }
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const auto [top, bottom] = buttons[i];
        if (matched[i] || top <= 0 || bottom >= c.height) continue;
        if (std::abs((bottom - top) - p.barHeight) <= 2) note("stray button piece at y " + std::to_string(top));
    }
    return result;
}

struct CaptureLog {
    std::mutex mutex;
    int frames = 0;
    int distinct = 0;
    int rejected = 0;       // guard failed (a foreign window passed over the area)
    int rows = 0;
    int badFrames = 0;
    std::vector<std::string> failures;
    std::vector<Capture> samples;  // a few frames for local review, including bad ones
    std::vector<double> intervals; // seconds between distinct frames
};

bool SameFrame(const Capture& a, const Capture& b) {
    return a.width == b.width && a.height == b.height && a.bgra == b.bgra;
}

void RunScrollCoherence(ThemeMode mode, const char* tag, bool capture = true) {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    platform::MessageLoop loop;
    graphics::FrameScheduler scheduler;
    REQUIRE(scheduler.Initialize(loop).ok());
    {
        auto tokens = graphics::MakeDesignTokens(mode);
        tokens.transparencyEnabled = SystemBackdropSupported() && platform::SystemTransparencyEnabled();
        MotionForm f;
        BuildMotionForm(f, tokens, scheduler);
        RECT bounds = f.host.WindowBounds();
        BackdropGuard backdrop;
        const int margin = f.host.Px(12.0f);
        backdrop.area = RECT{bounds.left - margin, bounds.top - margin, bounds.right + margin, bounds.bottom + margin};
        backdrop.hwnd = CreateTestBackdrop(backdrop.area);
        REQUIRE(backdrop.hwnd != nullptr);
        f.host.Show();
        const auto settle = [&](double seconds) {
            const double start = graphics::FrameScheduler::Now();
            RunNested(loop, [&] { return graphics::FrameScheduler::Now() - start > seconds; });
        };
        settle(0.6);

        RECT viewport{};
        ::GetWindowRect(f.scroll.Viewport(), &viewport);
        viewport.right -= f.scroll.GutterPx();  // the overlay indicator is not part of the probe
        const auto rest = CaptureWithRetry(backdrop.hwnd, backdrop.area, viewport);
        REQUIRE(rest.Valid());
        Probe probe;
        probe.barX0 = f.host.Px(kBarLeft + 2.0f);
        probe.barX1 = f.host.Px(kBarRight - 2.0f);
        probe.buttonX0 = f.host.Px(kButtonLeft + 4.0f);
        probe.buttonX1 = f.host.Px(kButtonLeft + 18.0f);
        probe.fieldX0 = f.host.Px(kFieldLeft + 8.0f);
        probe.fieldX1 = f.host.Px(kFieldRight - 8.0f);
        probe.barHeight = f.host.Px(kProbeBottom - kProbeTop);
        // Calibrate the fills and the text offset of the first complete row at rest.
        const auto bars = Runs(rest, probe.barX0, probe.barX1, Magenta);
        REQUIRE(!bars.empty());
        const auto [barTop, barBottom] = bars.front();
        const int middle = (barTop + barBottom) / 2;
        std::copy_n(rest.Pixel(f.host.Px(kButtonLeft + 10.0f), middle), 3, probe.accent);
        std::copy_n(rest.Pixel(f.host.Px(kFieldRight - 4.0f), barBottom - f.host.Px(4.0f)), 3, probe.well);
        const auto text = TextTop(rest, probe, barTop - probe.barHeight / 2, barBottom + probe.barHeight / 2);
        REQUIRE(text.has_value());
        probe.textOffset = *text - barTop;
        const auto calibration = Analyse(rest, probe);
        TEST_NOTE(std::string(tag) + " rest: rows " + std::to_string(calibration.rows) + ", violations " +
                  std::to_string(calibration.violations) + (calibration.first.empty() ? "" : " (" + calibration.first + ")"));
        REQUIRE(calibration.rows >= 2);
        REQUIRE_EQ(calibration.violations, 0);

        CaptureLog log;
        std::atomic<bool> stop{false};
        std::vector<double> applies;
        f.scroll.scrolled = [&] { applies.push_back(graphics::FrameScheduler::Now()); };
        f.host.surface.ResetStats();
        std::thread capturer([&] {
            if (!capture) return;
            Capture previous;
            double last = graphics::FrameScheduler::Now();
            while (!stop.load()) {
                auto frame = CapturePrivate(backdrop.hwnd, backdrop.area, viewport);
                std::lock_guard lock(log.mutex);
                if (!frame.Valid()) {
                    ++log.rejected;
                    continue;
                }
                ++log.frames;
                if (previous.Valid() && SameFrame(previous, frame)) continue;
                const double now = graphics::FrameScheduler::Now();
                if (previous.Valid()) log.intervals.push_back(now - last);
                last = now;
                ++log.distinct;
                const auto result = Analyse(frame, probe);
                log.rows += result.rows;
                if (result.violations) {
                    ++log.badFrames;
                    if (log.failures.size() < 12) log.failures.push_back(result.first);
                    if (log.samples.size() < 24) log.samples.push_back(frame);
                } else if (log.samples.size() < 8) {
                    log.samples.push_back(frame);
                }
                previous = std::move(frame);
            }
        });

        // Real wheel input as the Settings window forwards it: a notch burst, a reversal while the
        // first motion is still running, a fast burst and fractional precision-touchpad deltas.
        struct Step {
            double at;
            int delta;
        };
        const std::vector<Step> steps{{0.00, -120}, {0.05, -120}, {0.10, -120}, {0.15, -120}, {0.42, 120}, {0.45, 120},
                                      {0.70, -120}, {0.72, -120}, {0.74, -120}, {0.76, -120}, {0.78, -120}, {0.80, -120},
                                      {1.10, -40},  {1.116, -40}, {1.132, -40}, {1.148, -40}, {1.164, -40}, {1.18, -40},
                                      {1.196, -40}, {1.212, -40}, {1.228, -40}, {1.244, -40}};
        std::size_t next = 0;
        const int before = f.scroll.Position();
        const double start = graphics::FrameScheduler::Now();
        RunNested(loop, [&] {
            const double elapsed = graphics::FrameScheduler::Now() - start;
            while (next < steps.size() && steps[next].at <= elapsed) {
                POINT center{(viewport.left + viewport.right) / 2, (viewport.top + viewport.bottom) / 2};
                ::PostMessageW(f.scroll.Viewport(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(steps[next].delta))),
                               MAKELPARAM(center.x, center.y));
                ++next;
            }
            return elapsed > 1.9;
        });
        stop.store(true);
        capturer.join();
        const int after = f.scroll.Position();
        TEST_NOTE(std::string(tag) + " moved " + std::to_string(before) + " -> " + std::to_string(after) + " px; frames " +
                  std::to_string(log.frames) + ", distinct " + std::to_string(log.distinct) + ", rejected " +
                  std::to_string(log.rejected) + ", rows checked " + std::to_string(log.rows) + ", incoherent frames " +
                  std::to_string(log.badFrames));
        if (!log.intervals.empty()) {
            auto sorted = log.intervals;
            std::sort(sorted.begin(), sorted.end());
            const auto at = [&](double q) { return sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(q * static_cast<double>(sorted.size())))]; };
            TEST_NOTE(std::string(tag) + " distinct-frame interval ms p50 " + std::to_string(at(0.5) * 1000.0) + " p95 " +
                      std::to_string(at(0.95) * 1000.0) + " max " + std::to_string(sorted.back() * 1000.0) +
                      " (capture-thread observation, not compositor telemetry)");
        }
        {
            const auto stats = f.host.surface.Stats();
            const auto percentile = [&](double q) {
                const auto wanted = static_cast<std::uint64_t>(std::ceil(q * static_cast<double>(stats.frames)));
                std::uint64_t seen = 0;
                for (std::size_t i = 0; i < stats.histogram.size(); ++i) {
                    seen += stats.histogram[i];
                    if (seen >= wanted) return 0.5 * static_cast<double>(i + 1);
                }
                return 0.5 * static_cast<double>(stats.histogram.size());
            };
            if (stats.frames) {
                TEST_NOTE(std::string(tag) + " UI present work: " + std::to_string(stats.frames) + " frames, mean " +
                          std::to_string(stats.totalSeconds * 1000.0 / static_cast<double>(stats.frames)) + " ms, p50 <= " +
                          std::to_string(percentile(0.5)) + " ms, p95 <= " + std::to_string(percentile(0.95)) + " ms, max " +
                          std::to_string(stats.maxSeconds * 1000.0) + " ms");
                const double frames = static_cast<double>(stats.frames);
                TEST_NOTE(std::string(tag) + " stage means ms: render " + std::to_string(stats.stageSeconds[0] * 1000.0 / frames) +
                          ", move " + std::to_string(stats.stageSeconds[1] * 1000.0 / frames) + ", copy " +
                          std::to_string(stats.stageSeconds[2] * 1000.0 / frames) + ", bookkeeping " +
                          std::to_string(stats.stageSeconds[3] * 1000.0 / frames) + "; visuals reused " +
                          std::to_string(stats.visualsReused) + ", rendered " + std::to_string(stats.visualsRendered));
                std::string compose = std::string(tag) + " compose means ms:";
                for (const double seconds : stats.composeSeconds) compose += " " + std::to_string(seconds * 1000.0 / frames);
                TEST_NOTE(compose + " (material, invalidation, shift, content, copy, blend, overlay)");
                TEST_NOTE(std::string(tag) + " material renders " + std::to_string(stats.backgroundRenders) + ", content rebuilds " +
                          std::to_string(stats.contentRebuilds) + ", reasons created/origin/dpi/invalid " +
                          std::to_string(stats.materialReasons[0]) + "/" + std::to_string(stats.materialReasons[1]) + "/" +
                          std::to_string(stats.materialReasons[2]) + "/" + std::to_string(stats.materialReasons[3]) + ", fallbacks " +
                          std::to_string(stats.retainedFallbacks) + ", last origin " + std::to_string(stats.lastOriginFrom.x) + "," +
                          std::to_string(stats.lastOriginFrom.y) + " -> " + std::to_string(stats.lastOriginTo.x) + "," + std::to_string(stats.lastOriginTo.y));
            }
        }
        if (applies.size() > 2) {
            // UI-side cadence: intervals between consecutive moves that belong to one motion.
            std::vector<double> gaps;
            for (std::size_t i = 1; i < applies.size(); ++i) {
                const double gap = applies[i] - applies[i - 1];
                if (gap < 0.1) gaps.push_back(gap);
            }
            std::sort(gaps.begin(), gaps.end());
            if (!gaps.empty()) {
                const auto at = [&](double q) { return gaps[std::min(gaps.size() - 1, static_cast<std::size_t>(q * static_cast<double>(gaps.size())))]; };
                TEST_NOTE(std::string(tag) + " moves " + std::to_string(applies.size()) + ", in-motion move interval ms p50 " +
                          std::to_string(at(0.5) * 1000.0) + " p95 " + std::to_string(at(0.95) * 1000.0) + " p99 " +
                          std::to_string(at(0.99) * 1000.0) + " max " + std::to_string(gaps.back() * 1000.0));
            }
        }
        f.scroll.scrolled = nullptr;
        for (const auto& failure : log.failures) TEST_NOTE(std::string(tag) + " incoherent: " + failure);
        for (std::size_t i = 0; i < log.samples.size(); ++i) {
            (void)SavePng(log.samples[i], InspectionPath(std::string("form-motion-") + tag + "-" + std::to_string(i)));
        }
        CHECK(after > before);
        if (capture) {
            CHECK(log.distinct >= 20);   // the capture really observed motion
            CHECK(log.rows >= 40);
            CHECK_EQ(log.badFrames, 0);  // SF-01: no frame may show detached, stale or misregistered pieces
        }
    }
    scheduler.Shutdown();
}

// Diagnostic: how many composed frames each presentation channel needs to show one immediate jump.
void RunChannelLatency() {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    platform::MessageLoop loop;
    graphics::FrameScheduler scheduler;
    REQUIRE(scheduler.Initialize(loop).ok());
    {
        auto tokens = graphics::MakeDesignTokens(ThemeMode::Dark);
        tokens.transparencyEnabled = SystemBackdropSupported() && platform::SystemTransparencyEnabled();
        MotionForm f;
        BuildMotionForm(f, tokens, scheduler);
        RECT bounds = f.host.WindowBounds();
        BackdropGuard backdrop;
        const int margin = f.host.Px(12.0f);
        backdrop.area = RECT{bounds.left - margin, bounds.top - margin, bounds.right + margin, bounds.bottom + margin};
        backdrop.hwnd = CreateTestBackdrop(backdrop.area);
        REQUIRE(backdrop.hwnd != nullptr);
        f.host.Show();
        const auto settle = [&](double seconds) {
            const double start = graphics::FrameScheduler::Now();
            RunNested(loop, [&] { return graphics::FrameScheduler::Now() - start > seconds; });
        };
        settle(0.6);
        RECT viewport{};
        ::GetWindowRect(f.scroll.Viewport(), &viewport);
        viewport.right -= f.scroll.GutterPx();
        const int fieldX0 = f.host.Px(kFieldLeft + 8.0f), fieldX1 = f.host.Px(kFieldRight - 8.0f);
        const int barX0 = f.host.Px(kBarLeft + 2.0f), barX1 = f.host.Px(kBarRight - 2.0f);
        struct Sample {
            double at;
            int bar;
            int text;
        };
        std::mutex mutex;
        std::vector<Sample> samples;
        std::atomic<bool> stop{false};
        std::thread capturer([&] {
            while (!stop.load()) {
                auto frame = CapturePrivate(backdrop.hwnd, backdrop.area, viewport);
                if (!frame.Valid()) { ::Sleep(1); continue; }
                const auto bars = Runs(frame, barX0, barX1, Magenta);
                int bar = -1;
                for (const auto& [top, bottom] : bars) if (top > 0 && bottom < frame.height) { bar = top; break; }
                int text = -1;
                for (int y = 0; y < frame.height && text < 0; ++y) {
                    int count = 0;
                    for (int x = fieldX0; x < fieldX1; ++x) {
                        const auto* p = frame.Pixel(x, y);
                        if (p[0] > 200 && p[1] > 200 && p[2] > 200) ++count;
                    }
                    if (count >= 3) text = y;
                }
                std::lock_guard lock(mutex);
                samples.push_back({graphics::FrameScheduler::Now(), bar, text});
            }
        });
        std::vector<double> jumps;
        for (int jump = 0; jump < 5; ++jump) {
            settle(0.25);
            jumps.push_back(graphics::FrameScheduler::Now());
            f.scroll.ScrollTo(f.scroll.Position() + f.host.Px(30.0f), false);
        }
        settle(0.3);
        // Continuous constant-velocity steps on the frame timer (like an animated scroll).
        const std::size_t continuousFrom = [&] { std::lock_guard lock(mutex); return samples.size(); }();
        const int step = f.host.Px(12.0f);
        int steps = 0;
        double nextStep = graphics::FrameScheduler::Now();
        RunNested(loop, [&] {
            const double now = graphics::FrameScheduler::Now();
            if (now >= nextStep && steps < 40) {
                f.scroll.ScrollTo(f.scroll.Position() + step, false);
                ++steps;
                nextStep += 1.0 / 60.0;
            }
            return steps >= 40 && now > nextStep + 0.2;
        });
        stop.store(true);
        capturer.join();
        {
            std::map<int, int> histogram;
            int rest = -1000;
            for (std::size_t i = 0; i < continuousFrom && i < samples.size(); ++i)
                if (samples[i].bar >= 0 && samples[i].text >= 0) rest = samples[i].text - samples[i].bar;
            for (std::size_t i = continuousFrom; i < samples.size(); ++i) {
                if (samples[i].bar < 0 || samples[i].text < 0 || rest == -1000) continue;
                const int offset = samples[i].text - samples[i].bar - rest;
                if (std::abs(offset) < 4 * step) ++histogram[static_cast<int>(std::lround(static_cast<double>(offset) / step))];
            }
            std::string text = "continuous: editor minus scene, in steps of " + std::to_string(step) + " px:";
            for (const auto& [lag, count] : histogram) text += " [" + std::to_string(lag) + "]=" + std::to_string(count);
            TEST_NOTE(text);
        }
        for (const double at : jumps) {
            std::optional<double> barMoved, textMoved;
            int barBefore = -2, textBefore = -2;
            for (const auto& s : samples) {
                if (s.at < at) { barBefore = s.bar; textBefore = s.text; continue; }
                if (!barMoved && s.bar != barBefore) barMoved = s.at - at;
                if (!textMoved && s.text != textBefore) textMoved = s.at - at;
            }
            TEST_NOTE("jump: scene visible after " + (barMoved ? std::to_string(*barMoved * 1000.0) : std::string("n/a")) +
                      " ms, layered editor after " + (textMoved ? std::to_string(*textMoved * 1000.0) : std::string("n/a")) + " ms");
        }
    }
    scheduler.Shutdown();
}

}  // namespace

AETHER_TEST("ui_form motion diagnostic: presentation channel latency") { RunChannelLatency(); }

AETHER_TEST("ui_form motion: dark scrolling frames stay registered while moving") {
    RunScrollCoherence(ThemeMode::Dark, "dark");
}

AETHER_TEST("ui_form motion timing: scroll cost and cadence without a recording thread") {
    RunScrollCoherence(ThemeMode::Dark, "timing", false);
}

AETHER_TEST("ui_form motion: light scrolling frames stay registered while moving") {
    RunScrollCoherence(ThemeMode::Light, "light");
}
