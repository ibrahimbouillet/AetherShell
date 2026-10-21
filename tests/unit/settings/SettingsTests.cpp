// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/Dispatcher.h"
#include "core/FileSystem.h"
#include "core/Json.h"
#include "settings/ConfigurationMigrator.h"
#include "settings/SettingsSerialization.h"
#include "settings/SettingsService.h"
#include "settings/SettingsStore.h"
#include "settings/StartupRegistration.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace aether;
using namespace aether::settings;
using namespace std::chrono_literals;

namespace {

std::filesystem::path NewFile() { return test::MakeTempDirectory("settings") / L"settings.json"; }

bool Pump(ManualDispatcher& dispatcher, const std::function<bool()>& ready) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!ready() && std::chrono::steady_clock::now() < end) {
        dispatcher.RunPending();
        std::this_thread::sleep_for(1ms);
    }
    dispatcher.RunPending();
    return ready();
}

void Load(SettingsService& service, ManualDispatcher& dispatcher) {
    bool done = false;
    bool okay = false;
    REQUIRE(service.Load([&](Result<LoadReport> result) { okay = result.ok(); done = true; }).ok());
    REQUIRE(Pump(dispatcher, [&] { return done; }));
    REQUIRE(okay);
}

std::vector<std::filesystem::path> Matching(const std::filesystem::path& directory, std::wstring_view text) {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().wstring().find(text) != std::wstring::npos) paths.push_back(entry.path());
    }
    return paths;
}

class MemoryStore final : public ISettingsStore {
public:
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic<bool> touchedUi{false};
    std::mutex mutex;
    std::condition_variable changed;
    Settings initial;
    std::vector<Settings> saved;
    std::vector<bool> resetFlags;
    bool fail = false;
    bool failLoad = false;
    bool released = false;
    std::size_t blockSave = 0;

    Result<LoadedSettings> Load() override {
        if (std::this_thread::get_id() == owner) touchedUi = true;
        if (failLoad) return MakeError(ErrorCode::AccessDenied, "synthetic load failure");
        return LoadedSettings{initial, {LoadStatus::Loaded, {}}};
    }
    Result<void> Save(const Settings& settings, bool reset) override {
        if (std::this_thread::get_id() == owner) touchedUi = true;
        std::unique_lock lock(mutex);
        saved.push_back(settings);
        resetFlags.push_back(reset);
        changed.notify_all();
        if (saved.size() == blockSave) changed.wait_for(lock, 5s, [&] { return released; });
        if (fail) return MakeError(ErrorCode::AccessDenied, "synthetic persistence failure");
        return Ok();
    }
    bool WaitSaves(std::size_t count) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [&] { return saved.size() >= count; });
    }
    void Release() {
        { std::lock_guard lock(mutex); released = true; }
        changed.notify_all();
    }
};

class MemoryRegistry final : public IStartupRegistry {
public:
    std::optional<std::wstring> run;
    std::vector<std::uint8_t> approval;
    int writes = 0;
    int deletes = 0;
    bool throwRead = false;
    Result<std::optional<std::wstring>> ReadRun() override { return run; }
    Result<std::vector<std::uint8_t>> ReadApproval() override {
        if (throwRead) throw std::runtime_error("synthetic-private-registry-data");
        return approval;
    }
    Result<void> WriteRun(std::wstring command) override { ++writes; run = std::move(command); return Ok(); }
    Result<void> DeleteRun() override { ++deletes; run.reset(); return Ok(); }
};

}  // namespace

AETHER_TEST("settings serialization: defaults round trip without changing existing defaults") {
    const Settings defaults;
    auto document = ToJson(defaults);
    REQUIRE(document.ok());
    auto parsed = FromJson(*document);
    REQUIRE(parsed.ok());
    CHECK(parsed->warnings.empty());
    CHECK_EQ(*ToJson(parsed->value), *document);
    CHECK_EQ(parsed->value.dock.iconSize, 40);
    CHECK_EQ(parsed->value.dock.bottomMargin, 4);
    CHECK_EQ(parsed->value.dock.metricsVersion, 2);
    CHECK(parsed->value.topBar.reserveSpace);
    CHECK_FALSE(parsed->value.dock.reserveSpace);
    CHECK_FALSE(parsed->value.weather.enabled);
    CHECK_FALSE(parsed->value.general.startWithWindows);
    CHECK_EQ(parsed->value.general.searchHotkey, (HotkeyChord{3, 0x20}));
    CHECK_EQ(parsed->value.general.focusDockHotkey, (HotkeyChord{3, 0x44}));
}

