// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// FR-041: the greeting's ink and lifecycle, without a window. Handwriting quality and real sign-in or
// unlock timing are the owner's checks; these prove the structure that makes them possible.
#include "AetherTest.h"
#include "platform/windows/ComApartment.h"
#include "welcome/Handwriting.h"
#include "welcome/WelcomeBackdrop.h"
#include "welcome/WelcomeController.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

using namespace aether;
using namespace aether::welcome;

namespace {
WelcomeScene Scene(std::wstring_view greeting, std::wstring_view name, float width = 1600.0f, float height = 720.0f) {
    auto scene = ComposeScene(greeting, name, width, height);
    REQUIRE(scene.ok());
    return *scene;
}
}  // namespace

AETHER_TEST("welcome ink: French and English names with accents are written with real strokes") {
    for (const auto* text : {L"Salut", L"Hello", L"Ibrahim", L"Noëlle", L"Éloïse-François", L"Zoé Müller", L"Anaïs", L"Jérôme", L"Œuvre", L"D'Arcy", L"Åsa"})
        CHECK(CanWrite(text));
    for (const auto* text : {L"日本語", L"محمد", L"Иван", L"A\U0001F600"}) CHECK_FALSE(CanWrite(text));
}

AETHER_TEST("welcome ink: the pen moves continuously, with pen lifts, varying speed and width") {
    const auto scene = Scene(L"Salut", L"Ibrahim");
    CHECK(scene.strokes.size() >= 4u);     // words, a capital's own start, then dots and crossbars
    CHECK_EQ(scene.penLifts, static_cast<int>(scene.strokes.size()) - 1);
    CHECK(scene.inkLength > 600.0f);
    float previous = -1.0f, slowest = 0.0f, fastest = std::numeric_limits<float>::max();
    float thinnest = std::numeric_limits<float>::max(), thickest = 0.0f;
    for (const auto& stroke : scene.strokes) {
        REQUIRE(stroke.points.size() >= 2u);
        CHECK(stroke.points.front().time >= previous);  // strictly one pen: nothing is drawn at once
        for (std::size_t i = 1; i < stroke.points.size(); ++i) {
            const auto& a = stroke.points[i - 1];
            const auto& b = stroke.points[i];
            CHECK(b.time >= a.time);
            const float dt = b.time - a.time, ds = std::hypot(b.x - a.x, b.y - a.y);
            if (dt > 0.0f && ds > 0.5f) { slowest = std::max(slowest, dt / ds); fastest = std::min(fastest, dt / ds); }
            CHECK(ds <= scene.penWidth);  // finely sampled: smooth curves, not visible segments
            thinnest = std::min(thinnest, b.width); thickest = std::max(thickest, b.width);
        }
        previous = stroke.points.back().time;
    }
    CHECK(slowest > fastest * 2.0f);        // not a constant-speed plotter
    CHECK(thickest > thinnest * 1.15f);     // and not a constant-width line
    CHECK(scene.inkDuration >= 3.6f);
    CHECK(scene.inkDuration <= 6.2f);
    // Word first, then its dots and crossbars: the i's dot is written after everything else.
    const auto& last = scene.strokes.back().points;
    CHECK(last.front().time > scene.inkDuration * 0.5f);
}

AETHER_TEST("welcome ink: writing is deterministic and stays inside the bounds, for any name length") {
    const auto once = Scene(L"Hello", L"Zoé"), again = Scene(L"Hello", L"Zoé");
    REQUIRE_EQ(once.strokes.size(), again.strokes.size());
    CHECK_EQ(once.inkLength, again.inkLength);
    CHECK_EQ(once.strokes.front().points.size(), again.strokes.front().points.size());
    const std::wstring longName(64, L'm');
    for (const auto& [greeting, name] : {std::pair<std::wstring, std::wstring>{L"Hello", L""}, {L"Salut", L"Ibrahim"}, {L"Hello", longName}, {L"Salut", L"Marie-Angélique de la Fontaine-Beaumarchais"}}) {
        const auto scene = Scene(greeting, name, 1000.0f, 500.0f);
        CHECK(scene.width <= 1000.5f);
        CHECK(scene.height <= 500.5f);
        CHECK(scene.inkDuration <= 6.2f);
        for (const auto& stroke : scene.strokes) for (const auto& point : stroke.points) {
            CHECK(point.x >= -30.0f); CHECK(point.x <= 1030.0f);
            CHECK(std::isfinite(point.y)); CHECK(point.width > 0.0f);
        }
    }
    // A long name writes on two lines rather than shrinking out of reach.
    const auto wrapped = Scene(L"Salut", L"Marie-Angélique de la Fontaine", 500.0f, 500.0f);
    CHECK(wrapped.height > wrapped.width * 0.2f);  // a single line that long would be a thin strip
}

