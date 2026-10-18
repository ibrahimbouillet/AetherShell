// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "applications/ApplicationIdentity.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace aether;
using namespace aether::apps;

namespace {

std::wstring IdOrEmpty(const Result<std::wstring>& result) { return result.ok() ? result.value() : std::wstring(); }

}  // namespace

AETHER_TEST("identity: aumid ids are trimmed and lower-cased") {
    CHECK_EQ(IdOrEmpty(MakeIdForAumid(L"  Microsoft.WindowsCalculator_8wekyb3d8bbwe!App ")),
             std::wstring(L"aumid:microsoft.windowscalculator_8wekyb3d8bbwe!app"));
    CHECK_EQ(IdOrEmpty(MakeIdForAumid(L"Microsoft.Windows.Explorer")), std::wstring(L"aumid:microsoft.windows.explorer"));
    CHECK_EQ(MakeIdForAumid(L"").error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(MakeIdForAumid(L"   ").error().code, ErrorCode::InvalidArgument);
    CHECK_FALSE(MakeIdForAumid(L"bad\nid").ok());
    CHECK_FALSE(MakeIdForAumid(std::wstring(2000, L'a')).ok());
}

AETHER_TEST("identity: parsing-name and window-class ids") {
    CHECK_EQ(IdOrEmpty(MakeIdForParsingName(L"{6D809377-6AF0-444B-8957-A3773F02200E}\\7-Zip\\7zFM.exe")),
             std::wstring(L"shell:{6d809377-6af0-444b-8957-a3773f02200e}\\7-zip\\7zfm.exe"));
    CHECK_FALSE(MakeIdForParsingName(L"").ok());
    CHECK_EQ(IdOrEmpty(MakeIdForWindowClass(L"ConsoleWindowClass")), std::wstring(L"window:consolewindowclass"));
}

AETHER_TEST("identity: executable ids are normalized full paths") {
    CHECK_EQ(IdOrEmpty(MakeIdForExecutable(L"c:/Program Files/App/./bin/../App.EXE")),
             std::wstring(L"exe:c:\\program files\\app\\app.exe"));
    CHECK_EQ(IdOrEmpty(MakeIdForExecutable(L"C:\\Tools\\\\Sub\\X.exe")), std::wstring(L"exe:c:\\tools\\sub\\x.exe"));
    CHECK_EQ(IdOrEmpty(MakeIdForExecutable(L"\\\\?\\C:\\Tools\\x.exe")), std::wstring(L"exe:c:\\tools\\x.exe"));
    CHECK_EQ(IdOrEmpty(MakeIdForExecutable(L"  C:\\Tools\\x.exe  ")), std::wstring(L"exe:c:\\tools\\x.exe"));
    // Identical files spelled differently share one id.
    CHECK_EQ(IdOrEmpty(MakeIdForExecutable(L"C:\\WINDOWS\\notepad.exe")),
             IdOrEmpty(MakeIdForExecutable(L"c:/windows/NOTEPAD.EXE")));

    const auto normalized = NormalizeExecutablePath(L"c:/tools/App.exe");
    REQUIRE(normalized.ok());
    CHECK_EQ(normalized.value(), std::wstring(L"C:\\tools\\App.exe"));  // case preserved, drive upper-cased
}

AETHER_TEST("identity: executable path validation rejects unsafe paths") {
    const std::array<const wchar_t*, 17> rejected{
        L"",
        L"notepad.exe",                                         // relative
        L"C:notepad.exe",                                       // drive-relative
        L"\\Windows\\notepad.exe",                              // rooted without a drive
        L"\\\\.\\C:\\x.exe",                                    // Win32 device namespace
        L"\\\\.\\PhysicalDrive0",                               // raw device
        L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\x.exe",   // object manager
        L"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\x.exe",
        L"\\??\\C:\\x.exe",                                     // NT namespace
        L"\\\\server\\share\\app.exe",                          // UNC (rejected by default)
        L"\\\\?\\UNC\\server\\share\\app.exe",                  // long-path UNC
        L"C:\\x\\readme.txt",                                   // not an executable
        L"C:\\x\\.exe",                                         // no file name
        L"C:\\x\\a:b.exe",                                      // alternate data stream
        L"C:\\x\\*.exe",                                        // wildcard
        L"C:\\x\\dir\\",                                        // directory
        L"C:\\x\\bad\tname.exe",                                // control character
    };
    for (const wchar_t* path : rejected) {
        const auto result = MakeIdForExecutable(path);
        if (result.ok()) TEST_NOTE("unexpectedly accepted: " + test::NarrowForDisplay(path));
        CHECK_FALSE(result.ok());
        if (!result.ok()) CHECK_EQ(result.error().code, ErrorCode::InvalidArgument);
    }
}

AETHER_TEST("identity: UNC paths are accepted only when the policy allows them") {
    PathPolicy allowUnc;
    allowUnc.allowUnc = true;
    CHECK_EQ(IdOrEmpty(MakeIdForExecutable(L"\\\\Server\\Share\\Tools\\App.exe", allowUnc)),
             std::wstring(L"exe:\\\\server\\share\\tools\\app.exe"));
    CHECK_EQ(IdOrEmpty(MakeIdForExecutable(L"\\\\?\\UNC\\Server\\Share\\App.exe", allowUnc)),
             std::wstring(L"exe:\\\\server\\share\\app.exe"));
    CHECK_FALSE(MakeIdForExecutable(L"\\\\server\\app.exe", allowUnc).ok());  // no share
    CHECK_FALSE(MakeIdForExecutable(L"\\\\.\\pipe\\x.exe", allowUnc).ok());   // still a device path
}

AETHER_TEST("identity: UNC paths are never expanded (no network access)") {
    // The window tracker computes ids on the UI thread with allowUnc. A '~' in a UNC path must not
    // trigger GetLongPathNameW, which would contact the (here unresolvable) server and could block for
    // seconds; the id keeps the short spelling instead. The time bound guards that property.
    PathPolicy allowUnc;
    allowUnc.allowUnc = true;
    const auto start = std::chrono::steady_clock::now();
    const auto id = IdOrEmpty(MakeIdForExecutable(L"\\\\aether-review.invalid\\Share\\PROGRA~1\\App.exe", allowUnc));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK_EQ(id, std::wstring(L"exe:\\\\aether-review.invalid\\share\\progra~1\\app.exe"));
    CHECK(elapsed < std::chrono::milliseconds(500));
}

AETHER_TEST("identity: shortcut paths must name .lnk files") {
    CHECK(NormalizeShortcutPath(L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\App.lnk").ok());
    CHECK_FALSE(NormalizeShortcutPath(L"C:\\x\\App.exe").ok());
    CHECK_FALSE(NormalizeShortcutPath(L"App.lnk").ok());
}

AETHER_TEST("identity: 8.3 short names map to the long-name id") {
    const auto directory = test::MakeTempDirectory("apps-shortname") / L"LongDirectoryNameForShortNames";
    std::filesystem::create_directories(directory);
    const auto file = directory / L"LongExecutableFileName.exe";
    { std::ofstream(file) << "not a real executable"; }

    std::array<wchar_t, MAX_PATH> shortPath{};
    const DWORD length = ::GetShortPathNameW(file.c_str(), shortPath.data(), static_cast<DWORD>(shortPath.size()));
    REQUIRE(length > 0);
    REQUIRE(length < shortPath.size());
    const std::wstring shortName(shortPath.data(), length);
    const auto longId = IdOrEmpty(MakeIdForExecutable(file.native()));
    CHECK_FALSE(longId.empty());
    if (shortName.find(L'~') == std::wstring::npos) {
        TEST_NOTE("8.3 names are disabled on this volume; expansion not exercised");
    } else {
        CHECK_EQ(IdOrEmpty(MakeIdForExecutable(shortName)), longId);
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory.parent_path(), ignored);
}

AETHER_TEST("identity: packaged AUMID syntax") {
    CHECK(IsPackagedAumid(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"));
    CHECK(IsPackagedAumid(L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel"));
    CHECK(IsPackagedAumid(L"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App"));
    CHECK_FALSE(IsPackagedAumid(L"Microsoft.Windows.Explorer"));
    CHECK_FALSE(IsPackagedAumid(L"Chrome"));
    CHECK_FALSE(IsPackagedAumid(L""));
    CHECK_FALSE(IsPackagedAumid(L"!App"));
    CHECK_FALSE(IsPackagedAumid(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!"));
    CHECK_FALSE(IsPackagedAumid(L"Microsoft.WindowsCalculator!App"));            // no publisher id
    CHECK_FALSE(IsPackagedAumid(L"Microsoft.WindowsCalculator_8wekyb3d!App"));   // publisher id too short
    CHECK_FALSE(IsPackagedAumid(L"ab_8wekyb3d8bbwe!App"));                       // name too short
    CHECK_FALSE(IsPackagedAumid(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App Name"));
    CHECK_FALSE(IsPackagedAumid(L"C:\\Tools\\x_8wekyb3d8bbwe!App"));
}

AETHER_TEST("identity: explicit AUMIDs exclude ids synthesized by the Apps folder") {
    CHECK(IsExplicitAumid(L"Microsoft.Windows.Explorer"));
    CHECK(IsExplicitAumid(L"Chrome"));
    CHECK(IsExplicitAumid(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"));
    CHECK_FALSE(IsExplicitAumid(L"{6D809377-6AF0-444B-8957-A3773F02200E}\\7-Zip\\7zFM.exe"));
    CHECK_FALSE(IsExplicitAumid(L"C:\\msys64\\msys2.exe"));
    CHECK_FALSE(IsExplicitAumid(L"https://example.com/"));
    CHECK_FALSE(IsExplicitAumid(L"Microsoft.AutoGenerated.{923DD477-5846-686B-A659-0FCCD73851A8}"));
    CHECK_FALSE(IsExplicitAumid(L""));
    CHECK_FALSE(IsExplicitAumid(std::wstring(129, L'a')));
}

AETHER_TEST("identity: id scheme detection") {
    CHECK_EQ(GetIdScheme(L"aumid:chrome"), IdScheme::Aumid);
    CHECK_EQ(GetIdScheme(L"exe:c:\\x.exe"), IdScheme::Executable);
    CHECK_EQ(GetIdScheme(L"shell:x"), IdScheme::Shell);
    CHECK_EQ(GetIdScheme(L"window:class"), IdScheme::Window);
    CHECK_EQ(GetIdScheme(L"aumid:"), IdScheme::Invalid);
    CHECK_EQ(GetIdScheme(L"AUMID:chrome"), IdScheme::Invalid);  // ids are produced lower-case
    CHECK_EQ(GetIdScheme(L"other"), IdScheme::Invalid);
}

AETHER_TEST("identity: application id preference order") {
    ApplicationInfo packaged;
    packaged.appUserModelId = L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    packaged.parsingName = packaged.appUserModelId;
    CHECK_EQ(IdOrEmpty(MakeApplicationId(packaged)), std::wstring(L"aumid:microsoft.windowscalculator_8wekyb3d8bbwe!app"));

    ApplicationInfo desktop;
    desktop.appUserModelId = L"{6D809377-6AF0-444B-8957-A3773F02200E}\\7-Zip\\7zFM.exe";  // synthesized: ignored
    desktop.parsingName = desktop.appUserModelId;
    desktop.executablePath = L"C:\\Program Files\\7-Zip\\7zFM.exe";
    CHECK_EQ(IdOrEmpty(MakeApplicationId(desktop)), std::wstring(L"exe:c:\\program files\\7-zip\\7zfm.exe"));

    // Same executable, different trusted arguments: distinct entries keyed by their parsing name.
    ApplicationInfo withArguments;
    withArguments.executablePath = L"C:\\Windows\\System32\\cmd.exe";
    withArguments.arguments = L"/k \"C:\\VS\\VsDevCmd.bat\"";
    withArguments.parsingName = L"Microsoft.AutoGenerated.{923DD477-5846-686B-A659-0FCCD73851A8}";
    CHECK_EQ(IdOrEmpty(MakeApplicationId(withArguments)),
             std::wstring(L"shell:microsoft.autogenerated.{923dd477-5846-686b-a659-0fccd73851a8}"));

    ApplicationInfo shortcutOnly;
    shortcutOnly.executablePath = L"C:\\Windows\\System32\\cmd.exe";
    shortcutOnly.arguments = L"/k vcvars64.bat";
    shortcutOnly.shortcutPath = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Dev Prompt.lnk";
    CHECK_EQ(IdOrEmpty(MakeApplicationId(shortcutOnly)),
             std::wstring(L"shell:c:\\programdata\\microsoft\\windows\\start menu\\programs\\dev prompt.lnk"));

    ApplicationInfo argumentsOnly;
    argumentsOnly.executablePath = L"C:\\Windows\\System32\\cmd.exe";
    argumentsOnly.arguments = L"/k";
    CHECK_EQ(IdOrEmpty(MakeApplicationId(argumentsOnly)), std::wstring(L"exe:c:\\windows\\system32\\cmd.exe"));

    ApplicationInfo nothing;
    nothing.displayName = L"Ghost";
    CHECK_EQ(MakeApplicationId(nothing).error().code, ErrorCode::InvalidArgument);

    ApplicationInfo invalidPath;
    invalidPath.executablePath = L"relative.exe";
    CHECK_FALSE(MakeApplicationId(invalidPath).ok());
}

AETHER_TEST("identity: catalog entries are joined with running applications") {
    ApplicationInfo chrome;
    chrome.id = L"aumid:chrome";
    chrome.appUserModelId = L"Chrome";
    chrome.executablePath = L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";

    RunningApplication running;
    running.id = L"aumid:chrome";
    CHECK(RefersToSameApplication(chrome, running));

    RunningApplication profile;  // another browser profile: same exe, different AUMID
    profile.id = L"aumid:chrome.profile2";
    profile.appUserModelId = L"Chrome.Profile2";
    profile.executablePath = chrome.executablePath;
    CHECK_FALSE(RefersToSameApplication(chrome, profile));

    RunningApplication byPath;
    byPath.id = L"exe:c:\\program files\\google\\chrome\\application\\chrome.exe";
    byPath.executablePath = L"c:\\program files\\google\\chrome\\application\\CHROME.EXE";
    CHECK(RefersToSameApplication(chrome, byPath));

    RunningApplication other;
    other.id = L"exe:c:\\windows\\notepad.exe";
    other.executablePath = L"C:\\Windows\\notepad.exe";
    CHECK_FALSE(RefersToSameApplication(chrome, other));
}
