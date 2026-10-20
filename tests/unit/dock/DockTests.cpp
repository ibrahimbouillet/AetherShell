// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "dock/DockLayout.h"
#include "dock/DockModel.h"
#include "graphics/Animation.h"

#include <cmath>
#include <limits>

using namespace aether;
using namespace aether::dock;

namespace {
settings::DockSettings ApplicationSettings() {
    settings::DockSettings config; config.showStartButton = false; config.showTaskViewButton = false;
    return config;
}
settings::PinnedApp Pin(std::wstring id, std::wstring name = L"Synthetic app") {
    settings::PinnedApp pin; pin.id = std::move(id); pin.displayName = std::move(name); return pin;
}
apps::RunningApplication Running(std::wstring id, bool foreground = false) {
    apps::RunningApplication running; running.id = std::move(id); running.displayName = L"Synthetic running app";
    running.foreground = foreground; running.windows.push_back({reinterpret_cast<HWND>(1), L"Synthetic window", false});
    return running;
}
}

AETHER_TEST("dock model: Search precedes ordered pins and unpinned running applications") {
    auto config = ApplicationSettings(); config.pinned = {Pin(L"exe:a", L"A"), Pin(L"exe:b", L"B")};
    auto model = BuildDockItems(config, {Running(L"exe:c")}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 4u);
    CHECK_EQ((*model)[0].kind, DockItemKind::Search);
    CHECK_EQ((*model)[1].id, std::wstring(L"exe:a")); CHECK_EQ((*model)[2].id, std::wstring(L"exe:b"));
    CHECK_EQ((*model)[3].kind, DockItemKind::Running); CHECK_EQ((*model)[3].id, std::wstring(L"exe:c"));
}
AETHER_TEST("dock model: pins merge running state by case-insensitive identity without duplicates") {
    auto config = ApplicationSettings(); config.pinned = {Pin(L"exe:a"), Pin(L"EXE:A")};
    auto model = BuildDockItems(config, {Running(L"EXE:A", true), Running(L"exe:b")}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 3u);
    CHECK((*model)[1].running); CHECK((*model)[1].foreground); CHECK_EQ((*model)[1].windowCount, 1u);
    CHECK((*model)[1].runningApplication.has_value());
}
AETHER_TEST("dock model: disabling unpinned running applications keeps pinned running indicators") {
    auto config = ApplicationSettings(); config.showRunningApps = false; config.pinned = {Pin(L"exe:a")};
    auto model = BuildDockItems(config, {Running(L"exe:a"), Running(L"exe:b")}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 2u); CHECK((*model)[1].running);
}
AETHER_TEST("dock model: window counts and foreground state merge across matching running entries") {
    auto config = ApplicationSettings(); config.pinned = {Pin(L"exe:a")};
    auto model = BuildDockItems(config, {Running(L"exe:a"), Running(L"exe:a", true)}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 2u); CHECK_EQ((*model)[1].windowCount, 2u); CHECK((*model)[1].foreground);
    REQUIRE((*model)[1].runningApplication.has_value()); CHECK_EQ((*model)[1].runningApplication->windows.size(), 2u);
}
AETHER_TEST("dock model: duplicate unpinned tracker identities merge windows and foreground state") {
    auto model = BuildDockItems(ApplicationSettings(), {Running(L"exe:a"), Running(L"EXE:A", true)}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 2u); CHECK_EQ((*model)[1].windowCount, 2u); CHECK((*model)[1].foreground);
    REQUIRE((*model)[1].runningApplication.has_value()); CHECK((*model)[1].runningApplication->foreground);
}
AETHER_TEST("dock model: pinning copies launch identity and is idempotent") {
    apps::ApplicationInfo app; app.id = L"exe:synthetic"; app.displayName = L"Synthetic";
    app.parsingName = L"synthetic-shell-name"; app.executablePath = L"synthetic.exe"; app.shortcutPath = L"synthetic.lnk";
    auto pins = PinApplication({}, app); REQUIRE(pins.ok()); REQUIRE_EQ(pins->size(), 1u);
    CHECK_EQ((*pins)[0].executablePath, app.executablePath); CHECK_EQ((*pins)[0].shortcutPath, app.shortcutPath);
    app.id = L"EXE:SYNTHETIC"; auto again = PinApplication(*pins, app); REQUIRE(again.ok()); CHECK_EQ(again->size(), 1u);
    auto removed = UnpinApplication(*pins, app.id); REQUIRE(removed.ok()); CHECK(removed->empty());
}
AETHER_TEST("dock model: transient identities and too many pins fail explicitly") {
    apps::ApplicationInfo app; app.id = L"window:synthetic"; CHECK_EQ(PinFromApplication(app).error().code, ErrorCode::Unsupported);
    app.id.clear(); CHECK_FALSE(PinFromApplication(app).ok());
    std::vector<settings::PinnedApp> pins(64, Pin(L"exe:a")); app.id = L"exe:new";
    CHECK_EQ(PinApplication(pins, app).error().code, ErrorCode::InvalidState);
}
AETHER_TEST("dock model: a pinned launch retains arguments without depending on its shortcut file") {
    apps::ApplicationInfo app; app.id = L"exe:synthetic-profile"; app.displayName = L"Synthetic profile";
    app.executablePath = LR"(C:\AetherFixture\synthetic.exe)"; app.shortcutPath = LR"(C:\AetherFixture\profile.lnk)"; app.arguments = L"--profile \"Synthetic profile\" --mode=test";
    auto pin = PinFromApplication(app); REQUIRE(pin.ok()); CHECK_EQ(pin->arguments, app.arguments);
    auto config = ApplicationSettings(); config.pinned.push_back(*pin);
    auto model = BuildDockItems(config, {}); REQUIRE(model.ok()); REQUIRE_EQ(model->size(), 2u);
    CHECK_EQ((*model)[1].application.arguments, app.arguments);
    CHECK_EQ((*model)[1].application.executablePath, app.executablePath);
    CHECK_EQ((*model)[1].application.shortcutPath, app.shortcutPath);
}
AETHER_TEST("dock model: moving pins preserves relative order and validates destinations") {
    const std::vector pins{Pin(L"exe:a"), Pin(L"exe:b"), Pin(L"exe:c")};
    auto moved = MovePin(pins, L"exe:a", 2); REQUIRE(moved.ok());
    CHECK_EQ((*moved)[0].id, std::wstring(L"exe:b")); CHECK_EQ((*moved)[1].id, std::wstring(L"exe:c"));
    CHECK_EQ((*moved)[2].id, std::wstring(L"exe:a"));
    CHECK_EQ(MovePin(pins, L"exe:missing", 0).error().code, ErrorCode::NotFound);
    CHECK_EQ(MovePin(pins, L"exe:a", 3).error().code, ErrorCode::InvalidArgument);
}
AETHER_TEST("dock drag: running groups move in the session without pinning themselves or neighbours") {
    auto config = ApplicationSettings();config.pinned={Pin(L"exe:a"),Pin(L"exe:b")};
    config.pinned[1].arguments=L"--synthetic-profile";
    const std::vector running{Running(L"exe:c"),Running(L"window:temporary")};
    auto moved=ReorderDockItems(DockModelInputs{config,running,{}},L"window:temporary",L"exe:a");REQUIRE(moved.ok());
    CHECK_FALSE(moved->pinsChanged);REQUIRE_EQ(moved->pins.size(),2u);
    CHECK_EQ(moved->pins[1].arguments,L"--synthetic-profile");
    auto items=BuildDockItems(DockModelInputs{config,running,moved->sessionOrder});REQUIRE(items.ok());
    REQUIRE_EQ(items->size(),5u);CHECK_EQ((*items)[1].id,L"window:temporary");CHECK_EQ((*items)[1].kind,DockItemKind::Running);
    auto restarted=BuildDockItems(config,running);REQUIRE(restarted.ok());CHECK_EQ((*restarted)[1].id,L"exe:a");
}
AETHER_TEST("dock drag: pinned movement persists only the existing pin order") {
    auto config = ApplicationSettings();config.pinned={Pin(L"exe:a"),Pin(L"exe:b")};
    const std::vector running{Running(L"exe:c"),Running(L"exe:d")};
    auto moved=ReorderDockItems(DockModelInputs{config,running,{}},L"exe:a",L"exe:d");REQUIRE(moved.ok());
    REQUIRE_EQ(moved->pins.size(),2u);CHECK(moved->pinsChanged);
    CHECK_EQ(moved->pins[0].id,L"exe:b");CHECK_EQ(moved->pins[1].id,L"exe:a");
    config.pinned=moved->pins;
    auto session=BuildDockItems(DockModelInputs{config,running,moved->sessionOrder});REQUIRE(session.ok());
    CHECK_EQ((*session)[4].id,L"exe:a");
    auto restarted=BuildDockItems(config,running);REQUIRE(restarted.ok());CHECK_EQ((*restarted)[2].id,L"exe:a");
}
AETHER_TEST("dock drag: fixed and missing targets fail but a full pin list does not prevent session movement") {
    auto config = ApplicationSettings();
    for(int index=0;index<64;++index)config.pinned.push_back(Pin(L"exe:"+std::to_wstring(index)));
    const std::vector running{Running(L"window:temporary")};
    auto moved=ReorderDockItems(DockModelInputs{config,running,{}},L"window:temporary",L"exe:0");REQUIRE(moved.ok());
    CHECK_EQ(moved->pins.size(),64u);CHECK_FALSE(moved->pinsChanged);
    CHECK_EQ(ReorderDockItems(DockModelInputs{config,running,{}},L"missing",L"exe:0").error().code,ErrorCode::NotFound);
    CHECK_EQ(ReorderDockItems(DockModelInputs{config,running,{}},L"exe:0",L"aether:search").error().code,ErrorCode::NotFound);
}
AETHER_TEST("dock overflow: shrink before hiding trailing running applications") {
    auto config = ApplicationSettings(); config.pinned = {Pin(L"exe:a"), Pin(L"exe:b")};
    std::vector<apps::RunningApplication> running;
    for (int i = 0; i < 12; ++i) running.push_back(Running(L"exe:run" + std::to_wstring(i)));
    auto model = BuildDockItems(config, running); REQUIRE(model.ok());
    auto fitted = FitDockItems(*model, config, 400); REQUIRE(fitted.ok());
    CHECK_EQ(fitted->iconSize, 32.0f); CHECK_FALSE(fitted->overflow.empty());
    CHECK_EQ(fitted->items[1].kind, DockItemKind::Pinned); CHECK_EQ(fitted->items[2].kind, DockItemKind::Pinned);
    CHECK_EQ(fitted->items.back().kind, DockItemKind::Overflow);
    CHECK_EQ(fitted->items.size() + fitted->overflow.size() - 1, model->size());
}
AETHER_TEST("dock overflow: excess pins remain reachable and extremely narrow work areas keep Search") {
    auto config = ApplicationSettings();
    for (int i = 0; i < 64; ++i) config.pinned.push_back(Pin(L"exe:" + std::to_wstring(i)));
    auto model = BuildDockItems(config, {}); REQUIRE(model.ok());
    auto fitted = FitDockItems(*model, config, 300); REQUIRE(fitted.ok());
    CHECK_FALSE(fitted->overflow.empty()); CHECK_EQ(fitted->items.back().kind, DockItemKind::Overflow);
    auto narrow = FitDockItems(*model, config, 56); REQUIRE(narrow.ok());
    REQUIRE_EQ(narrow->items.size(), 1u); CHECK_EQ(narrow->items[0].kind, DockItemKind::Search);
    CHECK_EQ(narrow->overflow.size(), 64u); CHECK_EQ(narrow->magnification, 1.0f);
    CHECK_FALSE(FitDockItems(*model, config, 1).ok());
}
AETHER_TEST("dock layout: many pointer positions never overlap and share an upward baseline") {
    for (std::size_t count = 1; count <= 20; ++count) {
        for (int x = -100; x <= 1400; x += 13) {
            auto scales = DockMagnification(count, 48, 8, 1.8f, 1400, static_cast<float>(x)); REQUIRE(scales.ok());
            auto layout = ArrangeDockLayout(48, 8, 1.8f, 1400, *scales); REQUIRE(layout.ok());
            for (std::size_t i = 0; i < count; ++i) {
                CHECK(layout->items[i].icon.top >= 0);
                CHECK(std::abs(layout->items[i].icon.bottom - layout->items[0].icon.bottom) < 0.001f);
                if (i > 0) CHECK(layout->items[i - 1].icon.right + 7.99f <= layout->items[i].icon.left);
                const auto& icon = layout->items[i].icon;
                CHECK_EQ(HitTest(*layout, {(icon.left + icon.right) / 2, (icon.top + icon.bottom) / 2}), std::optional<std::size_t>(i));
            }
        }
    }
    DockLayout crossing;
    crossing.items.push_back({{0,0,60,60},1});crossing.items.push_back({{30,0,90,60},1});
    CHECK_EQ(HitTest(crossing,{45,30}),std::optional<std::size_t>(1));
}
AETHER_TEST("dock layout: centered magnification is symmetric and largest at the pointer") {
    auto scales = DockMagnification(7, 48, 8, 1.6f, 800, 400.0f); REQUIRE(scales.ok());
    CHECK(std::abs((*scales)[3] - 1.6f) < 0.001f);
    for (std::size_t i = 0; i < 3; ++i) CHECK(std::abs((*scales)[i] - (*scales)[6 - i]) < 0.001f);
    auto layout = ArrangeDockLayout(48, 8, 1.6f, 800, *scales); REQUIRE(layout.ok());
    CHECK(std::abs((layout->panel.left + layout->panel.right) / 2 - 400) < 0.001f);
}
AETHER_TEST("dock layout: disabled magnification outside pointer and invalid geometry are safe") {
    for (const auto point : {std::optional<float>{}, std::optional<float>{-100.0f}, std::optional<float>{9999.0f}}) {
        auto scales = DockMagnification(5, 48, 8, 2, 800, point); REQUIRE(scales.ok());
        for (const auto scale : *scales) CHECK_EQ(scale, 1.0f);
    }
    auto disabled = DockMagnification(5, 48, 8, 1, 800, 400.0f); REQUIRE(disabled.ok());
    for (const auto scale : *disabled) CHECK_EQ(scale, 1.0f);
    CHECK_FALSE(DockMagnification(5, 0, 8, 1, 800, {}).ok());
    CHECK_FALSE(DockMagnification(5, 48, 8, 1, 800, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_FALSE(ArrangeDockLayout(48, 8, 1.5f, 800, std::vector<float>{0.5f}).ok());
}
AETHER_TEST("dock layout: fitted magnification remains inside the available work width") {
    auto config = ApplicationSettings(); config.magnification = 2;
    for (int count = 1; count < 65; ++count) {
        config.pinned.push_back(Pin(L"exe:" + std::to_wstring(count)));
        auto model = BuildDockItems(config, {}); REQUIRE(model.ok());
        for (const float width : {56.0f, 120.0f, 320.0f, 700.0f, 1600.0f}) {
            auto fitted = FitDockItems(*model, config, width); REQUIRE(fitted.ok());
            for (float x = 0; x <= width; x += 19) {
                auto scales = DockMagnification(fitted->items.size(), fitted->iconSize, 8, fitted->magnification, width, x); REQUIRE(scales.ok());
                auto layout = ArrangeDockLayout(fitted->iconSize, 8, fitted->magnification, width, *scales); REQUIRE(layout.ok());
                CHECK(layout->panel.left >= -0.01f); CHECK(layout->panel.right <= width + 0.01f);
            }
        }
    }
}
AETHER_TEST("dock placement: visible bottom gap is not doubled by transparent window padding") {
    auto layout = ArrangeDockLayout(48, 8, 1.45f, 800, std::vector<float>{1, 1, 1}); REQUIRE(layout.ok());
    const RECT area{0, 0, 1920, 1080};
    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        for (const float margin : {0.0f, 6.0f, 10.0f, 20.0f}) {
            auto placed = PlaceDock(*layout, area, dpi, 48, 1.45f, margin, false); REQUIRE(placed.ok());
            const double scale = static_cast<double>(dpi) / 96;
            const double panelBottom = placed->windowPx.top + layout->panel.bottom * scale;
            CHECK(std::abs(panelBottom - (area.bottom - std::ceil(margin * scale))) <= 1.0);
            CHECK_EQ(placed->windowPx.left + placed->windowPx.right, area.left + area.right);
        }
    }
}
AETHER_TEST("dock placement: reservation contains every magnified bouncing icon and the panel shadow") {
    const RECT area{-1920, -200, 0, 1080};
    for (const float size : {32.0f, 48.0f, 96.0f}) {
        for (const float magnification : {1.0f, 1.45f, 2.0f}) {
            auto layout = ArrangeDockLayout(size, 8, magnification, 800, std::vector<float>{magnification}); REQUIRE(layout.ok());
            for (const UINT dpi : {96u, 120u, 144u, 192u}) {
                auto placed = PlaceDock(*layout, area, dpi, size, magnification, 10, false); REQUIRE(placed.ok());
                const double scale = static_cast<double>(dpi) / 96;
                const double clientBottom = area.bottom - placed->reservationHeightPx;
                const double shadowTop = placed->windowPx.top + (layout->panel.top - kDockShadowMargin) * scale;
                const double bounceFocusTop = placed->windowPx.top + (layout->items[0].icon.top - graphics::MaxBounceHeight(size) - graphics::Motion().liftOffset - 6) * scale;
                CHECK(clientBottom <= shadowTop); CHECK(clientBottom <= bounceFocusTop);
                CHECK(placed->reservationHeightPx <= static_cast<LONG>(std::ceil((size * magnification * graphics::Motion().liftScale + graphics::MaxBounceHeight(size) + 74) * scale)));
            }
        }
    }
}
AETHER_TEST("dock placement: reduced motion removes bounce reservation and changed edges do not compound") {
    auto layout = ArrangeDockLayout(48, 8, 1.8f, 800, std::vector<float>{1}); REQUIRE(layout.ok());
    const RECT work{0, 40, 1920, 1032};
    const RECT monitor{0, 40, 1920, 1080};
    auto ordinary = PlaceDock(*layout, work, 96, 48, 1.8f, 6, false); REQUIRE(ordinary.ok());
    auto replacement = PlaceDock(*layout, monitor, 96, 48, 1.8f, 6, false); REQUIRE(replacement.ok());
    auto reduced = PlaceDock(*layout, monitor, 96, 48, 1.8f, 6, true); REQUIRE(reduced.ok());
    CHECK_EQ(replacement->windowPx.bottom - ordinary->windowPx.bottom, 48);
    CHECK_EQ(replacement->reservationHeightPx, ordinary->reservationHeightPx);
    CHECK(replacement->reservationHeightPx > reduced->reservationHeightPx);
}
AETHER_TEST("dock placement: compact reservation excludes animation while the HWND retains its complete footprint") {
    auto layout=ArrangeDockLayout(40,8,1.45f,800,std::vector<float>{1});REQUIRE(layout.ok());
    const RECT area{0,0,1920,1080};
    auto compact=PlaceDock(*layout,area,144,40,1.45f,4,false,false);REQUIRE(compact.ok());
    auto animated=PlaceDock(*layout,area,144,40,1.45f,4,false,true);REQUIRE(animated.ok());
    CHECK(::EqualRect(&compact->windowPx,&animated->windowPx));
    CHECK_EQ(compact->reservationHeightPx,static_cast<LONG>(std::ceil((layout->panel.bottom-layout->panel.top+4)*1.5f)));
    CHECK(animated->reservationHeightPx>compact->reservationHeightPx);
    CHECK(layout->panel.bottom+kDockShadowMargin+kDockEntranceOffset<=layout->windowHeight);
    CHECK(layout->panel.top-kDockShadowMargin>=0);
}
AETHER_TEST("dock placement: malformed geometry is rejected before native coordinates are calculated") {
    auto layout = ArrangeDockLayout(48, 8, 1.45f, 800, std::vector<float>{1}); REQUIRE(layout.ok());
    const RECT area{0, 0, 1920, 1080};
    CHECK_FALSE(PlaceDock(*layout, area, 0, 48, 1.45f, 10, false).ok());
    CHECK_FALSE(PlaceDock(*layout, area, 96, 48, 1.45f, -1, false).ok());
    CHECK_FALSE(PlaceDock(*layout, RECT{0, 0, 100, 1080}, 96, 48, 1.45f, 10, false).ok());
    auto invalid = *layout; invalid.windowHeight = std::numeric_limits<float>::infinity();
    CHECK_FALSE(PlaceDock(invalid, area, 96, 48, 1.45f, 10, false).ok());
    invalid = *layout; invalid.windowWidth = std::numeric_limits<float>::max();
    CHECK_FALSE(PlaceDock(invalid, area, 96, 48, 1.45f, 10, false).ok());
    const LONG limit = std::numeric_limits<LONG>::max();
    CHECK_FALSE(PlaceDock(*layout, RECT{0, limit - 1080, 1920, limit}, 96, 48, 1.45f, 0, false).ok());
    auto fractional = ArrangeDockLayout(48, 8, 1.45f, 1919.0f * 96 / 144, std::vector<float>{1}); REQUIRE(fractional.ok());
    auto placed = PlaceDock(*fractional, RECT{0, 0, 1919, 1080}, 144, 48, 1.45f, 10, false); REQUIRE(placed.ok());
    CHECK_EQ(placed->windowPx.right - placed->windowPx.left, 1919);
}
