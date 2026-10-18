// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aether::app {

enum class StartupAction { None, OpenSettings, OpenSearch, Quit, RestoreTaskbar };

struct CommandLine {
    bool autostart = false;          // launched from the Run key at sign-in
    bool showVersion = false;        // print/show the version and exit
    StartupAction action = StartupAction::None;
    std::optional<std::filesystem::path> dataDirectory;  // --data-dir (development and tests)
    std::wstring healthToken;        // --post-update-check <token> (set by the updater)
};

// Parses the arguments (argv[0] excluded). Unknown arguments are an error so a typo never silently
// changes behaviour; nothing in the command line is ever executed.
[[nodiscard]] Result<CommandLine> ParseCommandLine(const std::vector<std::wstring>& arguments);

// Splits the process command line with CommandLineToArgvW and drops argv[0].
[[nodiscard]] std::vector<std::wstring> ProcessArguments();

}  // namespace aether::app
