// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Dispatcher.h"
#include "core/Result.h"
#include "core/Signal.h"
#include "settings/Settings.h"
#include "settings/SettingsStore.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aether::settings {

// UI-owned model; every file operation runs on the private FIFO worker. dispatcher must outlive
// the service. Load must finish before Update. Changed fires immediately for validated model
// changes; completions fire on the UI thread after persistence. A failed save leaves Dirty true.
class SettingsService {
public:
    using Completion = std::function<void(Result<void>)>;
    using LoadCompletion = std::function<void(Result<LoadReport>)>;
    using Mutator = std::function<void(Settings&)>;

    SettingsService(std::filesystem::path path, IUiDispatcher& dispatcher);
    SettingsService(std::shared_ptr<ISettingsStore> store, IUiDispatcher& dispatcher);
    ~SettingsService();
    SettingsService(const SettingsService&) = delete;
    SettingsService& operator=(const SettingsService&) = delete;

    Result<void> Load(LoadCompletion completion);
    [[nodiscard]] const Settings& Current() const;
    [[nodiscard]] bool Loaded() const;
    [[nodiscard]] bool Dirty() const;
    [[nodiscard]] bool NewerSchemaProtected() const;
    [[nodiscard]] Signal<const Settings&>& Changed();
    // UI-owned shutdown gate, enabled by default. Disable after pending editor/consent work settles
    // and before FlushAsync: new Update/Reset calls fail with InvalidState, while accepted saves
    // and flush barriers continue. Re-enable if shutdown is cancelled; this does not change JSON.
    Result<void> SetChangesEnabled(bool enabled);
    Result<std::vector<std::string>> Update(Mutator mutator, Completion completion = {});
    // Applies Settings{} except general.replaceWindowsTaskbar, which keeps its current value (D23):
    // a reset changes customization, never desktop ownership. Everything else is reset, including
    // the pin-import markers, so an emptied Dock imports the Windows taskbar pins once more.
    Result<std::vector<std::string>> ResetToDefaults(Completion completion = {});
    // FIFO barrier. Use before app shutdown, then destroy the service on the UI thread. Fails when
    // an earlier save failed; does not manufacture durability by silently retrying it.
    Result<void> FlushAsync(Completion completion);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace aether::settings
