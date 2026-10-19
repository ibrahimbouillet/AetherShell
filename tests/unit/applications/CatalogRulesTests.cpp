// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "applications/CatalogRules.h"
#include "core/Strings.h"

#include <algorithm>

using namespace aether;
using namespace aether::apps;

namespace {

constexpr const wchar_t* kCalculatorAumid = L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
constexpr const wchar_t* kSevenZipParsing = L"{6D809377-6AF0-444B-8957-A3773F02200E}\\7-Zip\\7zFM.exe";
constexpr const wchar_t* kStartMenu = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\";

DiscoveredItem AppsFolderItem(std::wstring name, std::wstring parsingName, std::wstring aumid, std::wstring target) {
    DiscoveredItem item;
    item.displayName = std::move(name);
    item.parsingName = std::move(parsingName);
    item.appUserModelId = std::move(aumid);
    item.targetPath = std::move(target);
    return item;
}

DiscoveredItem Shortcut(std::wstring name, std::wstring target, std::wstring arguments = {}, std::wstring aumid = {}) {
    DiscoveredItem item;
    item.displayName = name;
    item.targetPath = std::move(target);
    item.arguments = std::move(arguments);
    item.appUserModelId = std::move(aumid);
    item.shortcutPath = std::wstring(kStartMenu) + name + L".lnk";
    return item;
}

ApplicationInfo Include(const DiscoveredItem& item) {
    auto classification = ClassifyDiscoveredItem(item);
    REQUIRE_EQ(classification.decision, CatalogDecision::Include);
    return classification.app;
}

const ApplicationInfo* FindByName(const std::vector<ApplicationInfo>& apps, std::wstring_view name) {
    const auto found = std::find_if(apps.begin(), apps.end(), [&](const ApplicationInfo& app) { return app.displayName == name; });
    return found == apps.end() ? nullptr : &*found;
}

}  // namespace

AETHER_TEST("catalog rules: uninstaller detection") {
    CHECK(IsUninstallerName(L"Uninstall Foo"));
    CHECK(IsUninstallerName(L"  uninstall foo"));
    CHECK(IsUninstallerName(L"UNINSTALL"));
    CHECK_FALSE(IsUninstallerName(L"Foo Uninstaller"));
    CHECK_FALSE(IsUninstallerName(L"Unity Hub"));

    CHECK(IsUninstallerExecutable(L"C:\\Program Files\\Foo\\unins000.exe"));
    CHECK(IsUninstallerExecutable(L"C:\\Program Files\\Foo\\Uninstall.exe"));
    CHECK(IsUninstallerExecutable(L"C:\\Program Files\\Foo\\uninstall_foo.exe"));
    CHECK(IsUninstallerExecutable(L"C:\\Program Files\\Foo\\Uninst.exe"));
    CHECK(IsUninstallerExecutable(L"C:\\Windows\\System32\\msiexec.exe"));
    CHECK(IsUninstallerExecutable(L"unins001.exe"));
    CHECK_FALSE(IsUninstallerExecutable(L"C:\\Program Files\\Foo\\installer.exe"));
    CHECK_FALSE(IsUninstallerExecutable(L"C:\\Program Files\\Foo\\unins000.dat"));
    CHECK_FALSE(IsUninstallerExecutable(L"C:\\Program Files\\Unity\\Unity.exe"));
}

