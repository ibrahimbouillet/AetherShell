// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// D21: `--restore-taskbar` without a running instance. A simulated taskbar only; the real Windows
// taskbar is never read or changed.
#include "AetherTest.h"
#include "app/TaskbarRecovery.h"
#include "core/FileSystem.h"
#include "settings/SettingsStore.h"

#include <shellapi.h>

#include <memory>
#include <vector>

using namespace aether;
using namespace aether::app;

namespace {
class SimulatedTaskbar final : public platform::ITaskbarBackend {
public:
    unsigned state = ABS_ALWAYSONTOP | ABS_AUTOHIDE;
    std::vector<platform::TaskbarWindowState> windows{{reinterpret_cast<HWND>(std::uintptr_t{31}), false}};
    Result<unsigned> State() override { return state; }
    Result<void> SetState(unsigned value) override { state = value; return Ok(); }
    Result<std::vector<platform::TaskbarWindowState>> Windows() override { return windows; }
    Result<void> SetVisible(HWND window, bool visible) override {
        for (auto& item : windows) if (item.window == window) item.visible = visible;
        return Ok();
    }
};

class Borrowed final : public platform::ITaskbarBackend {
public:
    explicit Borrowed(platform::ITaskbarBackend& inner) : inner_(inner) {}
    Result<unsigned> State() override { return inner_.State(); }
    Result<void> SetState(unsigned value) override { return inner_.SetState(value); }
    Result<std::vector<platform::TaskbarWindowState>> Windows() override { return inner_.Windows(); }
    Result<void> SetVisible(HWND window, bool visible) override { return inner_.SetVisible(window, visible); }
private:
    platform::ITaskbarBackend& inner_;
};

platform::TaskbarBackendFactory Lend(SimulatedTaskbar& taskbar) {
    return [&taskbar]() -> Result<std::unique_ptr<platform::ITaskbarBackend>> {
        return std::unique_ptr<platform::ITaskbarBackend>(std::make_unique<Borrowed>(taskbar));
    };
}

void SaveReplacementSetting(const std::filesystem::path& config, bool replace) {
    REQUIRE(fs::EnsureDirectory(config).ok());  // AppPaths::Resolve creates it in the application
    auto store = settings::MakeFileSettingsStore(config / L"settings.json");
    REQUIRE(store.ok());
    auto loaded = store.value()->Load();  // the store saves only after a safe load
    REQUIRE(loaded.ok());
    loaded->value.general.replaceWindowsTaskbar = replace;
    REQUIRE(store.value()->Save(loaded->value, false).ok());
}

bool ReplacementSetting(const std::filesystem::path& config) {
    auto store = settings::MakeFileSettingsStore(config / L"settings.json");
    REQUIRE(store.ok());
    auto loaded = store.value()->Load();
    REQUIRE(loaded.ok());
    return loaded->value.general.replaceWindowsTaskbar;
}
}  // namespace

AETHER_TEST("taskbar recovery without an instance: restores the baseline and turns replacement off") {
    const auto config = test::MakeTempDirectory("app-taskbar-recovery") / L"config";
    SaveReplacementSetting(config, true);
    SimulatedTaskbar taskbar;
    REQUIRE(platform::SaveTaskbarBaseline(platform::TaskbarBaselineFile(config),
        platform::TaskbarSnapshot{ABS_ALWAYSONTOP, {{taskbar.windows[0].window, true}}}).ok());

    auto recovered = RecoverTaskbarWithoutInstance(config, Lend(taskbar));
    REQUIRE(recovered.ok());
    CHECK(recovered->outcome == platform::TaskbarRecovery::RestoredFromBaseline);
    CHECK(recovered->replacementTurnedOff);
    CHECK_EQ(taskbar.state, unsigned(ABS_ALWAYSONTOP));
    CHECK(taskbar.windows[0].visible);
    CHECK_FALSE(ReplacementSetting(config));
    CHECK_FALSE(std::filesystem::exists(platform::TaskbarBaselineFile(config)));
    CHECK(DescribeTaskbarRecovery(recovered).find(L"restored") != std::wstring::npos);
}

