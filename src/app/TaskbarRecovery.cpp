// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/TaskbarRecovery.h"

#include "settings/SettingsStore.h"

namespace aether::app {

Result<TaskbarRecoveryReport> RecoverTaskbarWithoutInstance(const std::filesystem::path& configDirectory,
                                                           const platform::TaskbarBackendFactory& makeBackend) try {
    if (!configDirectory.is_absolute()) return MakeError(ErrorCode::InvalidArgument, "taskbar recovery needs the data folder");
    auto recovered = platform::RecoverTaskbars(platform::TaskbarBaselineFile(configDirectory), true, makeBackend);
    if (!recovered) return recovered.error();
    TaskbarRecoveryReport report;
    report.outcome = *recovered;
    auto store = settings::MakeFileSettingsStore(configDirectory / L"settings.json");
    if (!store) return report;
    auto loaded = store.value()->Load();
    if (!loaded) return report;
    if (loaded->report.status == settings::LoadStatus::Defaults) {
        report.replacementTurnedOff = !loaded->value.general.replaceWindowsTaskbar;
    } else if (loaded->report.status == settings::LoadStatus::Loaded) {
        if (!loaded->value.general.replaceWindowsTaskbar) {
            report.replacementTurnedOff = true;
        } else {
            loaded->value.general.replaceWindowsTaskbar = false;
            report.replacementTurnedOff = store.value()->Save(loaded->value, false).ok();
        }
    }
    return report;
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar recovery failed"); }

std::wstring DescribeTaskbarRecovery(const Result<TaskbarRecoveryReport>& result) {
    if (!result) {
        return L"Aether Shell could not restore the Windows taskbar.\n\n"
               L"Restart Windows Explorer from Task Manager (Ctrl+Shift+Esc), or sign out and sign in again.";
    }
    std::wstring message;
    switch (result->outcome) {
        case platform::TaskbarRecovery::RestoredFromBaseline:
            message = L"The Windows taskbar was restored to its saved state."; break;
        case platform::TaskbarRecovery::ShownWithoutBaseline:
            message = L"No saved taskbar state was found. The Windows taskbar was made visible; "
                      L"its auto-hide setting was not changed."; break;
        case platform::TaskbarRecovery::NothingToRestore:
            message = L"There was nothing to restore."; break;
    }
    message += result->replacementTurnedOff
        ? L"\n\nTaskbar replacement is turned off."
        : L"\n\nTaskbar replacement could not be turned off automatically. Turn it off in "
          L"Aether Shell Settings > Desktop.";
    return message;
}

}  // namespace aether::app