AETHER_TEST("welcome ink: an unsupported script stays readable; the greeting is still written") {
    const auto scene = Scene(L"Hello", L"日本語");
    CHECK_EQ(scene.fallbackName, std::wstring(L"日本語"));
    CHECK(scene.strokes.size() >= 1u);
    CHECK(scene.fallbackHeight > 0.0f);
    CHECK(scene.duration > scene.inkDuration);
    const auto spaces = Scene(L"Hello", L"   ");  // nothing to greet by: no name, no fallback
    CHECK(spaces.fallbackName.empty());
    CHECK_FALSE(ComposeScene(L"Hello", L"", std::numeric_limits<float>::quiet_NaN(), 500.0f).ok());
    CHECK_FALSE(ComposeScene(L"Hello", L"", 0.0f, 500.0f).ok());
}

AETHER_TEST("welcome ink: the pen tip follows the line, lifts between strokes and is gone at the end") {
    const auto scene = Scene(L"Salut", L"");
    CHECK_FALSE(PenAt(scene, -0.1f).has_value());
    CHECK_FALSE(PenAt(scene, scene.inkDuration + 0.1f).has_value());
    const auto start = PenAt(scene, 0.0f);
    REQUIRE(start.has_value());
    CHECK_FALSE(start->lifted);
    int lifted = 0, down = 0;
    float previousX = start->x, previousY = start->y, longest = 0.0f;
    for (float t = 0.0f; t < scene.inkDuration; t += 1.0f / 120.0f) {
        const auto pen = PenAt(scene, t);
        REQUIRE(pen.has_value());
        pen->lifted ? ++lifted : ++down;
        longest = std::max(longest, std::hypot(pen->x - previousX, pen->y - previousY));
        previousX = pen->x; previousY = pen->y;
    }
    CHECK(down > 100);
    CHECK(lifted > 0);           // "Salut": the t's crossbar is a real lift
    CHECK(longest < 60.0f);      // it travels, it does not jump
}

AETHER_TEST("welcome greeting: the explicit language wins, otherwise the Windows display language") {
    using settings::GreetingLanguage;
    const LANGID french = MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH_CANADIAN), english = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), swedish = MAKELANGID(LANG_SWEDISH, SUBLANG_DEFAULT);
    CHECK_EQ(GreetingWord(GreetingLanguage::Automatic, french), std::wstring(L"Salut"));
    CHECK_EQ(GreetingWord(GreetingLanguage::Automatic, english), std::wstring(L"Hello"));
    CHECK_EQ(GreetingWord(GreetingLanguage::Automatic, swedish), std::wstring(L"Hello"));
    CHECK_EQ(GreetingWord(GreetingLanguage::French, english), std::wstring(L"Salut"));
    CHECK_EQ(GreetingWord(GreetingLanguage::English, french), std::wstring(L"Hello"));
}

AETHER_TEST("welcome lifecycle: one greeting per sign-in, and never over a locked or disallowed session") {
    using Action = WelcomeController::Action;
    WelcomeController controller;
    CHECK(controller.SignedIn(true, 1000) == Action::Present);
    CHECK(controller.Presenting());
    CHECK(controller.SignedIn(true, 1100) == Action::None);  // a duplicate
    CHECK(controller.Skipped() == Action::DismissAnimated);
    CHECK(controller.SignedIn(true, 60000) == Action::None);  // already greeted for this sign-in
    WelcomeController off;
    CHECK(off.SignedIn(false, 1000) == Action::None);
    CHECK(off.SignedIn(true, 2000) == Action::None);
    CHECK_FALSE(off.Presenting());
    // Started behind the lock screen: the unlock is the greeting.
    WelcomeController behind;
    CHECK(behind.Locked(false, 500) == Action::None);
    CHECK(behind.SignedIn(true, 1000) == Action::None);
    CHECK(behind.Unlocked(true, 2000) == Action::Present);
}

