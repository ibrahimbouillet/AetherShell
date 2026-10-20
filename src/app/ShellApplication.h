// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "app/CommandLine.h"
#include "core/Result.h"
#include "platform/windows/AppPaths.h"
#include <memory>
#include <string>

namespace aether::app {
class ShellApplication {
public:
    ShellApplication();
    ~ShellApplication();
    // A separate instance name isolates own-window lifecycle tests from any running installation.
    // Taskbar replacement is on by default (D23), so the real Windows taskbar is reachable only when
    // `nativeTaskbarAccess` is true AND the instance name is the installed "AetherShell"
    // (DesktopPolicy.h). Any other session never creates the taskbar controller, never puts the
    // Dock in replacement mode and refuses --restore-taskbar recovery without an instance, whatever
    // its settings ask for; tests also pass false explicitly.
    Result<int> Run(CommandLine command, platform::AppPaths paths, std::wstring instanceName = L"AetherShell",
                    bool nativeTaskbarAccess = true);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace aether::app
