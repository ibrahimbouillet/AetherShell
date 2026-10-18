// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "dock/DockLayout.h"
#include "dock/DockModel.h"

#include <cmath>
#include <limits>

using namespace aether;
using namespace aether::dock;

namespace {
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
    settings::DockSettings config; config.pinned = {Pin(L"exe:a", L"A"), Pin(L"exe:b", L"B")};
    auto model = BuildDockItems(config, {Running(L"exe:c")}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 4u);
    CHECK_EQ((*model)[0].kind, DockItemKind::Search);
    CHECK_EQ((*model)[1].id, std::wstring(L"exe:a")); CHECK_EQ((*model)[2].id, std::wstring(L"exe:b"));
    CHECK_EQ((*model)[3].kind, DockItemKind::Running); CHECK_EQ((*model)[3].id, std::wstring(L"exe:c"));
}
AETHER_TEST("dock model: pins merge running state by case-insensitive identity without duplicates") {
    settings::DockSettings config; config.pinned = {Pin(L"exe:a"), Pin(L"EXE:A")};
    auto model = BuildDockItems(config, {Running(L"EXE:A", true), Running(L"exe:b")}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 3u);
    CHECK((*model)[1].running); CHECK((*model)[1].foreground); CHECK_EQ((*model)[1].windowCount, 1u);
    CHECK((*model)[1].runningApplication.has_value());
}
AETHER_TEST("dock model: disabling unpinned running applications keeps pinned running indicators") {
    settings::DockSettings config; config.showRunningApps = false; config.pinned = {Pin(L"exe:a")};
    auto model = BuildDockItems(config, {Running(L"exe:a"), Running(L"exe:b")}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 2u); CHECK((*model)[1].running);
}
AETHER_TEST("dock model: window counts and foreground state merge across matching running entries") {
    settings::DockSettings config; config.pinned = {Pin(L"exe:a")};
    auto model = BuildDockItems(config, {Running(L"exe:a"), Running(L"exe:a", true)}); REQUIRE(model.ok());
    REQUIRE_EQ(model->size(), 2u); CHECK_EQ((*model)[1].windowCount, 2u); CHECK((*model)[1].foreground);
    REQUIRE((*model)[1].runningApplication.has_value()); CHECK_EQ((*model)[1].runningApplication->windows.size(), 2u);
}
AETHER_TEST("dock model: duplicate unpinned tracker identities merge windows and foreground state") {
    auto model = BuildDockItems(settings::DockSettings{}, {Running(L"exe:a"), Running(L"EXE:A", true)}); REQUIRE(model.ok());
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
AETHER_TEST("dock model: moving pins preserves relative order and validates destinations") {
    const std::vector pins{Pin(L"exe:a"), Pin(L"exe:b"), Pin(L"exe:c")};
    auto moved = MovePin(pins, L"exe:a", 2); REQUIRE(moved.ok());
    CHECK_EQ((*moved)[0].id, std::wstring(L"exe:b")); CHECK_EQ((*moved)[1].id, std::wstring(L"exe:c"));
    CHECK_EQ((*moved)[2].id, std::wstring(L"exe:a"));
    CHECK_EQ(MovePin(pins, L"exe:missing", 0).error().code, ErrorCode::NotFound);
    CHECK_EQ(MovePin(pins, L"exe:a", 3).error().code, ErrorCode::InvalidArgument);
}
AETHER_TEST("dock overflow: shrink before hiding trailing running applications") {
    settings::DockSettings config; config.pinned = {Pin(L"exe:a"), Pin(L"exe:b")};
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
    settings::DockSettings config;
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
    settings::DockSettings config; config.magnification = 2;
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