AETHER_TEST("settings serialization: all enum strings survive round trips") {
    for (int index = 0; index < 3; ++index) {
        Settings settings;
        settings.general.motion = static_cast<MotionPreference>(index);
        settings.topBar.clockFormat = static_cast<ClockFormat>(index);
        settings.topBar.dateStyle = static_cast<DateStyle>(index);
        settings.weather.unit = index % 2 ? TemperatureUnit::Fahrenheit : TemperatureUnit::Celsius;
        settings.updates.channel = index % 2 ? UpdateChannel::Prerelease : UpdateChannel::Stable;
        auto serialized = ToJson(settings);
        REQUIRE(serialized.ok());
        auto parsed = FromJson(*serialized);
        REQUIRE(parsed.ok());
        CHECK_EQ(parsed->value.general.motion, settings.general.motion);
        CHECK_EQ(parsed->value.topBar.clockFormat, settings.topBar.clockFormat);
        CHECK_EQ(parsed->value.topBar.dateStyle, settings.topBar.dateStyle);
        CHECK_EQ(parsed->value.weather.unit, settings.weather.unit);
        CHECK_EQ(parsed->value.updates.channel, settings.updates.channel);
        CHECK(parsed->warnings.empty());
    }
}

AETHER_TEST("settings validation: all numeric ranges and non-finite values are corrected") {
    Settings settings;
    settings.dock.iconSize = -1; settings.dock.bottomMargin = 999; settings.dock.sideMargin = -1;
    settings.dock.iconSpacing = 999; settings.dock.opacity = 0.01; settings.dock.magnification = 10;
    settings.topBar.topMargin = 999; settings.topBar.opacity = std::numeric_limits<double>::quiet_NaN();
    settings.weather.refreshMinutes = 1; settings.updates.checkIntervalHours = 999;
    auto warnings = Validate(settings);
    REQUIRE(warnings.ok()); CHECK_EQ(warnings->size(), 10u);
    CHECK_EQ(settings.dock.iconSize, 32); CHECK_EQ(settings.dock.bottomMargin, 200);
    CHECK_EQ(settings.dock.sideMargin, 0); CHECK_EQ(settings.dock.iconSpacing, 32);
    CHECK_EQ(settings.dock.opacity, 0.30); CHECK_EQ(settings.dock.magnification, 2.0);
    CHECK_EQ(settings.topBar.topMargin, 100); CHECK_EQ(settings.topBar.opacity, 0.80);
    CHECK_EQ(settings.weather.refreshMinutes, 15); CHECK_EQ(settings.updates.checkIntervalHours, 168);
}

AETHER_TEST("settings validation: coordinate pairs stay optional and are clamped together") {
    Settings settings;
    settings.weather.latitude = 120; settings.weather.longitude = -250;
    REQUIRE(Validate(settings).ok());
    CHECK_EQ(settings.weather.latitude, std::optional<double>(90));
    CHECK_EQ(settings.weather.longitude, std::optional<double>(-180));
    settings.weather.longitude.reset();
    REQUIRE(Validate(settings).ok());
    CHECK_FALSE(settings.weather.latitude.has_value());
    CHECK_FALSE(settings.weather.longitude.has_value());
    settings.weather.latitude = std::numeric_limits<double>::infinity(); settings.weather.longitude = 12;
    REQUIRE(Validate(settings).ok());
    CHECK_FALSE(settings.weather.latitude.has_value());
}

AETHER_TEST("settings serialization: huge integer settings clamp and bounded strings retain valid UTF-8") {
    auto input = json::Parse(R"({"dock":{"iconSize":1e100,"bottomMargin":-1e100}})"); REQUIRE(input.ok());
    auto parsed = FromJson(*input); REQUIRE(parsed.ok());
    CHECK_EQ(parsed->value.dock.iconSize, 96); CHECK_EQ(parsed->value.dock.bottomMargin, 0);
    Settings settings;
    settings.updates.skippedVersion.assign(127, 'a'); settings.updates.skippedVersion += "\xC3\xA9";
    REQUIRE(Validate(settings).ok());
    auto document = ToJson(settings); REQUIRE(document.ok());
    CHECK(json::Parse(json::Serialize(*document)).ok());
}

