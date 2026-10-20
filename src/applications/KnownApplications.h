// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Applications that Windows itself provides and that Aether Shell treats specially.
//
// File Explorer has one stable identity, whatever form it arrives in (an Apps-folder entry, a pinned
// "File Explorer" shortcut without an AppUserModelID, the default pin, or a running window):
//   id "aumid:microsoft.windows.explorer", AppUserModelID and parsing name
//   "Microsoft.Windows.Explorer", executable "<system Windows directory>\explorer.exe", no arguments.
// The executable path lets File Explorer windows that expose no AppUserModelID group with the pin.
// The desktop (Progman/WorkerW) and taskbar windows of the same process are excluded from the
// running list by class (WindowEligibility.h), so they never count as File Explorer windows.
#pragma once

#include "applications/ApplicationTypes.h"
#include "applications/WindowIdentityRules.h"
#include "core/Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace aether::apps {

inline constexpr std::wstring_view kFileExplorerAumid = L"Microsoft.Windows.Explorer";
inline constexpr std::wstring_view kFileExplorerDefaultName = L"File Explorer";

// "aumid:microsoft.windows.explorer".
[[nodiscard]] std::wstring FileExplorerId();

// The Windows directory of the system (GetSystemWindowsDirectoryW: correct under Terminal Services,
// unlike GetWindowsDirectoryW). No file-system access.
[[nodiscard]] Result<std::wstring> SystemWindowsDirectory();

// Pure. True when `executablePath` is <windowsDirectory>\explorer.exe (lexical comparison, ignoring
// case). False when either is empty or invalid.
[[nodiscard]] bool IsFileExplorerExecutable(std::wstring_view executablePath, std::wstring_view windowsDirectory);

// Pure. True for File Explorer in any of its forms, when it carries no arguments: the File Explorer
// AppUserModelID, or <windowsDirectory>\explorer.exe without an AppUserModelID of its own. Shortcuts
// that open a folder (arguments) are distinct applications.
[[nodiscard]] bool IsFileExplorer(const ApplicationInfo& app, std::wstring_view windowsDirectory);

// Pure. When IsFileExplorer(app, windowsDirectory) holds, rewrites `app` to the canonical identity
// (id, AppUserModelID, parsing name, Desktop kind and, when `windowsDirectory` is valid, the
// canonical executable path); the display name, shortcut and keywords are kept. Returns whether it
// applied.
bool CanonicalizeFileExplorer(ApplicationInfo& app, std::wstring_view windowsDirectory);

// Pure. Running windows of <windowsDirectory>\explorer.exe that expose no AppUserModelID are File
// Explorer windows: their identity becomes the canonical File Explorer identity, so they form a
// single group with the ones that do expose it. Returns whether it applied.
bool CanonicalizeFileExplorerWindow(WindowIdentity& identity, std::wstring_view windowsDirectory);

// Worker thread only (checks the file system). The default File Explorer pin: the catalog's entry for
// the File Explorer AppUserModelID when there is one (its localized name is kept), else a new entry
// named "File Explorer"; canonicalized, with the executable path filled whenever
// <windowsDirectory>\explorer.exe is an existing regular file. `windowsDirectory` empty means
// SystemWindowsDirectory(). NotFound when neither the catalog entry nor the executable exists.
[[nodiscard]] Result<ApplicationInfo> MakeFileExplorerApplication(const std::vector<ApplicationInfo>& catalog,
                                                                  std::wstring windowsDirectory = {});

}  // namespace aether::apps