AETHER_TEST("catalog rules: help, URL and shell-namespace targets") {
    CHECK(IsHelpViewerExecutable(L"C:\\Windows\\hh.exe"));
    CHECK(IsHelpViewerExecutable(L"winhlp32.exe"));
    CHECK_FALSE(IsHelpViewerExecutable(L"C:\\Windows\\notepad.exe"));

    CHECK(IsUrlTarget(L"https://example.com/"));
    CHECK(IsUrlTarget(L"mailto:someone@example.com"));
    CHECK(IsUrlTarget(L"steam://rungameid/10"));
    CHECK(IsUrlTarget(L"ms-settings:display"));
    CHECK_FALSE(IsUrlTarget(L"C:\\Windows\\notepad.exe"));
    CHECK_FALSE(IsUrlTarget(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
    CHECK_FALSE(IsUrlTarget(L"notepad"));
    CHECK_FALSE(IsUrlTarget(L"1a:thing"));

    CHECK(IsShellNamespaceTarget(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
    CHECK_FALSE(IsShellNamespaceTarget(L"C:\\x.exe"));
}

AETHER_TEST("catalog rules: packaged applications") {
    const auto app = Include(AppsFolderItem(L"Calculator", kCalculatorAumid, kCalculatorAumid, L""));
    CHECK_EQ(app.kind, ApplicationKind::Packaged);
    CHECK_EQ(app.id, std::wstring(L"aumid:microsoft.windowscalculator_8wekyb3d8bbwe!app"));
    CHECK_EQ(app.appUserModelId, std::wstring(kCalculatorAumid));
    CHECK_EQ(app.parsingName, std::wstring(kCalculatorAumid));

    // Package identity reported by the shell even though the AUMID lacks the "!" form.
    auto withIdentity = AppsFolderItem(L"Packaged Tool", L"Contoso.Tool", L"Contoso.Tool", L"");
    withIdentity.hasPackageIdentity = true;
    CHECK_EQ(Include(withIdentity).kind, ApplicationKind::Packaged);
}

AETHER_TEST("catalog rules: desktop applications from the Apps folder") {
    const auto app = Include(
        AppsFolderItem(L"7-Zip File Manager", kSevenZipParsing, kSevenZipParsing, L"C:\\Program Files\\7-Zip\\7zFM.exe"));
    CHECK_EQ(app.kind, ApplicationKind::Desktop);
    CHECK_EQ(app.id, std::wstring(L"exe:c:\\program files\\7-zip\\7zfm.exe"));
    CHECK_EQ(app.executablePath, std::wstring(L"C:\\Program Files\\7-Zip\\7zFM.exe"));
    CHECK(app.appUserModelId.empty());  // the Apps folder's path-derived id identifies no window
    CHECK_EQ(app.parsingName, std::wstring(kSevenZipParsing));
    CHECK_EQ(app.keywords, std::wstring(L"7zFM"));

    const auto chrome = Include(AppsFolderItem(L"Google Chrome", L"Chrome", L"Chrome",
                                               L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"));
    CHECK_EQ(chrome.kind, ApplicationKind::Desktop);
    CHECK_EQ(chrome.id, std::wstring(L"aumid:chrome"));  // explicit AUMID wins
    CHECK_EQ(chrome.appUserModelId, std::wstring(L"Chrome"));
}

AETHER_TEST("catalog rules: uninstallers, help, URLs and documents are skipped") {
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Uninstall Foo", L"C:\\Program Files\\Foo\\foo.exe")).decision,
             CatalogDecision::SkipUninstaller);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Foo Remover", L"C:\\Program Files\\Foo\\unins000.exe")).decision,
             CatalogDecision::SkipUninstaller);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Repair Foo", L"C:\\Windows\\System32\\msiexec.exe", L"/f {GUID}")).decision,
             CatalogDecision::SkipUninstaller);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Foo Manual", L"C:\\Program Files\\Foo\\manual.chm")).decision,
             CatalogDecision::SkipHelp);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Foo Readme", L"C:\\Program Files\\Foo\\README.txt")).decision,
             CatalogDecision::SkipHelp);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Foo Help", L"C:\\Windows\\hh.exe", L"foo.chm")).decision,
             CatalogDecision::SkipHelp);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Foo Website", L"https://foo.example/")).decision,
             CatalogDecision::SkipUrl);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Foo Online", L"C:\\Program Files\\Foo\\Foo.url")).decision,
             CatalogDecision::SkipUrl);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Budget", L"C:\\Users\\Public\\Budget.xlsx")).decision,
             CatalogDecision::SkipDocument);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Services", L"C:\\Windows\\System32\\services.msc")).decision,
             CatalogDecision::SkipDocument);
}

AETHER_TEST("catalog rules: invalid entries are skipped") {
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"", L"C:\\Windows\\notepad.exe")).decision, CatalogDecision::SkipInvalid);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Relative", L"tool.exe")).decision, CatalogDecision::SkipInvalid);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Network", L"\\\\server\\share\\tool.exe")).decision,
             CatalogDecision::SkipInvalid);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"Device", L"\\\\.\\C:\\tool.exe")).decision, CatalogDecision::SkipInvalid);
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"No Target", L"")).decision, CatalogDecision::SkipInvalid);
    // Shell objects are launchable only by Apps-folder name, not from a Start-menu file.
    CHECK_EQ(ClassifyDiscoveredItem(Shortcut(L"This PC", L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}")).decision,
             CatalogDecision::SkipInvalid);
    // Apps-folder item without target or explicit identity.
    CHECK_EQ(ClassifyDiscoveredItem(AppsFolderItem(L"Mystery", L"C:\\x\\thing", L"C:\\x\\thing", L"")).decision,
             CatalogDecision::SkipInvalid);
}