AETHER_TEST("settings serialization: wrong types and unknown members warn without echoing user text") {
    auto root = json::Parse(R"({"dock":{"iconSize":"bad","reserveSpace":42,"private-synthetic-key":1},"topBar":[],"weather":{"enabled":"true","unit":"kelvin"},"general":{"motion":"invalid","logLevel":"secret-synthetic-value"},"other":{}})");
    REQUIRE(root.ok());
    auto parsed = FromJson(*root);
    REQUIRE(parsed.ok());
    CHECK(parsed->warnings.size() >= 8u);
    CHECK_EQ(parsed->value.dock.iconSize, 40);
    CHECK_FALSE(parsed->value.weather.enabled);
    CHECK_EQ(parsed->value.general.logLevel, std::string("info"));
    for (const auto& warning : parsed->warnings) {
        CHECK(warning.find("private-synthetic") == std::string::npos);
        CHECK(warning.find("secret-synthetic") == std::string::npos);
    }
    CHECK_FALSE(FromJson(json::Value::MakeArray()).ok());
}

AETHER_TEST("settings validation: pins deduplicate validate identity cap and bound strings") {
    Settings settings;
    settings.dock.pinned.push_back({});
    for (int i = 0; i < 70; ++i) {
        PinnedApp pin;
        pin.id = L"exe:synthetic-" + std::to_wstring(i);
        pin.displayName.assign(2048, L'x');
        settings.dock.pinned.push_back(pin);
        settings.dock.pinned.push_back(std::move(pin));
    }
    auto warnings = Validate(settings);
    REQUIRE(warnings.ok()); CHECK_FALSE(warnings->empty());
    REQUIRE_EQ(settings.dock.pinned.size(), 64u);
    CHECK_EQ(settings.dock.pinned[0].id, std::wstring(L"exe:synthetic-0"));
    CHECK_EQ(settings.dock.pinned[63].id, std::wstring(L"exe:synthetic-63"));
    CHECK_EQ(settings.dock.pinned[0].displayName.size(), 1024u);
    auto document = ToJson(settings);
    REQUIRE(document.ok());
    auto parsed = FromJson(*document);
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed->value.dock.pinned.size(), 64u);
    CHECK_EQ(parsed->value.dock.pinned.back().id, settings.dock.pinned.back().id);
}

AETHER_TEST("settings serialization: invalid pin entries do not consume valid pin slots") {
    auto root = json::Value::MakeObject();
    auto dock = json::Value::MakeObject();
    auto pins = json::Value::MakeArray();
    for (int i = 0; i < 100; ++i) pins.Append(json::Value::MakeObject());
    auto pin = json::Value::MakeObject(); pin.Set("id", "exe:valid");
    pins.Append(pin); pins.Append(pin);
    dock.Set("pinned", pins); root.Set("dock", dock);
    auto parsed = FromJson(root);
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed->value.dock.pinned.size(), 1u);
    CHECK_EQ(parsed->value.dock.pinned[0].id, std::wstring(L"exe:valid"));
}

AETHER_TEST("settings validation: custom chords round trip and invalid or duplicate chords recover") {
    Settings settings;
    settings.general.searchHotkey = {6, 0x46};
    settings.general.focusDockHotkey = {9, 0x47};
    auto document = ToJson(settings); REQUIRE(document.ok());
    auto parsed = FromJson(*document); REQUIRE(parsed.ok());
    CHECK_EQ(parsed->value.general.searchHotkey, settings.general.searchHotkey);
    CHECK_EQ(parsed->value.general.focusDockHotkey, settings.general.focusDockHotkey);
    settings.general.searchHotkey = {0xFFFF, 0x11};
    settings.general.focusDockHotkey = {0, 0x44};
    REQUIRE(Validate(settings).ok());
    CHECK_EQ(settings.general.searchHotkey, (HotkeyChord{3, 0x20}));
    CHECK_EQ(settings.general.focusDockHotkey, (HotkeyChord{3, 0x44}));
    settings.general.searchHotkey = settings.general.focusDockHotkey;
    REQUIRE(Validate(settings).ok());
    CHECK_FALSE(settings.general.searchHotkey == settings.general.focusDockHotkey);
}

AETHER_TEST("settings migration: missing schema becomes v1 in memory and newer unknown data survives") {
    auto input = json::Parse(R"({"dock":{"iconSize":64}})"); REQUIRE(input.ok());
    auto migrated = MigrateConfiguration(*input); REQUIRE(migrated.ok());
    CHECK_EQ(migrated->sourceVersion, 0); CHECK_FALSE(migrated->newerSchema);
    CHECK_EQ(migrated->document.GetInt64("schemaVersion", 0), 1);
    auto newer = json::Parse(R"({"schemaVersion":8,"future":{"synthetic":true}})"); REQUIRE(newer.ok());
    auto protectedFile = MigrateConfiguration(*newer); REQUIRE(protectedFile.ok());
    CHECK(protectedFile->newerSchema); CHECK_EQ(protectedFile->document, *newer);
}

