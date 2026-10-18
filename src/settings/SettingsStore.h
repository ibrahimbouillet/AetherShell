// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "settings/Settings.h"

#include <filesystem>
#include <memory>

namespace aether::settings {

enum class LoadStatus { Loaded, Defaults, RecoveredFromCorruption, NewerSchema };
struct LoadReport {
    LoadStatus status = LoadStatus::Defaults;
    std::vector<std::string> warnings;
};
struct LoadedSettings {
    Settings value;
    LoadReport report;
};

// Blocking persistence seam. SettingsService calls it only on its private worker. Implementations
// must preserve source bytes before overwriting a newer schema or resetting existing settings.
class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;
    virtual Result<LoadedSettings> Load() = 0;
    virtual Result<void> Save(const Settings& settings, bool reset) = 0;
};

// Does not touch disk until Load/Save is called.
Result<std::shared_ptr<ISettingsStore>> MakeFileSettingsStore(std::filesystem::path path);

}  // namespace aether::settings
