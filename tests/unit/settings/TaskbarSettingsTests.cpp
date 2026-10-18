// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "core/Json.h"
#include "settings/SettingsSerialization.h"

using namespace aether;
using namespace aether::settings;

AETHER_TEST("settings taskbar: legacy settings require fresh explicit replacement consent") {
    auto root = json::Parse(R"({"schemaVersion":1,"general":{},"dock":{"pinned":[{"id":"exe:synthetic","displayName":"Fixture"}]}})");
    REQUIRE(root.ok());
    auto parsed = FromJson(*root); REQUIRE(parsed.ok());
    CHECK_FALSE(parsed->value.general.replaceWindowsTaskbar);
    CHECK_FALSE(parsed->value.general.taskbarPinsImported);
    CHECK_FALSE(parsed->value.general.startWithWindows);
    REQUIRE_EQ(parsed->value.dock.pinned.size(), 1u);
    CHECK(parsed->value.dock.pinned[0].arguments.empty());
}

AETHER_TEST("settings taskbar: consent import marker and exact launch arguments survive JSON roundtrip") {
    for (bool replacement : {false, true}) for (bool imported : {false, true}) {
        Settings settings;
        settings.general.replaceWindowsTaskbar = replacement;
        settings.general.taskbarPinsImported = imported;
        PinnedApp pin;
        pin.id = L"shell:synthetic-pin"; pin.displayName = L"Fixture Profile";
        pin.arguments = LR"(--profile="Synthetic profile" --literal=\\value --title=é)";
        settings.dock.pinned.push_back(pin);
        auto document = ToJson(settings); REQUIRE(document.ok());
        auto text = json::Parse(json::Serialize(*document)); REQUIRE(text.ok());
        auto parsed = FromJson(*text); REQUIRE(parsed.ok());
        CHECK_EQ(parsed->value.general.replaceWindowsTaskbar, replacement);
        CHECK_EQ(parsed->value.general.taskbarPinsImported, imported);
        REQUIRE_EQ(parsed->value.dock.pinned.size(), 1u);
        CHECK_EQ(parsed->value.dock.pinned[0].arguments, pin.arguments);
        CHECK(parsed->warnings.empty());
    }
}

AETHER_TEST("settings taskbar: oversized and embedded NUL pin arguments reject the whole pin") {
    Settings settings;
    PinnedApp oversized; oversized.id = L"exe:oversized"; oversized.arguments.assign(32768, L'x');
    PinnedApp embedded; embedded.id = L"exe:embedded"; embedded.arguments = std::wstring(L"--synthetic\0--marker", 21);
    PinnedApp boundary; boundary.id = L"exe:boundary"; boundary.arguments.assign(32767, L'x');
    PinnedApp valid; valid.id = L"exe:valid"; valid.arguments = L"--profile=fixture";
    settings.dock.pinned = {oversized, embedded, boundary, valid};
    auto warnings = Validate(settings); REQUIRE(warnings.ok());
    REQUIRE_EQ(settings.dock.pinned.size(), 2u);
    CHECK_EQ(settings.dock.pinned[0].id, boundary.id);
    CHECK_EQ(settings.dock.pinned[0].arguments.size(), 32767u);
    CHECK_EQ(settings.dock.pinned[1].arguments, valid.arguments);
    REQUIRE_EQ(warnings->size(), 2u);
    for (const auto& warning : *warnings) CHECK(warning.find("synthetic") == std::string::npos);
}