AETHER_TEST("welcome lifecycle: duplicate unlocks coalesce and a re-lock during a stroke cancels at once") {
    using Action = WelcomeController::Action;
    WelcomeController controller;
    CHECK(controller.Locked(false, 1000) == Action::None);
    CHECK(controller.Unlocked(true, 2000) == Action::Present);
    CHECK(controller.Unlocked(true, 2010) == Action::None);   // a repeat while it shows
    CHECK(controller.Skipped() == Action::DismissAnimated);
    CHECK(controller.Unlocked(true, 2500) == Action::None);   // a repeat just after a quick skip
    // Locked again while writing: gone immediately, and the next unlock greets even if very soon.
    CHECK(controller.Unlocked(true, 9000) == Action::Present);
    CHECK(controller.Locked(false, 9100) == Action::DismissNow);
    CHECK_FALSE(controller.Presenting());
    CHECK(controller.Unlocked(true, 9200) == Action::Present);
    CHECK(controller.Canceled() == Action::DismissNow);       // display change, quitting
    CHECK(controller.Canceled() == Action::None);
    CHECK(controller.Skipped() == Action::None);
    // A setting turned off, or a full-screen application, means no greeting on that unlock.
    CHECK(controller.Unlocked(false, 60000) == Action::None);
}

// D54, the owner's report of seeing the desktop first: the background must already be up at the unlock.
AETHER_TEST("welcome lifecycle: the cover goes up at the lock, and the unlock turns it into the greeting") {
    using Action = WelcomeController::Action;
    WelcomeController controller;
    CHECK(controller.Locked(true, 1000) == Action::Cover);
    CHECK(controller.Covering());
    CHECK_FALSE(controller.Presenting());
    CHECK(controller.SessionLocked());
    CHECK(controller.Locked(true, 1100) == Action::None);       // a repeated notification
    CHECK(controller.Update(true, true, 500000) == Action::None);  // however long the lock lasts
    CHECK(controller.Covering());
    CHECK(controller.Unlocked(true, 600000) == Action::Present);
    CHECK_FALSE(controller.Covering());
    CHECK(controller.Presenting());
    CHECK_FALSE(controller.SessionLocked());
    // Locked again while it writes: back to the cover, and that next unlock is a new greeting.
    CHECK(controller.Locked(true, 600100) == Action::Cover);
    CHECK_FALSE(controller.Presenting());
    CHECK(controller.Unlocked(true, 600200) == Action::Present);
    CHECK(controller.Skipped() == Action::DismissAnimated);
    // A lock between two quick unlocks makes them two events, even after a skip.
    CHECK(controller.Locked(true, 600300) == Action::Cover);
    CHECK(controller.Unlocked(true, 600400) == Action::Present);
}

AETHER_TEST("welcome lifecycle: nothing can leave the cover in the way") {
    using Action = WelcomeController::Action;
    // An application went full screen behind the lock, or the setting changed: it leaves at the unlock.
    WelcomeController refused;
    REQUIRE(refused.Locked(true, 1000) == Action::Cover);
    CHECK(refused.Unlocked(false, 2000) == Action::DismissNow);
    CHECK_FALSE(refused.Covering());
    CHECK_FALSE(refused.Presenting());
    // Display change or quitting: gone at once, with nothing to greet.
    WelcomeController canceled;
    REQUIRE(canceled.Locked(true, 1000) == Action::Cover);
    CHECK(canceled.Canceled() == Action::DismissNow);
    CHECK_FALSE(canceled.Covering());
    // A person who can reach it can always remove it.
    WelcomeController skipped;
    REQUIRE(skipped.Locked(true, 1000) == Action::Cover);
    CHECK(skipped.Skipped() == Action::DismissNow);
    CHECK_FALSE(skipped.Covering());
    // A disconnect or logoff never asks for one, and takes down one that is writing.
    WelcomeController away;
    REQUIRE(away.Unlocked(true, 1000) == Action::Present);
    CHECK(away.Locked(false, 2000) == Action::DismissNow);
    CHECK_FALSE(away.Covering());
    // A failure to draw it is no obstacle either.
    WelcomeController failed;
    REQUIRE(failed.Locked(true, 1000) == Action::Cover);
    failed.PresentationFailed();
    CHECK_FALSE(failed.Covering());
    CHECK(WelcomeTiming{}.deadlineMs >= 10000u);  // room for the longest writing and the time to read it
    CHECK(WelcomeTiming{}.deadlineMs <= 15000u);
}

// D54: "bien plus en grand". A 1280 x 720 DIP screen offers about 1100 x 576 of it.
AETHER_TEST("welcome ink: it writes large: a word or two fills the room it is given") {
    const auto word = Scene(L"Salut", L"", 1100.0f, 576.0f);
    CHECK(word.height >= 576.0f * 0.99f || word.width >= 1100.0f * 0.99f);  // touches the limit that binds first
    CHECK(word.height <= 576.5f); CHECK(word.width <= 1100.5f);
    const auto named = Scene(L"Salut", L"Ibrahim", 1100.0f, 576.0f);
    CHECK(named.height >= 576.0f * 0.99f || named.width >= 1100.0f * 0.99f);
    CHECK(named.penWidth >= 8.0f);
    // Tall room stacks the words when that makes them larger; wide room keeps them on one line.
    CHECK(Scene(L"Salut", L"Ibrahim", 700.0f, 700.0f).height > Scene(L"Salut", L"Ibrahim", 2000.0f, 300.0f).height);
    // The same hand at any size: writing twice as large takes about the same time, not twice as long.
    const auto tiny = Scene(L"Salut", L"", 500.0f, 240.0f);
    CHECK(word.inkDuration >= 3.6f);
    CHECK(std::abs(word.inkDuration - tiny.inkDuration) <= 2.6f);
    CHECK(word.penWidth > tiny.penWidth * 1.8f);
}

