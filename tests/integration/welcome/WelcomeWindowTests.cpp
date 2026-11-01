// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FR-041's window with this test's own small window only: it never covers the desktop and the session
// events of the real machine play no part. Frames are rendered offscreen and saved under
// %TEMP%\AetherShellTests\ for inspection of the moving pen. Real sign-in, unlock and physical touch
// stay separate owner checks.
#include "AetherTest.h"
#include "FakeTouch.h"
#include "../../unit/graphics/D2DCanvas.h"

#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/SystemEvents.h"
#include "welcome/WelcomeWindow.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::welcome;

namespace {

struct Stage {
    platform::ComApartment com{COINIT_APARTMENTTHREADED};
    platform::MessageLoop loop;
    graphics::GraphicsDevice device;
    graphics::FrameScheduler scheduler;
    std::unique_ptr<WelcomeWindow> window;
    RECT bounds{};
    Stage() {
        REQUIRE(device.Initialize().ok());
        REQUIRE(scheduler.Initialize(loop).ok());
        window = std::make_unique<WelcomeWindow>(device, scheduler);
        const auto monitor = platform::PrimaryMonitor();
        bounds = RECT{monitor.workArea.left + 60, monitor.workArea.top + 120, monitor.workArea.left + 760, monitor.workArea.top + 480};
    }
    ~Stage() { window.reset(); scheduler.Shutdown(); device.Shutdown(); }
    bool Pump(const std::function<bool()>& done, DWORD limitMs) {
        HANDLE timer = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
        REQUIRE(timer != nullptr);
        LARGE_INTEGER due{}; due.QuadPart = -10'000;
        REQUIRE(::SetWaitableTimer(timer, &due, 4, nullptr, nullptr, FALSE) != FALSE);
        const auto id = loop.AddWaitHandle(timer, [] {});
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
        loop.RunNested([&] { return done() || std::chrono::steady_clock::now() >= end; });
        loop.RemoveWaitHandle(id);
        ::CancelWaitableTimer(timer); ::CloseHandle(timer);
        return done();
    }
};

graphics::DesignTokens Dark() { return graphics::MakeDesignTokens(graphics::ThemeMode::Dark); }

int InkPixels(test::D2DCanvas& canvas, UINT width, UINT height) {
    int count = 0;
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) if (canvas.At(x, y).a > 40) ++count;
    return count;
}

// A plain 24-bit bitmap, written only under the test temp folder.
void SaveFrame(test::D2DCanvas& canvas, UINT width, UINT height, const std::filesystem::path& path) {
    const std::uint32_t row = (width * 3 + 3) & ~3u, size = row * height;
    std::vector<std::uint8_t> bytes(54 + size, 0);
    bytes[0] = 'B'; bytes[1] = 'M';
    const auto put = [&](std::size_t at, std::uint32_t value) { for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i)); };
    put(2, 54 + size); put(10, 54); put(14, 40); put(18, width); put(22, height); bytes[26] = 1; bytes[28] = 24; put(34, size);
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        const auto p = canvas.At(x, height - 1 - y);
        const auto offset = 54 + y * row + x * 3;
        bytes[offset] = static_cast<std::uint8_t>(p.b + 26 * (255 - p.a) / 255);
        bytes[offset + 1] = static_cast<std::uint8_t>(p.g + 20 * (255 - p.a) / 255);
        bytes[offset + 2] = static_cast<std::uint8_t>(p.r + 18 * (255 - p.a) / 255);
    }
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

