// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "core/Dispatcher.h"
#include "core/Json.h"
#include "settings/SettingsSerialization.h"
#include "settings/SettingsService.h"

#include <chrono>
#include <mutex>
#include <thread>

using namespace aether;
using namespace aether::settings;
using namespace std::chrono_literals;

namespace {
class RecordingStore final : public ISettingsStore {
public:
    Settings initial;
    std::mutex mutex;
    std::vector<Settings> saved;
    std::vector<bool> resets;
    Result<LoadedSettings> Load() override { return LoadedSettings{initial, {LoadStatus::Loaded, {}}}; }
    Result<void> Save(const Settings& settings, bool reset) override {
        std::lock_guard lock(mutex); saved.push_back(settings); resets.push_back(reset); return Ok();
    }
};

bool Pump(ManualDispatcher& dispatcher, const std::function<bool()>& ready) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!ready() && std::chrono::steady_clock::now() < end) {
        dispatcher.RunPending(); std::this_thread::sleep_for(1ms);
    }
    dispatcher.RunPending(); return ready();
}

void LoadService(SettingsService& service, ManualDispatcher& dispatcher) {
    bool done = false;
    REQUIRE(service.Load([&](Result<LoadReport> result) { REQUIRE(result.ok()); done = true; }).ok());
    REQUIRE(Pump(dispatcher, [&] { return done; }));
}
}  // namespace

AETHER_TEST("settings taskbar: legacy settings without the key replace the taskbar by default (D23)") {
    auto root = json::Parse(R"({"schemaVersion":1,"general":{},"dock":{"pinned":[{"id":"exe:synthetic","displayName":"Fixture"}]}})");
    REQUIRE(root.ok());
    auto parsed = FromJson(*root); REQUIRE(parsed.ok());
    CHECK(parsed->value.general.replaceWindowsTaskbar);
    CHECK_FALSE(parsed->value.general.taskbarPinsImported);
    CHECK_FALSE(parsed->value.general.startWithWindows);  // startup at sign-in stays opt-in
    REQUIRE_EQ(parsed->value.dock.pinned.size(), 1u);
    CHECK(parsed->value.dock.pinned[0].arguments.empty());
}

AETHER_TEST("settings taskbar: defaults replace the taskbar while startup at sign-in stays off") {
    const Settings defaults;
    CHECK(defaults.general.replaceWindowsTaskbar);
    CHECK_FALSE(defaults.general.startWithWindows);
    CHECK_FALSE(defaults.general.taskbarPinsImported);
    auto document = ToJson(defaults); REQUIRE(document.ok());
    const auto* general = document->Find("general"); REQUIRE(general != nullptr);
    const auto* replace = general->Find("replaceWindowsTaskbar"); REQUIRE(replace != nullptr);
    CHECK_EQ(replace->AsBool(), std::optional<bool>(true));  // every save stores the choice explicitly
}

AETHER_TEST("settings taskbar: an explicit off survives JSON and a non-boolean falls back to on") {
    auto off = json::Parse(R"({"schemaVersion":1,"dock":{"metricsVersion":2},"general":{"replaceWindowsTaskbar":false}})");
    REQUIRE(off.ok());
    auto parsed = FromJson(*off); REQUIRE(parsed.ok());
    CHECK_FALSE(parsed->value.general.replaceWindowsTaskbar);
    CHECK(parsed->warnings.empty());
    auto written = ToJson(parsed->value); REQUIRE(written.ok());
    auto reread = FromJson(*written); REQUIRE(reread.ok());
    CHECK_FALSE(reread->value.general.replaceWindowsTaskbar);

    for (const char* text : {R"({"schemaVersion":1,"dock":{"metricsVersion":2},"general":{"replaceWindowsTaskbar":"no"}})",
                             R"({"schemaVersion":1,"dock":{"metricsVersion":2},"general":{"replaceWindowsTaskbar":0}})",
                             R"({"schemaVersion":1,"dock":{"metricsVersion":2},"general":{"replaceWindowsTaskbar":null}})"}) {
        auto root = json::Parse(text); REQUIRE(root.ok());
        auto invalid = FromJson(*root); REQUIRE(invalid.ok());
        CHECK(invalid->value.general.replaceWindowsTaskbar);
        REQUIRE_EQ(invalid->warnings.size(), 1u);
        CHECK(invalid->warnings[0].find("replaceWindowsTaskbar") != std::string::npos);
    }
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

AETHER_TEST("settings taskbar: reset to defaults keeps the replacement choice and resets the rest") {
    for (bool replacement : {false, true}) {
        ManualDispatcher dispatcher;
        auto store = std::make_shared<RecordingStore>();
        store->initial.general.replaceWindowsTaskbar = replacement;
        store->initial.general.taskbarPinsImported = true;
        store->initial.general.syncTaskbarPins = false;
        store->initial.dock.iconSize = 64;
        store->initial.dock.explorerDefaultApplied = true;
        store->initial.dock.dismissedPins = {L"exe:synthetic-dismissed"};
        PinnedApp pin; pin.id = L"exe:synthetic"; store->initial.dock.pinned.push_back(pin);
        SettingsService service(store, dispatcher);
        LoadService(service, dispatcher);
        bool saved = false;
        REQUIRE(service.ResetToDefaults([&](Result<void> result) { saved = result.ok(); }).ok());
        const auto& current = service.Current();
        CHECK_EQ(current.general.replaceWindowsTaskbar, replacement);
        CHECK_FALSE(current.general.taskbarPinsImported);  // an emptied Dock imports once more
        CHECK(current.general.syncTaskbarPins);
        CHECK_EQ(current.dock.iconSize, Settings{}.dock.iconSize);
        CHECK_FALSE(current.dock.explorerDefaultApplied);
        CHECK(current.dock.dismissedPins.empty());
        CHECK(current.dock.pinned.empty());
        REQUIRE(Pump(dispatcher, [&] { return saved; }));
        std::lock_guard lock(store->mutex);
        REQUIRE_EQ(store->saved.size(), 1u);
        CHECK(store->resets[0]);
        CHECK_EQ(store->saved[0].general.replaceWindowsTaskbar, replacement);
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