// D56: "comme si c'etait un gamin". The look comes from a calligrapher's pen: strong contrast between down
// strokes and hairlines, a forward lean, and letters joined without knots.
AETHER_TEST("welcome ink: down strokes are heavy, up strokes are hairlines, and it leans forward") {
    const auto scene = Scene(L"Hello", L"", 1100.0f, 576.0f);
    float downWidth = 0.0f, upWidth = 0.0f, downCount = 0.0f, upCount = 0.0f, finest = 1000.0f, heaviest = 0.0f;
    for (const auto& stroke : scene.strokes) for (std::size_t i = 1; i < stroke.points.size(); ++i) {
        const auto& a = stroke.points[i - 1];
        const auto& b = stroke.points[i];
        const float dy = b.y - a.y, d = std::hypot(b.x - a.x, dy);
        if (d <= 0.0f) continue;
        finest = std::min(finest, b.width); heaviest = std::max(heaviest, b.width);
        if (dy / d > 0.8f) { downWidth += b.width; ++downCount; }
        if (dy / d < -0.5f) { upWidth += b.width; ++upCount; }
    }
    REQUIRE(downCount > 0.0f); REQUIRE(upCount > 0.0f);
    CHECK(downWidth / downCount > (upWidth / upCount) * 2.0f);
    CHECK(heaviest > finest * 3.0f);
    CHECK_EQ(heaviest <= scene.penWidth + 0.01f, true);
    // The lean: the top of the H's first stem is to the right of its foot.
    const auto& stem = scene.strokes.front().points;
    CHECK(stem.back().x < stem.front().x);
    // No knots at the joins: the line never doubles back on itself within a pen's width in one sharp turn.
    const auto joined = Scene(L"Ibrahim", L"", 1100.0f, 576.0f);
    for (const auto& stroke : joined.strokes) for (std::size_t i = 1; i < stroke.points.size(); ++i)
        CHECK(std::hypot(stroke.points[i].x - stroke.points[i - 1].x, stroke.points[i].y - stroke.points[i - 1].y) <= joined.penWidth);
    // A dot is a dab of the pen, not a speck.
    const auto dotted = Scene(L"i", L"", 400.0f, 400.0f);
    REQUIRE(dotted.strokes.size() >= 2u);
    CHECK(dotted.strokes.back().points.front().width >= dotted.penWidth * 0.5f);
}

AETHER_TEST("welcome backdrop: the veil keeps pale ink legible on any picture, and stays light on dark ones") {
    const auto contrastOver = [](float brightness) {
        const float shown = brightness * (1.0f - VeilOpacityFor(brightness));
        const float linear = shown <= 0.04045f ? shown / 12.92f : std::pow((shown + 0.055f) / 1.055f, 2.4f);
        return 1.05f / (linear + 0.05f);
    };
    for (float brightness = 0.0f; brightness <= 1.0f; brightness += 0.05f) CHECK(contrastOver(brightness) >= 4.5f);
    CHECK(VeilOpacityFor(0.15f) <= 0.15f);   // a dark picture stays itself
    CHECK(VeilOpacityFor(1.0f) <= 0.6f);     // it is a veil, not a curtain
    CHECK(VeilOpacityFor(std::numeric_limits<float>::quiet_NaN()) <= 0.15f);
}

AETHER_TEST("welcome backdrop: the picture fills the whole area in its own proportions, centred") {
    const auto wide = CoverRect(200.0f, 100.0f, 100.0f, 100.0f);  // wider than the area: the sides overflow
    CHECK_EQ(wide.top, 0.0f); CHECK_EQ(wide.bottom, 100.0f);
    CHECK_EQ(wide.left, -50.0f); CHECK_EQ(wide.right, 150.0f);
    const auto tall = CoverRect(100.0f, 400.0f, 200.0f, 200.0f);  // taller: top and bottom overflow
    CHECK_EQ(tall.left, 0.0f); CHECK_EQ(tall.right, 200.0f);
    CHECK_EQ(tall.top, -300.0f); CHECK_EQ(tall.bottom, 500.0f);
    const auto tiny = CoverRect(16.0f, 9.0f, 1920.0f, 1080.0f);  // enlarged when it must
    CHECK_EQ(tiny.left, 0.0f); CHECK_EQ(tiny.right, 1920.0f);
    const auto empty = CoverRect(0.0f, 0.0f, 300.0f, 200.0f);
    CHECK_EQ(empty.right, 300.0f); CHECK_EQ(empty.bottom, 200.0f);
}