AETHER_TEST("welcome pixels: the ink grows along the moving pen, frame after frame, and is complete at the end") {
    constexpr UINT kWidth = 1280, kHeight = 320;
    auto scene = ComposeScene(L"Salut", L"Éloïse", 1200.0f, 300.0f);
    REQUIRE(scene.ok());
    test::D2DCanvas canvas;
    REQUIRE(canvas.Create(kWidth, kHeight));
    winrt::com_ptr<ID2D1SolidColorBrush> ink, pen;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.96f, 0.98f), ink.put())));
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(0.4f, 0.7f, 1.0f), pen.put())));
    const auto folder = std::filesystem::temp_directory_path() / L"AetherShellTests";
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    const auto origin = D2D1::Point2F((static_cast<float>(kWidth) - scene->width) * 0.5f, 10.0f);
    int previous = -1;
    for (int step = 0; step <= 6; ++step) {
        const float time = scene->inkDuration * static_cast<float>(step) / 6.0f;
        canvas.Begin();
        PaintHandwriting(canvas.Context(), *scene, origin, time, ink.get(), pen.get());
        REQUIRE(canvas.End());
        const int pixels = InkPixels(canvas, kWidth, kHeight);
        TEST_NOTE("t=" + std::to_string(time) + " ink pixels=" + std::to_string(pixels));
        if (step > 0 && step < 6) CHECK(pixels >= previous - 40);  // the tip may leave a spot; ink never recedes
        previous = pixels;
        SaveFrame(canvas, kWidth, kHeight, folder / (L"welcome-frame-" + std::to_wstring(step) + L".bmp"));
    }
    canvas.Begin();
    PaintHandwriting(canvas.Context(), *scene, origin, 0.0f, ink.get(), nullptr);
    REQUIRE(canvas.End());
    const int atStart = InkPixels(canvas, kWidth, kHeight);
    canvas.Begin();
    PaintHandwriting(canvas.Context(), *scene, origin, scene->duration + 1.0f, ink.get(), nullptr);
    REQUIRE(canvas.End());
    const int complete = InkPixels(canvas, kWidth, kHeight);
    CHECK_EQ(atStart, 0);
    CHECK(complete > 2000);
}

AETHER_TEST("welcome window: it writes with continuous frames, then requests none; the pen finishes first") {
    Stage stage;
    REQUIRE(stage.window->Present(L"Hello", L"Zoé", Dark(), graphics::MotionPolicy{}, stage.bounds).ok());
    CHECK(stage.window->Presenting());
    CHECK(stage.window->AnimatesInk());
    CHECK((::GetWindowLongPtrW(stage.window->Handle(), GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);  // takes no focus
    wchar_t title[64]{};
    ::GetWindowTextW(stage.window->Handle(), title, 64);
    CHECK(std::wstring(title).find(L"Zo") == std::wstring::npos);  // the name is not exposed in the window text
    CHECK_FALSE(stage.window->WritingFinished(graphics::FrameScheduler::Now()));
    const auto duration = stage.window->Scene().duration;
    const auto started = graphics::FrameScheduler::Now();
    (void)stage.Pump([&] { return stage.window->WritingFinished(graphics::FrameScheduler::Now()); }, 10000);
    const auto frames = stage.window->RenderedFrames();
    CHECK(graphics::FrameScheduler::Now() - started >= static_cast<double>(duration) + 0.4);  // and time to read it
    CHECK(frames > static_cast<std::uint64_t>(duration * 20.0f));   // a moving line, not a few steps
    TEST_NOTE("rendered " + std::to_string(frames) + " frames in " + std::to_string(duration) + " s of writing");
    CHECK(stage.window->HasPresented());
    (void)stage.Pump([] { return false; }, 150);
    const auto settled = stage.window->RenderedFrames();
    (void)stage.Pump([] { return false; }, 300);
    CHECK_EQ(stage.window->RenderedFrames(), settled);             // idle once written
    stage.window->Dismiss(true);
    CHECK_FALSE(stage.window->Presenting());
    CHECK(stage.Pump([&] { return !stage.window->IsShown(); }, 2000));
}

AETHER_TEST("welcome window: reduced motion shows the finished words at once, without pen or animation frames") {
    Stage stage;
    graphics::MotionPolicy reduced; reduced.reduced = true;
    REQUIRE(stage.window->Present(L"Salut", L"Anaïs", Dark(), reduced, stage.bounds).ok());
    CHECK_FALSE(stage.window->AnimatesInk());
    CHECK_FALSE(stage.window->WritingFinished(graphics::FrameScheduler::Now()));  // time to read it
    (void)stage.Pump([] { return false; }, 200);
    CHECK(stage.window->HasPresented());
    CHECK(stage.window->RenderedFrames() <= 3u);
    const auto high = graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast);
    REQUIRE(stage.window->Present(L"Salut", L"Anaïs", high, graphics::MotionPolicy{}, stage.bounds).ok());
    CHECK_FALSE(stage.window->AnimatesInk());  // high contrast is still as well
    stage.window->Dismiss(false);
    CHECK_FALSE(stage.window->IsShown());      // a re-lock leaves nothing behind, immediately
}

namespace {

// The test's own picture, never a real desktop's: its left half is `left`, its right half `right` (BGRA).
BackdropImage SplitPicture(UINT width, UINT height, std::uint8_t left, std::uint8_t right) {
    BackdropImage image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<std::size_t>(width) * height * 4u, 255);
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        auto* p = image.pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4u;
        p[0] = p[1] = p[2] = x < width / 2 ? left : right;
    }
    image.brightness = MeanBrightness(image.pixels);
    return image;
}

