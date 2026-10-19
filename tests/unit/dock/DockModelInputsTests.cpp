// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The DockModelInputs entry point and the fixed Start / Task View kinds.
#include "AetherTest.h"

#include "dock/DockModel.h"

#include <string>
#include <vector>

using namespace aether;
using namespace aether::dock;

namespace {

settings::PinnedApp Pin(std::wstring id) {
    settings::PinnedApp pin; pin.id = std::move(id); pin.displayName = L"Synthetic pinned app"; return pin;
}

apps::RunningApplication Running(std::wstring id, bool foreground = false) {
    apps::RunningApplication running; running.id = std::move(id); running.displayName = L"Synthetic running app";
    running.foreground = foreground; running.windows.push_back({reinterpret_cast<HWND>(1), L"Synthetic window", false});
    return running;
}

}  // namespace

AETHER_TEST("dock model inputs: the forwarding overload and the inputs overload build the same model") {
    settings::DockSettings config; config.pinned = {Pin(L"exe:a"), Pin(L"exe:b")};
    const std::vector running{Running(L"exe:b", true), Running(L"exe:c"), Running(L"exe:d")};
    auto legacy = BuildDockItems(config, running); REQUIRE(legacy.ok());
    auto inputs = BuildDockItems(DockModelInputs{config, running, {}}); REQUIRE(inputs.ok());
    REQUIRE_EQ(inputs->size(), legacy->size());
    for (std::size_t index = 0; index < legacy->size(); ++index) {
        CHECK_EQ((*inputs)[index].kind, (*legacy)[index].kind);
        CHECK_EQ((*inputs)[index].id, (*legacy)[index].id);
        CHECK_EQ((*inputs)[index].running, (*legacy)[index].running);
        CHECK_EQ((*inputs)[index].foreground, (*legacy)[index].foreground);
        CHECK_EQ((*inputs)[index].windowCount, (*legacy)[index].windowCount);
    }
    // A session order is accepted but not applied yet: the order stays pins first, then tracker order.
    const std::vector<std::wstring> order{L"exe:d", L"exe:a", L"exe:c", L"exe:b"};
    auto ordered = BuildDockItems(DockModelInputs{.settings = config, .running = running, .sessionOrder = order});
    REQUIRE(ordered.ok());
    REQUIRE_EQ(ordered->size(), 5u);
    CHECK_EQ((*ordered)[1].id, std::wstring(L"exe:a"));
    CHECK_EQ((*ordered)[2].id, std::wstring(L"exe:b"));
    CHECK_EQ((*ordered)[3].id, std::wstring(L"exe:c"));
    CHECK_EQ((*ordered)[4].id, std::wstring(L"exe:d"));
}

AETHER_TEST("dock model inputs: fixed entries are never reorderable and are not emitted yet") {
    CHECK(IsReorderable(DockItemKind::Pinned));
    CHECK(IsReorderable(DockItemKind::Running));
    for (const auto kind : {DockItemKind::Search, DockItemKind::Overflow, DockItemKind::Start, DockItemKind::TaskView}) {
        CHECK_FALSE(IsReorderable(kind));
        CHECK_FALSE(IsApplicationItem(kind));
    }
    CHECK(IsApplicationItem(DockItemKind::Pinned));
    CHECK(IsApplicationItem(DockItemKind::Running));
    // The new kinds are appended, so existing values keep their numbers.
    CHECK_EQ(static_cast<int>(DockItemKind::Overflow), 3);
    CHECK_EQ(static_cast<int>(DockItemKind::Start), 4);
    CHECK_EQ(static_cast<int>(DockItemKind::TaskView), 5);
    settings::DockSettings config;
    CHECK(config.showStartButton);
    CHECK(config.showTaskViewButton);
    config.pinned = {Pin(L"exe:a")};
    auto model = BuildDockItems(DockModelInputs{config, std::vector{Running(L"exe:b")}, {}});
    REQUIRE(model.ok());
    for (const auto& item : *model) {
        CHECK(item.kind != DockItemKind::Start);
        CHECK(item.kind != DockItemKind::TaskView);
    }
}