AETHER_TEST("catalog rules: shell objects from the Apps folder") {
    const auto explorer = Include(
        AppsFolderItem(L"File Explorer", L"Microsoft.Windows.Explorer", L"Microsoft.Windows.Explorer", L""));
    CHECK_EQ(explorer.kind, ApplicationKind::Other);
    CHECK_EQ(explorer.id, std::wstring(L"aumid:microsoft.windows.explorer"));

    const auto thisPc = Include(AppsFolderItem(L"This PC", L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", L"",
                                               L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
    CHECK_EQ(thisPc.kind, ApplicationKind::Other);
    CHECK_EQ(thisPc.id, std::wstring(L"shell:::{20d04fe0-3aea-1069-a2d8-08002b30309d}"));
}

AETHER_TEST("catalog rules: shortcuts with trusted arguments keep distinct identities") {
    const auto plain = Include(Shortcut(L"Command Prompt", L"C:\\Windows\\System32\\cmd.exe"));
    const auto developer = Include(Shortcut(L"Developer Command Prompt", L"C:\\Windows\\System32\\cmd.exe",
                                            L"/k \"C:\\VS\\VsDevCmd.bat\""));
    CHECK_EQ(plain.id, std::wstring(L"exe:c:\\windows\\system32\\cmd.exe"));
    CHECK_EQ(developer.id, std::wstring(L"shell:") + std::wstring(L"c:\\programdata\\microsoft\\windows\\start menu\\"
                                                                 L"programs\\developer command prompt.lnk"));
    CHECK_EQ(developer.arguments, std::wstring(L"/k \"C:\\VS\\VsDevCmd.bat\""));
    CHECK_NE(plain.id, developer.id);
}

AETHER_TEST("catalog rules: duplicates merge and prefer entries with an AUMID and an executable") {
    ApplicationInfo exeOnly;
    exeOnly.id = L"aumid:chrome";
    exeOnly.displayName = L"Chrome (shortcut)";
    exeOnly.executablePath = L"C:\\Chrome\\chrome.exe";
    exeOnly.shortcutPath = L"C:\\Start\\Chrome.lnk";
    exeOnly.keywords = L"browser";

    ApplicationInfo complete;
    complete.id = L"AUMID:Chrome";  // ids compare case-insensitively
    complete.displayName = L"Google Chrome";
    complete.appUserModelId = L"Chrome";
    complete.executablePath = L"C:\\Chrome\\chrome.exe";
    complete.parsingName = L"Chrome";

    ApplicationInfo other;
    other.id = L"exe:c:\\windows\\notepad.exe";
    other.displayName = L"Notepad";

    ApplicationInfo packagedTwin;
    packagedTwin.id = L"aumid:chrome";
    packagedTwin.kind = ApplicationKind::Packaged;
    packagedTwin.displayName = L"Chrome twin";

    const auto merged = MergeDuplicateApplications({exeOnly, other, complete, packagedTwin});
    REQUIRE_EQ(merged.size(), std::size_t{2});
    CHECK_EQ(merged[0].displayName, std::wstring(L"Google Chrome"));  // primary: AUMID + exe
    CHECK_EQ(merged[0].shortcutPath, std::wstring(L"C:\\Start\\Chrome.lnk"));  // filled from the other member
    CHECK_EQ(merged[0].keywords, std::wstring(L"browser"));
    CHECK_EQ(merged[0].kind, ApplicationKind::Packaged);
    CHECK_EQ(merged[1].displayName, std::wstring(L"Notepad"));  // groups keep first-appearance order
}

AETHER_TEST("catalog rules: start-menu shortcuts enrich Apps-folder entries instead of duplicating them") {
    std::vector<ApplicationInfo> appsFolder{
        Include(AppsFolderItem(L"Google Chrome", L"Chrome", L"Chrome", L"C:\\Chrome\\chrome.exe")),
        Include(AppsFolderItem(L"7-Zip File Manager", kSevenZipParsing, kSevenZipParsing,
                               L"C:\\Program Files\\7-Zip\\7zFM.exe")),
        Include(AppsFolderItem(L"Command Prompt", L"{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\cmd.exe",
                               L"{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\\cmd.exe", L"C:\\Windows\\System32\\cmd.exe")),
        Include(AppsFolderItem(L"Calculator", kCalculatorAumid, kCalculatorAumid, L"")),
    };
    auto devPromptItem = AppsFolderItem(L"Developer Command Prompt",
                                        L"Microsoft.AutoGenerated.{923DD477-5846-686B-A659-0FCCD73851A8}",
                                        L"Microsoft.AutoGenerated.{923DD477-5846-686B-A659-0FCCD73851A8}",
                                        L"C:\\Windows\\System32\\cmd.exe");
    devPromptItem.arguments = L"/k vcvars64.bat";
    appsFolder.push_back(Include(devPromptItem));

    std::vector<ApplicationInfo> shortcuts{
        Include(Shortcut(L"Google Chrome", L"C:\\Chrome\\chrome.exe", L"", L"Chrome")),
        Include(Shortcut(L"7-Zip File Manager", L"C:\\Program Files\\7-Zip\\7zFM.exe")),
        Include(Shortcut(L"Developer Command Prompt", L"C:\\Windows\\System32\\cmd.exe", L"/k vcvars64.bat")),
        Include(Shortcut(L"Legacy Tool", L"C:\\Legacy\\tool.exe")),
    };

    const auto catalog = CombineCatalog(appsFolder, shortcuts);
    CHECK_EQ(catalog.size(), std::size_t{6});

    const auto* chrome = FindByName(catalog, L"Google Chrome");
    REQUIRE(chrome != nullptr);
    CHECK_EQ(chrome->shortcutPath, std::wstring(kStartMenu) + L"Google Chrome.lnk");
    CHECK_EQ(chrome->parsingName, std::wstring(L"Chrome"));

    const auto* sevenZip = FindByName(catalog, L"7-Zip File Manager");
    REQUIRE(sevenZip != nullptr);
    CHECK_EQ(sevenZip->shortcutPath, std::wstring(kStartMenu) + L"7-Zip File Manager.lnk");

    const auto* devPrompt = FindByName(catalog, L"Developer Command Prompt");
    REQUIRE(devPrompt != nullptr);
    CHECK_EQ(devPrompt->shortcutPath, std::wstring(kStartMenu) + L"Developer Command Prompt.lnk");
    const auto* plainPrompt = FindByName(catalog, L"Command Prompt");
    REQUIRE(plainPrompt != nullptr);
    CHECK(plainPrompt->shortcutPath.empty());  // the developer shortcut did not attach to it

    const auto* legacy = FindByName(catalog, L"Legacy Tool");
    REQUIRE(legacy != nullptr);
    CHECK_EQ(legacy->id, std::wstring(L"exe:c:\\legacy\\tool.exe"));
    CHECK(legacy->parsingName.empty());

    // Sorted by display name.
    for (std::size_t i = 1; i < catalog.size(); ++i) {
        CHECK(text::ToLower(catalog[i - 1].displayName) <= text::ToLower(catalog[i].displayName));
    }
    // Ids are unique.
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        for (std::size_t j = i + 1; j < catalog.size(); ++j) CHECK_NE(catalog[i].id, catalog[j].id);
    }
}

AETHER_TEST("catalog rules: a shortcut duplicating a shortcut merges by id") {
    // The same executable reached from the per-user and the all-users Programs folder.
    auto common = Include(Shortcut(L"Tool", L"C:\\Tools\\tool.exe"));
    auto user = Include(Shortcut(L"Tool", L"c:\\tools\\TOOL.exe"));
    user.shortcutPath = L"C:\\Users\\Someone\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Tool.lnk";
    const auto catalog = CombineCatalog({}, {common, user});
    REQUIRE_EQ(catalog.size(), std::size_t{1});
    CHECK_EQ(catalog[0].id, std::wstring(L"exe:c:\\tools\\tool.exe"));
}

AETHER_TEST("catalog rules: Apps-folder child names from absolute parsing names") {
    CHECK_EQ(AppsFolderChildName(L"::{4234D49B-0245-4DF3-B780-3893943456E1}\\Microsoft.Windows.Explorer").value_or(L""),
             std::wstring(L"Microsoft.Windows.Explorer"));
    CHECK_EQ(AppsFolderChildName(L"::{4234d49b-0245-4df3-b780-3893943456e1}\\Aether.Fixture_123456789abcd!App")
                 .value_or(L""),
             std::wstring(L"Aether.Fixture_123456789abcd!App"));
    CHECK_EQ(AppsFolderChildName(L"SHELL:appsfolder\\Aether.Fixture.Explicit").value_or(L""),
             std::wstring(L"Aether.Fixture.Explicit"));
    CHECK_FALSE(AppsFolderChildName(L"::{4234D49B-0245-4DF3-B780-3893943456E1}\\").has_value());
    CHECK_FALSE(AppsFolderChildName(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\C:\\").has_value());  // This PC
    CHECK_FALSE(AppsFolderChildName(L"C:\\Fixture\\Tool.exe").has_value());
    CHECK_FALSE(AppsFolderChildName(L"").has_value());
}