winrt::com_ptr<ID2D1Device> DeviceOf(test::D2DCanvas& canvas) {
    winrt::com_ptr<ID2D1Device> device;
    canvas.Context()->GetDevice(device.put());
    return device;
}

}  // namespace

AETHER_TEST("welcome pixels: the background is the picture, blurred and opaque edge to edge, never black") {
    constexpr UINT kWidth = 640, kHeight = 360;
    test::D2DCanvas canvas;
    REQUIRE(canvas.Create(kWidth, kHeight));
    const auto picture = SplitPicture(320, 180, 240, 90);
    const auto blurred = BlurBackdrop(DeviceOf(canvas).get(), picture);
    REQUIRE(blurred != nullptr);
    canvas.Begin();
    PaintBackdrop(canvas.Context(), blurred.get(), D2D1::ColorF(1.0f, 0.0f, 0.0f), picture.brightness, static_cast<float>(kWidth), static_cast<float>(kHeight));
    REQUIRE(canvas.End());
    // Opaque everywhere, with nothing of the fallback colour and nothing nearly black.
    for (UINT y = 0; y < kHeight; y += 6) for (UINT x = 0; x < kWidth; x += 6) {
        const auto p = canvas.At(x, y);
        CHECK_EQ(p.a, 255);
        CHECK(p.g > 40);
        CHECK(std::abs(p.r - p.g) <= 2);
    }
    // The hard edge in the picture has become a soft ramp: several steps across the middle, none large.
    const int bright = canvas.At(8, kHeight / 2).g, dark = canvas.At(kWidth - 9, kHeight / 2).g;
    CHECK(bright > dark + 60);
    int steps = 0, largest = 0;
    for (UINT x = kWidth / 2 - 40; x < kWidth / 2 + 40; ++x) {
        const int change = std::abs(canvas.At(x + 1, kHeight / 2).g - canvas.At(x, kHeight / 2).g);
        if (change > 0) ++steps;
        largest = std::max(largest, change);
    }
    CHECK(steps >= 30);
    CHECK(largest <= (bright - dark) / 8);
    // No rim where the blur meets the screen's edge: the corners match what lies just inside them.
    CHECK(std::abs(canvas.At(0, 0).g - canvas.At(20, 20).g) <= 3);
    CHECK(std::abs(canvas.At(kWidth - 1, kHeight - 1).g - canvas.At(kWidth - 21, kHeight - 21).g) <= 3);
    // Without a picture: the plain colour, under the same veil.
    canvas.Begin();
    PaintBackdrop(canvas.Context(), nullptr, D2D1::ColorF(0.0f, 0.5f, 0.0f), 0.3f, static_cast<float>(kWidth), static_cast<float>(kHeight));
    REQUIRE(canvas.End());
    CHECK_EQ(canvas.At(100, 100).r, 0);
    CHECK(canvas.At(100, 100).g > 90);
    CHECK_EQ(canvas.At(100, 100).a, 255);
    CHECK(BlurBackdrop(nullptr, picture) == nullptr);
    CHECK(BlurBackdrop(DeviceOf(canvas).get(), BackdropImage{}) == nullptr);
}