AETHER_TEST("taskbar recovery without an instance: no baseline and no settings writes replacement off") {
    // Behaviour change (D23): the default replaces the taskbar, so the missing file is created with
    // replacement off; otherwise the next normal start would hide the taskbar again.
    const auto config = test::MakeTempDirectory("app-taskbar-recovery-empty") / L"config";
    SimulatedTaskbar taskbar;

    auto recovered = RecoverTaskbarWithoutInstance(config, Lend(taskbar));
    REQUIRE(recovered.ok());
    CHECK(recovered->outcome == platform::TaskbarRecovery::ShownWithoutBaseline);
    CHECK(recovered->replacementTurnedOff);
    CHECK(taskbar.windows[0].visible);
    CHECK_EQ(taskbar.state, unsigned(ABS_ALWAYSONTOP | ABS_AUTOHIDE));
    REQUIRE(std::filesystem::exists(config / L"settings.json"));
    CHECK_FALSE(ReplacementSetting(config));
    CHECK(DescribeTaskbarRecovery(recovered).find(L"is turned off") != std::wstring::npos);
}

AETHER_TEST("taskbar recovery without an instance: a corrupt settings file is preserved and rewritten off") {
    const auto config = test::MakeTempDirectory("app-taskbar-recovery-corrupt") / L"config";
    REQUIRE(fs::EnsureDirectory(config).ok());
    REQUIRE(fs::WriteFileAtomic(config / L"settings.json", "{ synthetic corrupt settings").ok());
    SimulatedTaskbar taskbar;

    auto recovered = RecoverTaskbarWithoutInstance(config, Lend(taskbar));
    REQUIRE(recovered.ok());
    CHECK(recovered->replacementTurnedOff);
    CHECK_FALSE(ReplacementSetting(config));
    bool preserved = false;
    for (const auto& entry : std::filesystem::directory_iterator(config)) {
        if (entry.path().filename().wstring().starts_with(L"settings.json.corrupt-")) {
            preserved = fs::ReadFile(entry.path(), 1024).value() == "{ synthetic corrupt settings";
        }
    }
    CHECK(preserved);
}

AETHER_TEST("taskbar recovery without an instance: a newer settings schema is never rewritten") {
    const auto config = test::MakeTempDirectory("app-taskbar-recovery-newer") / L"config";
    REQUIRE(fs::EnsureDirectory(config).ok());
    const std::string newer = R"({"schemaVersion":99,"general":{"replaceWindowsTaskbar":true}})";
    REQUIRE(fs::WriteFileAtomic(config / L"settings.json", newer).ok());
    SimulatedTaskbar taskbar;

    auto recovered = RecoverTaskbarWithoutInstance(config, Lend(taskbar));
    REQUIRE(recovered.ok());
    CHECK(taskbar.windows[0].visible);
    CHECK_FALSE(recovered->replacementTurnedOff);
    CHECK_EQ(fs::ReadFile(config / L"settings.json", 1024).value(), newer);
    CHECK(DescribeTaskbarRecovery(recovered).find(L"Settings > Desktop") != std::wstring::npos);
}

AETHER_TEST("taskbar recovery without an instance: an explicit off stays off without a rewrite") {
    const auto config = test::MakeTempDirectory("app-taskbar-recovery-off") / L"config";
    SaveReplacementSetting(config, false);
    const auto before = fs::ReadFile(config / L"settings.json", 1024 * 1024).value();
    SimulatedTaskbar taskbar;

    auto recovered = RecoverTaskbarWithoutInstance(config, Lend(taskbar));
    REQUIRE(recovered.ok());
    CHECK(recovered->replacementTurnedOff);
    CHECK_EQ(fs::ReadFile(config / L"settings.json", 1024 * 1024).value(), before);
}

AETHER_TEST("taskbar recovery without an instance: a failure is reported and settings stay unchanged") {
    const auto config = test::MakeTempDirectory("app-taskbar-recovery-failed") / L"config";
    SaveReplacementSetting(config, true);
    const platform::TaskbarBackendFactory unavailable = []() -> Result<std::unique_ptr<platform::ITaskbarBackend>> {
        return MakeError(ErrorCode::Unavailable, "synthetic missing Explorer");
    };

    auto recovered = RecoverTaskbarWithoutInstance(config, unavailable);
    CHECK_FALSE(recovered.ok());
    CHECK(ReplacementSetting(config));
    CHECK(DescribeTaskbarRecovery(recovered).find(L"Task Manager") != std::wstring::npos);
}