AETHER_TEST("settings store: missing file uses defaults without creating directories or files") {
    auto file = NewFile().parent_path() / L"not-created" / L"settings.json";
    auto store = MakeFileSettingsStore(file); REQUIRE(store.ok());
    auto loaded = (*store)->Load(); REQUIRE(loaded.ok());
    CHECK_EQ(loaded->report.status, LoadStatus::Defaults);
    CHECK_FALSE(std::filesystem::exists(file.parent_path()));
}

AETHER_TEST("settings migration: large future schema versions remain protected and unrepresentable ones fail") {
    auto future = json::Parse(R"({"schemaVersion":4000000000})"); REQUIRE(future.ok());
    auto migrated = MigrateConfiguration(*future); REQUIRE(migrated.ok());
    CHECK(migrated->newerSchema); CHECK_EQ(migrated->sourceVersion, 4'000'000'000LL);
    auto unbounded = json::Parse(R"({"schemaVersion":1e100})"); REQUIRE(unbounded.ok());
    CHECK_FALSE(MigrateConfiguration(*unbounded).ok());
}

AETHER_TEST("settings store: atomic save leaves valid JSON and no temporary siblings") {
    const auto file = NewFile();
    auto store = MakeFileSettingsStore(file); REQUIRE(store.ok()); REQUIRE((*store)->Load().ok());
    Settings settings; settings.dock.iconSize = 72;
    REQUIRE((*store)->Save(settings, false).ok());
    auto bytes = fs::ReadFile(file, 1024 * 1024); REQUIRE(bytes.ok());
    auto document = json::Parse(*bytes); REQUIRE(document.ok());
    CHECK_EQ(document->Find("dock")->GetInt64("iconSize", 0), 72);
    CHECK(Matching(file.parent_path(), L".tmp-").empty());
}

AETHER_TEST("settings store: corrupt inputs are preserved with at most three backups") {
    const auto file = NewFile();
    for (int i = 0; i < 5; ++i) {
        REQUIRE(fs::WriteFileAtomic(file, "corrupt-" + std::to_string(i)).ok());
        auto store = MakeFileSettingsStore(file); REQUIRE(store.ok());
        auto loaded = (*store)->Load(); REQUIRE(loaded.ok());
        CHECK_EQ(loaded->report.status, LoadStatus::RecoveredFromCorruption);
        CHECK_FALSE(std::filesystem::exists(file));
        CHECK(Matching(file.parent_path(), L".corrupt-").size() <= 3u);
    }
    CHECK_EQ(Matching(file.parent_path(), L".corrupt-").size(), 3u);
}

AETHER_TEST("settings store: oversized data is quarantined without truncating it") {
    const auto file = NewFile();
    const std::string original(1024 * 1024 + 1, 'x');
    REQUIRE(fs::WriteFileAtomic(file, original).ok());
    auto store = MakeFileSettingsStore(file); REQUIRE(store.ok());
    auto loaded = (*store)->Load(); REQUIRE(loaded.ok());
    CHECK_EQ(loaded->report.status, LoadStatus::RecoveredFromCorruption);
    const auto backups = Matching(file.parent_path(), L".corrupt-"); REQUIRE_EQ(backups.size(), 1u);
    CHECK_EQ(*fs::ReadFile(backups[0], 2 * 1024 * 1024), original);
}

AETHER_TEST("settings store: newer schema is untouched until update and backed up exactly") {
    const auto file = NewFile();
    const std::string original = R"({"schemaVersion":7,"dock":{"iconSize":80},"future":{"value":"synthetic"}})";
    REQUIRE(fs::WriteFileAtomic(file, original).ok());
    auto store = MakeFileSettingsStore(file); REQUIRE(store.ok());
    auto loaded = (*store)->Load(); REQUIRE(loaded.ok());
    CHECK_EQ(loaded->report.status, LoadStatus::NewerSchema);
    CHECK_EQ(*fs::ReadFile(file, 1024 * 1024), original);
    CHECK_FALSE(std::filesystem::exists(file.parent_path() / L"settings.v7.backup.json"));
    REQUIRE((*store)->Save(loaded->value, false).ok());
    CHECK_EQ(*fs::ReadFile(file.parent_path() / L"settings.v7.backup.json", 1024 * 1024), original);
    auto saved = json::Parse(*fs::ReadFile(file, 1024 * 1024)); REQUIRE(saved.ok());
    CHECK_EQ(saved->GetInt64("schemaVersion", 0), 1);
}

AETHER_TEST("settings store: replacing a newer-schema backup first preserves the previous backup") {
    const auto file = NewFile();
    const std::string previous = R"({"schemaVersion":7,"future":1})";
    const std::string current = R"({"schemaVersion":7,"future":2})";
    REQUIRE(fs::WriteFileAtomic(file.parent_path() / L"settings.v7.backup.json", previous).ok());
    REQUIRE(fs::WriteFileAtomic(file, current).ok());
    auto store = MakeFileSettingsStore(file); REQUIRE(store.ok()); REQUIRE((*store)->Load().ok());
    REQUIRE((*store)->Save(Settings{}, false).ok());
    const auto extra = Matching(file.parent_path(), L".backup-"); REQUIRE_EQ(extra.size(), 1u);
    CHECK_EQ(*fs::ReadFile(extra.front(), 1024 * 1024), previous);
    CHECK_EQ(*fs::ReadFile(file.parent_path() / L"settings.v7.backup.json", 1024 * 1024), current);
}

AETHER_TEST("settings store: reset backs up original bytes before replacing settings") {
    const auto file = NewFile(); const std::string original = R"({"dock":{"iconSize":96}})";
    REQUIRE(fs::WriteFileAtomic(file, original).ok());
    auto store = MakeFileSettingsStore(file); REQUIRE(store.ok()); REQUIRE((*store)->Load().ok());
    REQUIRE((*store)->Save(Settings{}, true).ok());
    CHECK_EQ(*fs::ReadFile(std::filesystem::path(file.wstring() + L".bak"), 1024 * 1024), original);
}

AETHER_TEST("settings store: a directory at the file path is never renamed or overwritten") {
    const auto file = NewFile(); std::filesystem::create_directory(file);
    REQUIRE(fs::WriteFileAtomic(file / L"keep.txt", "synthetic-preserved").ok());
    auto store = MakeFileSettingsStore(file); REQUIRE(store.ok());
    CHECK_FALSE((*store)->Load().ok()); CHECK_FALSE((*store)->Save(Settings{}, false).ok());
    CHECK(std::filesystem::is_directory(file));
    CHECK_EQ(*fs::ReadFile(file / L"keep.txt", 1024), std::string("synthetic-preserved"));
}

AETHER_TEST("settings store: serialized output beyond load limit never replaces the working file") {
    const auto file = NewFile();
    const std::string original = R"({"schemaVersion":1})";
    REQUIRE(fs::WriteFileAtomic(file, original).ok());
    auto store = MakeFileSettingsStore(file); REQUIRE(store.ok()); REQUIRE((*store)->Load().ok());
    Settings large;
    for (int i = 0; i < 64; ++i) {
        PinnedApp pin;
        pin.id = std::to_wstring(i) + std::wstring(1000, L'\u65e5');
        pin.displayName = pin.parsingName = pin.appUserModelId = pin.executablePath = pin.shortcutPath = std::wstring(1024, L'\u65e5');
        large.dock.pinned.push_back(std::move(pin));
    }
    const auto saved = (*store)->Save(large, false);
    REQUIRE_FALSE(saved.ok()); CHECK_EQ(saved.error().code, ErrorCode::Integrity);
    CHECK_EQ(*fs::ReadFile(file, 1024 * 1024), original);
}

AETHER_TEST("settings service: disk work stays on worker and notifications completions stay on UI") {
    ManualDispatcher dispatcher;
    auto store = std::make_shared<MemoryStore>(); store->initial.dock.iconSize = 64;
    int changes = 0; bool completionOnUi = false; bool saved = false;
    SettingsService service(store, dispatcher);
    auto subscription = service.Changed().Connect([&](const Settings&) { CHECK(dispatcher.IsUiThread()); ++changes; });
    Load(service, dispatcher);
    CHECK_EQ(service.Current().dock.iconSize, 64); CHECK_EQ(changes, 1);
    auto updated = service.Update([](Settings& s) { s.dock.iconSize = 80; }, [&](Result<void> result) {
        CHECK(result.ok()); completionOnUi = dispatcher.IsUiThread(); saved = true;
    });
    REQUIRE(updated.ok()); CHECK_EQ(changes, 2); CHECK_EQ(service.Current().dock.iconSize, 80);
    REQUIRE(Pump(dispatcher, [&] { return saved; }));
    CHECK(completionOnUi); CHECK_FALSE(store->touchedUi.load()); CHECK_FALSE(service.Dirty());
}

AETHER_TEST("settings service: an older save cannot mark a newer pending revision clean") {
    ManualDispatcher dispatcher; auto store = std::make_shared<MemoryStore>(); store->blockSave = 2;
    bool firstDone = false; bool secondDone = false;
    SettingsService service(store, dispatcher); Load(service, dispatcher);
    REQUIRE(service.Update([](Settings& s) { s.dock.iconSize = 64; }, [&](Result<void>) { firstDone = true; }).ok());
    REQUIRE(service.Update([](Settings& s) { s.dock.iconSize = 80; }, [&](Result<void>) { secondDone = true; }).ok());
    const bool secondReached = store->WaitSaves(2);
    const bool firstDelivered = Pump(dispatcher, [&] { return firstDone; });
    const bool dirtyBeforeSecond = service.Dirty();
    store->Release();
    REQUIRE(secondReached); REQUIRE(firstDelivered); CHECK(dirtyBeforeSecond);
    REQUIRE(Pump(dispatcher, [&] { return secondDone; }));
    CHECK_FALSE(service.Dirty());
    REQUIRE_EQ(store->saved.size(), 2u);
    CHECK_EQ(store->saved[0].dock.iconSize, 64); CHECK_EQ(store->saved[1].dock.iconSize, 80);
}

AETHER_TEST("settings service: failed writes keep model dirty flush reports failure and next update retries") {
    ManualDispatcher dispatcher; auto store = std::make_shared<MemoryStore>(); store->fail = true;
    bool failed = false; bool flushed = false; bool retried = false;
    SettingsService service(store, dispatcher); Load(service, dispatcher);
    REQUIRE(service.Update([](Settings& s) { s.dock.iconSize = 80; }, [&](Result<void> result) {
        failed = !result && result.error().code == ErrorCode::AccessDenied;
    }).ok());
    REQUIRE(Pump(dispatcher, [&] { return failed; }));
    CHECK_EQ(service.Current().dock.iconSize, 80); CHECK(service.Dirty());
    REQUIRE(service.FlushAsync([&](Result<void> result) { flushed = !result; }).ok());
    REQUIRE(Pump(dispatcher, [&] { return flushed; }));
    { std::lock_guard lock(store->mutex); store->fail = false; }
    REQUIRE(service.Update([](Settings&) {}, [&](Result<void> result) { retried = result.ok(); }).ok());
    REQUIRE(Pump(dispatcher, [&] { return retried; }));
    CHECK_FALSE(service.Dirty()); CHECK_EQ(store->saved.back().dock.iconSize, 80);
}

AETHER_TEST("settings service: shutdown gate rejects late mutations preserves accepted saves and can reopen") {
    ManualDispatcher dispatcher; auto store = std::make_shared<MemoryStore>(); store->blockSave = 1;
    SettingsService service(store, dispatcher); Load(service, dispatcher);
    int changes = 0; bool firstSaved = false; bool flushed = false; bool secondSaved = false;
    bool rejectedMutatorCalled = false; bool rejectedCompletionCalled = false;
    auto subscription = service.Changed().Connect([&](const Settings&) { ++changes; });
    REQUIRE(service.Update([](Settings& s) { s.dock.iconSize = 64; }, [&](Result<void> result) { firstSaved = result.ok(); }).ok());
    const bool saveInFlight = store->WaitSaves(1);
    REQUIRE(service.SetChangesEnabled(false).ok());
    const auto late = service.Update([&](Settings& s) { rejectedMutatorCalled = true; s.dock.iconSize = 96; },
        [&](Result<void>) { rejectedCompletionCalled = true; });
    const auto reset = service.ResetToDefaults([&](Result<void>) { rejectedCompletionCalled = true; });
    CHECK_FALSE(late.ok()); CHECK_FALSE(reset.ok());
    if (!late) CHECK_EQ(late.error().code, ErrorCode::InvalidState);
    if (!reset) CHECK_EQ(reset.error().code, ErrorCode::InvalidState);
    CHECK_FALSE(rejectedMutatorCalled); CHECK_EQ(service.Current().dock.iconSize, 64); CHECK_EQ(changes, 1);
    REQUIRE(service.FlushAsync([&](Result<void> result) { flushed = result.ok(); }).ok());
    dispatcher.RunPending(); CHECK_FALSE(firstSaved); CHECK_FALSE(flushed); CHECK(service.Dirty());
    store->Release(); REQUIRE(saveInFlight);
    REQUIRE(Pump(dispatcher, [&] { return flushed; }));
    CHECK(firstSaved); CHECK_FALSE(rejectedCompletionCalled); CHECK_FALSE(service.Dirty());
    REQUIRE_EQ(store->saved.size(), 1u); CHECK_EQ(store->saved[0].dock.iconSize, 64); CHECK_FALSE(store->resetFlags[0]);
    REQUIRE(service.SetChangesEnabled(true).ok());
    REQUIRE(service.Update([](Settings& s) { s.dock.iconSize = 80; }, [&](Result<void> result) { secondSaved = result.ok(); }).ok());
    REQUIRE(Pump(dispatcher, [&] { return secondSaved; }));
    CHECK_EQ(changes, 2); REQUIRE_EQ(store->saved.size(), 2u); CHECK_EQ(store->saved[1].dock.iconSize, 80);
}

AETHER_TEST("settings service: throwing mutators preserve model and do not queue a save") {
    ManualDispatcher dispatcher; auto store = std::make_shared<MemoryStore>();
    SettingsService service(store, dispatcher); Load(service, dispatcher);
    const auto result = service.Update([](Settings& s) { s.dock.iconSize = 80; throw std::runtime_error("synthetic-private-data"); });
    REQUIRE_FALSE(result.ok()); CHECK_EQ(result.error().code, ErrorCode::Internal);
    CHECK_EQ(service.Current().dock.iconSize, 40); CHECK_FALSE(service.Dirty()); CHECK(store->saved.empty());
    CHECK(result.error().message.find("synthetic-private") == std::string::npos);
}

AETHER_TEST("settings service: failed load blocks unsafe updates and can be retried") {
    ManualDispatcher dispatcher; auto store = std::make_shared<MemoryStore>(); store->failLoad = true;
    bool failed = false; SettingsService service(store, dispatcher);
    REQUIRE(service.Load([&](Result<LoadReport> result) { failed = !result; }).ok());
    REQUIRE(Pump(dispatcher, [&] { return failed; }));
    CHECK_FALSE(service.Loaded());
    CHECK_EQ(service.Update([](Settings&) {}).error().code, ErrorCode::InvalidState);
    store->failLoad = false;
    Load(service, dispatcher); CHECK(service.Loaded());
}

AETHER_TEST("settings service: destruction suppresses dispatcher callbacks already queued") {
    ManualDispatcher dispatcher; auto store = std::make_shared<MemoryStore>(); bool called = false;
    {
        SettingsService service(store, dispatcher);
        REQUIRE(service.Load([&](Result<LoadReport>) { called = true; }).ok());
        const auto end = std::chrono::steady_clock::now() + 5s;
        while (!dispatcher.PendingCount() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
        REQUIRE(dispatcher.PendingCount() > 0);
    }
    dispatcher.RunPending(); CHECK_FALSE(called);
}

AETHER_TEST("settings service: operations reject wrong threads and updates before load completion") {
    ManualDispatcher dispatcher; auto store = std::make_shared<MemoryStore>();
    SettingsService service(store, dispatcher);
    CHECK_EQ(service.Update([](Settings&) {}).error().code, ErrorCode::InvalidState);
    std::optional<ErrorCode> code, gateCode;
    std::thread other([&] {
        code = service.Update([](Settings&) {}).error().code;
        gateCode = service.SetChangesEnabled(false).error().code;
    }); other.join();
    CHECK_EQ(code, std::optional<ErrorCode>(ErrorCode::InvalidState));
    CHECK_EQ(gateCode, std::optional<ErrorCode>(ErrorCode::InvalidState));
    Load(service, dispatcher);
    CHECK_EQ(service.Load({}).error().code, ErrorCode::InvalidState);
}

AETHER_TEST("settings service: reset applies defaults immediately and queues a backup save") {
    ManualDispatcher dispatcher; auto store = std::make_shared<MemoryStore>(); store->initial.dock.iconSize = 96;
    bool saved = false; SettingsService service(store, dispatcher); Load(service, dispatcher);
    REQUIRE(service.ResetToDefaults([&](Result<void> result) { saved = result.ok(); }).ok());
    CHECK_EQ(service.Current().dock.iconSize, 40);
    REQUIRE(Pump(dispatcher, [&] { return saved; }));
    REQUIRE_EQ(store->resetFlags.size(), 1u); CHECK(store->resetFlags[0]);
}

AETHER_TEST("settings startup: injected registry supports quoted paths enable inspect and disable") {
    const auto file = NewFile().parent_path() / L"launcher with spaces.exe";
    REQUIRE(fs::WriteFileAtomic(file, "synthetic executable fixture; never run").ok());
    auto registry = std::make_shared<MemoryRegistry>(); StartupRegistration startup(registry);
    CHECK_EQ(*startup.IsEnabled(file), StartupState::Disabled);
    REQUIRE(startup.Enable(file).ok());
    CHECK_EQ(*registry->run, L"\"" + file.wstring() + L"\" --autostart");
    CHECK_EQ(*startup.IsEnabled(file), StartupState::Enabled);
    REQUIRE(startup.Disable().ok()); CHECK_FALSE(registry->run.has_value()); CHECK_EQ(registry->writes, 1);
    registry->run = L"different-target"; CHECK_EQ(*startup.IsEnabled(file), StartupState::DifferentTarget);
}

AETHER_TEST("settings startup: a Task Manager disable is reported and never overwritten") {
    const auto file = NewFile().parent_path() / L"launcher.exe"; REQUIRE(fs::WriteFileAtomic(file, "fixture").ok());
    auto registry = std::make_shared<MemoryRegistry>(); registry->approval = {3, 0, 0, 0};
    registry->run = L"preserve-synthetic-command"; StartupRegistration startup(registry);
    CHECK_EQ(*startup.IsEnabled(file), StartupState::DisabledByUser);
    CHECK_EQ(startup.Enable(file).error().code, ErrorCode::AccessDenied);
    CHECK_EQ(registry->writes, 0); CHECK_EQ(*registry->run, std::wstring(L"preserve-synthetic-command"));
    REQUIRE(startup.Disable().ok()); CHECK_EQ(registry->approval[0], 3);
}

AETHER_TEST("settings startup: unsupported approval formats and adapter exceptions fail safely") {
    const auto file = NewFile().parent_path() / L"launcher.exe"; REQUIRE(fs::WriteFileAtomic(file, "fixture").ok());
    auto registry = std::make_shared<MemoryRegistry>(); StartupRegistration startup(registry);
    registry->approval = {255}; CHECK_EQ(*startup.IsEnabled(file), StartupState::UnknownApproval);
    CHECK_EQ(startup.Enable(file).error().code, ErrorCode::Unsupported); CHECK_EQ(registry->writes, 0);
    registry->throwRead = true; auto failed = startup.Enable(file); REQUIRE_FALSE(failed.ok());
    CHECK_EQ(failed.error().code, ErrorCode::Internal);
    CHECK(failed.error().message.find("synthetic-private") == std::string::npos);
}

AETHER_TEST("settings startup: invalid or missing launcher paths never write registration") {
    auto registry = std::make_shared<MemoryRegistry>(); StartupRegistration startup(registry);
    CHECK_EQ(startup.Enable(L"relative.exe").error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(startup.Enable(NewFile().parent_path() / L"missing.exe").error().code, ErrorCode::NotFound);
    CHECK_EQ(startup.Enable(NewFile()).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(registry->writes, 0);
    CHECK_FALSE(MakeWindowsStartupRegistry({L"", L""}).ok());
    // Constructing the adapter with injectable paths performs no registry access.
    CHECK(MakeWindowsStartupRegistry({L"Software\\AetherShellTests\\Run", L"Software\\AetherShellTests\\Approval"}).ok());
}

AETHER_TEST("weather location settings: preserve legacy manual coordinates and disabled consent") {
    auto root=json::Parse(R"({"schemaVersion":1,"weather":{"enabled":false,"latitude":10,"longitude":20}})");REQUIRE(root.ok());
    auto parsed=FromJson(*root);REQUIRE(parsed.ok());
    CHECK_FALSE(parsed->value.weather.enabled);CHECK_FALSE(parsed->value.weather.useWindowsLocation);
    CHECK_FALSE(parsed->value.weather.windowsLocationConsent);
    Settings automatic;automatic.weather.enabled=true;automatic.weather.useWindowsLocation=true;automatic.weather.windowsLocationConsent=true;
    auto encoded=ToJson(automatic);REQUIRE(encoded.ok());auto roundtrip=FromJson(*encoded);REQUIRE(roundtrip.ok());
    CHECK(roundtrip->value.weather.useWindowsLocation);CHECK(roundtrip->value.weather.windowsLocationConsent);
    CHECK_FALSE(roundtrip->value.weather.latitude.has_value());
}