AETHER_TEST("welcome pixels: ink added piece by piece is the same as ink written at once") {
    constexpr UINT kWidth = 1280, kHeight = 400;
    auto scene = ComposeScene(L"Hello", L"Zoé", 1200.0f, 380.0f);
    REQUIRE(scene.ok());
    test::D2DCanvas canvas;
    REQUIRE(canvas.Create(kWidth, kHeight));
    winrt::com_ptr<ID2D1SolidColorBrush> ink;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), ink.put())));
    const auto origin = D2D1::Point2F(20.0f, 10.0f);
    canvas.Begin();
    PaintHandwriting(canvas.Context(), *scene, origin, scene->inkDuration + 1.0f, ink.get(), nullptr);
    REQUIRE(canvas.End());
    const int whole = InkPixels(canvas, kWidth, kHeight);
    canvas.Begin();
    float until = -1.0f;
    for (int frame = 0; frame <= 400; ++frame) {
        const float time = scene->inkDuration * static_cast<float>(frame) / 390.0f;
        PaintHandwritingRange(canvas.Context(), *scene, origin, until, time, ink.get());
        until = time;
    }
    REQUIRE(canvas.End());
    const int pieces = InkPixels(canvas, kWidth, kHeight);
    TEST_NOTE("whole " + std::to_string(whole) + " pixels, in pieces " + std::to_string(pieces));
    CHECK(whole > 2000);
    CHECK(std::abs(whole - pieces) <= whole / 50);
}

// A frame for the eye: the whole greeting as a person would see it, over the test's own picture.
AETHER_TEST("welcome pixels: the whole greeting, saved for inspection") {
    constexpr UINT kWidth = 1280, kHeight = 720;
    test::D2DCanvas canvas;
    REQUIRE(canvas.Create(kWidth, kHeight));
    BackdropImage picture;
    picture.width = 640;
    picture.height = 360;
    picture.pixels.resize(640u * 360u * 4u);
    for (UINT y = 0; y < 360; ++y) for (UINT x = 0; x < 640; ++x) {
        auto* p = picture.pixels.data() + (static_cast<std::size_t>(y) * 640u + x) * 4u;
        const bool ground = static_cast<float>(y) > 210.0f + 30.0f * std::sin(static_cast<float>(x) * 0.02f);
        p[0] = ground ? 60 : static_cast<std::uint8_t>(230 - y / 4);
        p[1] = ground ? 110 : static_cast<std::uint8_t>(170 + y / 8);
        p[2] = ground ? 70 : static_cast<std::uint8_t>(90 + y / 3);
        p[3] = 255;
    }
    picture.brightness = MeanBrightness(picture.pixels);
    const auto blurred = BlurBackdrop(DeviceOf(canvas).get(), picture);
    REQUIRE(blurred != nullptr);
    auto scene = ComposeScene(L"Salut", L"Ibrahim", 1280.0f * 0.86f, 720.0f * 0.8f);
    REQUIRE(scene.ok());
    TEST_NOTE("scene " + std::to_string(scene->width) + " x " + std::to_string(scene->height) + ", writing " + std::to_string(scene->inkDuration) + " s, pen " + std::to_string(scene->penWidth));
    winrt::com_ptr<ID2D1SolidColorBrush> ink;
    REQUIRE(SUCCEEDED(canvas.Context()->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.98f, 0.99f), ink.put())));
    const auto folder = std::filesystem::temp_directory_path() / L"AetherShellTests";
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    canvas.Begin();
    PaintBackdrop(canvas.Context(), blurred.get(), D2D1::ColorF(0.0f, 0.0f, 0.0f), picture.brightness, 1280.0f, 720.0f);
    PaintHandwriting(canvas.Context(), *scene, D2D1::Point2F((1280.0f - scene->width) * 0.5f, (720.0f - scene->height) * 0.5f), scene->inkDuration * 0.62f, ink.get(), ink.get());
    REQUIRE(canvas.End());
    SaveFrame(canvas, kWidth, kHeight, folder / L"welcome-whole-greeting.bmp");
    CHECK_EQ(canvas.At(4, 4).a, 255);
}

