// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Json.h"
#include "settings/Settings.h"

namespace aether::settings {

struct ParsedSettings {
    Settings value;
    std::vector<std::string> warnings;
};

Result<std::vector<std::string>> Validate(Settings& settings);
Result<json::Value> ToJson(const Settings& settings);
Result<ParsedSettings> FromJson(const json::Value& value);

}  // namespace aether::settings
