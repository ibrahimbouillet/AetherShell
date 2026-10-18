// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "app/CommandLine.h"
#include "core/Result.h"
#include "platform/windows/AppPaths.h"
#include <memory>

namespace aether::app {
class ShellApplication {
public:
    ShellApplication();
    ~ShellApplication();
    // A separate instance name isolates own-window lifecycle tests from any running installation.
    Result<int> Run(CommandLine command, platform::AppPaths paths, std::wstring instanceName = L"AetherShell");
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace aether::app