AETHER_TEST("welcome backdrop: a picture is reduced to just cover the area, never enlarged; bad files are refused") {
    platform::ComApartment com{COINIT_APARTMENTTHREADED};
    const auto folder = std::filesystem::temp_directory_path() / L"AetherShellTests";
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    const auto file = folder / L"welcome-backdrop-source.bmp";
    {
        // The test's own picture: 64 x 32, its left half white and its right half black.
        constexpr std::uint32_t width = 64, height = 32, row = width * 3, size = row * height;
        std::vector<std::uint8_t> bytes(54 + size, 0);
        bytes[0] = 'B'; bytes[1] = 'M';
        const auto put = [&](std::size_t at, std::uint32_t value) { for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (8 * i)); };
        put(2, 54 + size); put(10, 54); put(14, 40); put(18, width); put(22, height); bytes[26] = 1; bytes[28] = 24; put(34, size);
        for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width / 2; ++x)
            for (std::uint32_t c = 0; c < 3; ++c) bytes[54 + y * row + x * 3 + c] = 255;
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    auto reduced = DecodeBackdrop(file, 16, 16);
    REQUIRE(reduced.ok());
    CHECK_EQ(reduced->width, 32u);   // 16 high covers 16 x 16 at this shape
    CHECK_EQ(reduced->height, 16u);
    CHECK_EQ(reduced->pixels.size(), 32u * 16u * 4u);
    CHECK(reduced->brightness > 0.4f);
    CHECK(reduced->brightness < 0.6f);
    CHECK_EQ(reduced->pixels[3], 255);  // opaque
    CHECK(reduced->pixels[0] > 240);    // the white half is on the left, right way up
    CHECK(reduced->pixels[31 * 4] < 15);
    auto kept = DecodeBackdrop(file, 1000, 1000);
    REQUIRE(kept.ok());
    CHECK_EQ(kept->width, 64u);
    CHECK_EQ(kept->height, 32u);
    CHECK_FALSE(DecodeBackdrop(file, 0, 16).ok());
    CHECK_FALSE(DecodeBackdrop(folder / L"welcome-backdrop-absent.bmp", 16, 16).ok());
    const auto garbage = folder / L"welcome-backdrop-garbage.bmp";
    std::ofstream(garbage, std::ios::binary) << "not a picture at all";
    CHECK_FALSE(DecodeBackdrop(garbage, 16, 16).ok());
    CHECK_FALSE(DecodeBackdrop(folder, 16, 16).ok());  // a folder
    std::filesystem::remove(file, ignored);
    std::filesystem::remove(garbage, ignored);
    CHECK_EQ(MeanBrightness({}), 0.0f);
}

AETHER_TEST("welcome lifecycle: it ends when written and ready, never before, and always by the deadline") {
    using Action = WelcomeController::Action;
    WelcomeTiming timing; timing.deadlineMs = 5000;
    WelcomeController controller(timing);
    REQUIRE(controller.SignedIn(true, 1000) == Action::Present);
    CHECK(controller.Update(false, false, 1500) == Action::None);
    CHECK(controller.Update(false, true, 2000) == Action::None);   // fast startup never truncates the writing
    CHECK(controller.Update(true, false, 3000) == Action::None);   // written, surfaces not ready yet
    CHECK(controller.Update(true, true, 3100) == Action::DismissAnimated);  // no extra delay
    CHECK(controller.Update(true, true, 3200) == Action::None);
    // A provider or surface that never gets ready cannot keep the desktop covered.
    WelcomeController stuck(timing);
    REQUIRE(stuck.SignedIn(true, 1000) == Action::Present);
    CHECK(stuck.Update(true, false, 5999) == Action::None);
    CHECK(stuck.Update(false, false, 6000) == Action::DismissAnimated);
    // A greeting that could not be drawn is simply not there.
    WelcomeController failed(timing);
    REQUIRE(failed.SignedIn(true, 1000) == Action::Present);
    failed.PresentationFailed();
    CHECK_FALSE(failed.Presenting());
    CHECK(failed.Update(true, true, 1100) == Action::None);
}