AETHER_TEST("welcome window: the cover shows the background alone, at once; the unlock starts writing on it") {
    Stage stage;
    CHECK_EQ(platform::CurrentSessionLockState(), platform::SessionLockState::Unlocked);  // this test could not run otherwise
    stage.window->SetBackdrop(std::make_shared<const BackdropImage>(SplitPicture(160, 90, 200, 80)));
    REQUIRE(stage.window->ShowCover(Dark(), graphics::MotionPolicy{}, stage.bounds).ok());
    CHECK(stage.window->Covering());
    CHECK_FALSE(stage.window->Presenting());
    CHECK(stage.window->IsShown());
    CHECK(stage.window->HasPresented());          // its first frame was drawn before it became visible
    CHECK(stage.window->ShowsPicture());
    CHECK_FALSE(stage.window->IsTransitioning()); // no entrance: there is nobody to see one
    CHECK((::GetWindowLongPtrW(stage.window->Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    CHECK((::GetWindowLongPtrW(stage.window->Handle(), GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);
    (void)stage.Pump([] { return false; }, 150);
    const auto waiting = stage.window->RenderedFrames();
    (void)stage.Pump([] { return false; }, 300);
    CHECK_EQ(stage.window->RenderedFrames(), waiting);  // it waits without drawing, however long the lock lasts
    REQUIRE(stage.window->Present(L"Hello", L"Zoé", Dark(), graphics::MotionPolicy{}, stage.bounds).ok());
    CHECK(stage.window->Presenting());
    CHECK_FALSE(stage.window->Covering());
    CHECK_FALSE(stage.window->IsTransitioning());       // already there: nothing fades in
    CHECK(stage.Pump([&] { return stage.window->RenderedFrames() > waiting + 10; }, 2000));
    // Locked again while it writes: back to the bare background, with no words left on it.
    REQUIRE(stage.window->ShowCover(Dark(), graphics::MotionPolicy{}, stage.bounds).ok());
    CHECK(stage.window->Scene().strokes.empty());
    CHECK_FALSE(stage.window->Presenting());
    stage.window->Dismiss(false);
    CHECK_FALSE(stage.window->IsShown());
    // Without a cover (sign-in), it enters as before, and a picture may still arrive while it writes.
    stage.window->SetBackdrop(nullptr);
    CHECK_FALSE(stage.window->ShowsPicture());
    REQUIRE(stage.window->Present(L"Salut", L"", Dark(), graphics::MotionPolicy{}, stage.bounds).ok());
    CHECK(stage.window->IsTransitioning());
    stage.window->SetBackdrop(std::make_shared<const BackdropImage>(SplitPicture(160, 90, 200, 80)));
    CHECK(stage.window->ShowsPicture());
    stage.window->Dismiss(false);
}

AETHER_TEST("welcome window: a click on the cover removes it, once, and high contrast never shows the picture") {
    Stage stage;
    int skips = 0;
    auto connection = stage.window->SkipRequested.Connect([&] { ++skips; });
    REQUIRE(stage.window->ShowCover(Dark(), graphics::MotionPolicy{}, stage.bounds).ok());
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
    CHECK_EQ(skips, 1);
    stage.window->Dismiss(false);
    connection.Disconnect();
    const auto high = graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast);
    REQUIRE(stage.window->Present(L"Salut", L"", high, graphics::MotionPolicy{}, stage.bounds).ok());
    (void)stage.Pump([] { return false; }, 100);
    CHECK(stage.window->HasPresented());
    stage.window->Dismiss(false);
}

AETHER_TEST("welcome window: touch, click and Escape each skip exactly once; nothing else does") {
    Stage stage;
    int skips = 0;
    auto connection = stage.window->SkipRequested.Connect([&] { ++skips; });
    const auto reopen = [&] { skips = 0; REQUIRE(stage.window->Present(L"Hello", L"", Dark(), graphics::MotionPolicy{}, stage.bounds).ok()); };
    const POINT inside{(stage.bounds.left + stage.bounds.right) / 2, (stage.bounds.top + stage.bounds.bottom) / 2};
    reopen();
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
    CHECK_EQ(skips, 1);
    reopen();
    {
        test::FakeTouch touch;
        touch.Send(stage.window->Handle(), WM_POINTERDOWN, 1, inside);
        CHECK_EQ(skips, 1);
        touch.Send(stage.window->Handle(), WM_POINTERUP, 1, inside);
        CHECK_EQ(skips, 1);
    }
    reopen();
    ::SendMessageW(stage.window->Handle(), WM_KEYDOWN, 'A', 0);
    ::SendMessageW(stage.window->Handle(), WM_MOUSEMOVE, 0, MAKELPARAM(40, 40));
    CHECK_EQ(skips, 0);
    ::SendMessageW(stage.window->Handle(), WM_KEYDOWN, VK_ESCAPE, 0);
    CHECK_EQ(skips, 1);
    stage.window->Dismiss(false);
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
    ::SendMessageW(stage.window->Handle(), WM_LBUTTONUP, 0, MAKELPARAM(40, 40));
    CHECK_EQ(skips, 1);  // dismissed: no further requests
    connection.Disconnect();
}
