// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure rules of the application catalog: which discovered shell items are applications, how they
// are classified (Packaged / Desktop / Other), and how Apps-folder entries and Start-menu shortcuts
// describing the same application are merged. Kept free of COM so they can be unit tested.
#pragma once

#include "applications/ApplicationTypes.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aether::apps {

// Raw facts about one Apps-folder item or one Start-menu shortcut, exactly as reported by the shell.
struct DiscoveredItem {
    std::wstring displayName;
    std::wstring parsingName;     // Apps-folder parsing name; empty for Start-menu shortcuts
    std::wstring appUserModelId;  // System.AppUserModel.ID (may be path-like or auto-generated)
    std::wstring targetPath;      // System.Link.TargetParsingPath / IShellLink target: exe, document,
                                  // URL or shell namespace object ("::{CLSID}")
    std::wstring arguments;       // System.Link.Arguments / IShellLink arguments (trusted definition)
    std::wstring shortcutPath;    // the .lnk file, for Start-menu shortcuts
    bool hasPackageIdentity = false;  // System.AppUserModel.PackageFamilyName (or similar) is present
};

enum class CatalogDecision {
    Include,
    SkipInvalid,      // no display name, no launchable target or no usable identity
    SkipUninstaller,  // uninstallers and Windows Installer maintenance shortcuts
    SkipHelp,         // help viewers and help/readme documents
    SkipDocument,     // non-executable targets (documents, MMC consoles, folders...)
    SkipUrl,          // web links and other URL protocols
};

struct CatalogClassification {
    CatalogDecision decision = CatalogDecision::SkipInvalid;
    ApplicationInfo app;  // filled (including id) when decision == Include
};

// "Uninstall ..." (case-insensitive, after trimming).
[[nodiscard]] bool IsUninstallerName(std::wstring_view displayName) noexcept;
// unins*.exe (Inno Setup unins000.exe, NSIS uninst.exe, uninstall*.exe) and msiexec.exe (Windows
// Installer shortcuts only ever repair or remove products). Accepts a full path or a file name.
[[nodiscard]] bool IsUninstallerExecutable(std::wstring_view executablePath) noexcept;
// hh.exe / winhlp32.exe (help viewers).
[[nodiscard]] bool IsHelpViewerExecutable(std::wstring_view executablePath) noexcept;
// "https://...", "mailto:...", "steam://..." (a scheme of two or more characters followed by ':').
[[nodiscard]] bool IsUrlTarget(std::wstring_view target) noexcept;
// "::{CLSID}..." shell namespace objects (File Explorer, Control Panel, Run...).
[[nodiscard]] bool IsShellNamespaceTarget(std::wstring_view target) noexcept;

// The Apps folder's own absolute parsing name prefix ("::{4234D49B-0245-4DF3-B780-3893943456E1}\").
inline constexpr std::wstring_view kAppsFolderParsingPrefix = L"::{4234D49B-0245-4DF3-B780-3893943456E1}\\";

// The child name inside the Apps folder ("Microsoft.WindowsCalculator_8wekyb3d8bbwe!App") when
// `absoluteParsingName` (SIGDN_DESKTOPABSOLUTEPARSING) names an Apps-folder child, either as
// "::{4234D49B-...}\<name>" or "shell:AppsFolder\<name>" (prefixes ignore case). nullopt for any
// other item or an empty child name. Note: an item rebuilt from a shortcut's ID list can report only
// its child name; check its parent with IsAppsFolderParsingName instead.
[[nodiscard]] std::optional<std::wstring> AppsFolderChildName(std::wstring_view absoluteParsingName);

// True when `absoluteParsingName` names the Apps folder itself ("::{4234D49B-...}" or
// "shell:AppsFolder", ignoring case and a trailing backslash).
[[nodiscard]] bool IsAppsFolderParsingName(std::wstring_view absoluteParsingName) noexcept;

// Applies the catalog filters and builds the ApplicationInfo (with its id, see MakeApplicationId):
//  * uninstallers (by name or target executable)                                 -> SkipUninstaller
//  * packaged applications (packaged AUMID or package identity)                  -> Include, Packaged
//  * .exe targets (help viewers excluded)                                         -> Include, Desktop
//  * shell namespace targets and target-less Apps-folder items with an explicit
//    AUMID, both only when they come from the Apps folder (launchable by name)    -> Include, Other
//  * URLs, help files, documents and everything else                              -> Skip*
// The AUMID is kept only when it is packaged or explicit (IsExplicitAumid); the Apps folder's own
// synthesized IDs never identify running windows.
[[nodiscard]] CatalogClassification ClassifyDiscoveredItem(const DiscoveredItem& item);

// Merges entries that share an id (ordinal, case-insensitive). Within a group the primary entry is
// the one with both an AUMID and an executable, then an AUMID or an executable, then a parsing name,
// then the earliest; its empty fields are filled from the other members. A group is Packaged when
// any member is. Groups keep the order of their first member.
[[nodiscard]] std::vector<ApplicationInfo> MergeDuplicateApplications(std::vector<ApplicationInfo> entries);

// Combines Apps-folder entries with Start-menu shortcut entries. A shortcut enriches the Apps-folder
// entry it describes (fills shortcutPath, and executablePath/arguments/AUMID when missing) instead of
// creating a duplicate. It describes an entry when both carry the same AUMID, or, without an AUMID on
// the shortcut, when both start the same executable with the same arguments (several candidates are
// disambiguated by display name, then by the first one without a shortcut). Unmatched shortcuts are
// added. The result is merged by id and sorted by display name (case-insensitive, user locale), then id.
[[nodiscard]] std::vector<ApplicationInfo> CombineCatalog(std::vector<ApplicationInfo> appsFolderEntries,
                                                          std::vector<ApplicationInfo> shortcutEntries);

}  // namespace aether::apps
