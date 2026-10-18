// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Json.h"
#include "settings/Settings.h"

namespace aether::settings {

struct MigrationReport {
    json::Value document;
    std::int64_t sourceVersion = 0;
    bool newerSchema = false;
    std::vector<std::string> warnings;
};

Result<MigrationReport> MigrateConfiguration(const json::Value& document);

}  // namespace aether::settings
