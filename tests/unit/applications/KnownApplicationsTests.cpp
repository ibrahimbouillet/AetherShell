// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// File Explorer identity with an injected Windows directory. Pure cases use a synthetic directory
// that does not exist; MakeFileExplorerApplication uses a temporary folder with a fixture file.
#include "AetherTest.h"

#include "applications/ApplicationIdentity.h"
#include "applications/KnownApplications.h"

#include <filesystem>
#include <fstream>

using namespace aether;
using namespace aether::apps;

namespace {

constexpr wchar_t kWindows[] = L"C:\\AetherFixtureWindows";
constexpr wchar_t kExplorer[] = L"C:\\AetherFixtureWindows\\explorer.exe";

ApplicationInfo WithId(ApplicationInfo app) {
    auto id = MakeApplicationId(app);
    REQUIRE(id.ok());
    app.id = std::move(id).value();
    return app;
}

ApplicationInfo ExecutablePin(std::wstring path, std::wstring arguments = {}) {
    ApplicationInfo app;
    app.displayName = L"Fixture Explorer Shortcut";
    app.executablePath = std::move(path);
    app.arguments = std::move(arguments);
    app.shortcutPath = L"C:\\Fixture\\Pins\\File Explorer.lnk";
    return WithId(std::move(app));
}

ApplicationInfo AumidPin(std::wstring aumid) {
    ApplicationInfo app;
    app.displayName = L"Fixture Explorer Item";
    app.kind = ApplicationKind::Other;
    app.appUserModelId = aumid;
    app.parsingName = std::move(aumid);
    return WithId(std::move(app));
}

}  // namespace

AETHER_TEST("known applications: File Explorer has one stable identity") {
    CHECK_EQ(FileExplorerId(), std::wstring(L"aumid:microsoft.windows.explorer"));
    CHECK_EQ(FileExplorerId(), MakeIdForAumid(kFileExplorerAumid).value());
    const auto system = SystemWindowsDirectory();
    REQUIRE(system.ok());
    CHECK_FALSE(system.value().empty());
}

AETHER_TEST("known applications: executable-only and AUMID-only Explorer pins canonicalize alike") {
    auto byExecutable = ExecutablePin(kExplorer);
    CHECK(byExecutable.id.starts_with(kExecutableIdPrefix));
    REQUIRE(CanonicalizeFileExplorer(byExecutable, kWindows));
    CHECK_EQ(byExecutable.id, FileExplorerId());
    CHECK_EQ(byExecutable.appUserModelId, std::wstring(kFileExplorerAumid));
    CHECK_EQ(byExecutable.parsingName, std::wstring(kFileExplorerAumid));
    CHECK_EQ(byExecutable.executablePath, std::wstring(kExplorer));
    CHECK_EQ(byExecutable.kind, ApplicationKind::Desktop);
    CHECK_EQ(byExecutable.displayName, std::wstring(L"Fixture Explorer Shortcut"));  // kept
    CHECK_EQ(byExecutable.shortcutPath, std::wstring(L"C:\\Fixture\\Pins\\File Explorer.lnk"));

    auto byAumid = AumidPin(L"microsoft.windows.EXPLORER");
    REQUIRE(CanonicalizeFileExplorer(byAumid, kWindows));
    CHECK_EQ(byAumid.id, byExecutable.id);
    CHECK_EQ(byAumid.appUserModelId, std::wstring(kFileExplorerAumid));
    CHECK_EQ(byAumid.executablePath, std::wstring(kExplorer));  // filled: windows without an AUMID group with it
    CHECK_EQ(byAumid.kind, ApplicationKind::Desktop);

    // Equivalent spellings of the directory and the path.
    auto spelled = ExecutablePin(L"c:/AetherFixtureWindows/System32/../EXPLORER.EXE");
    CHECK(CanonicalizeFileExplorer(spelled, L"C:\\AetherFixtureWindows\\"));
    CHECK_EQ(spelled.id, FileExplorerId());

    // Without a usable Windows directory the AUMID form still applies, without an executable.
    auto noDirectory = AumidPin(std::wstring(kFileExplorerAumid));
    REQUIRE(CanonicalizeFileExplorer(noDirectory, L""));
    CHECK(noDirectory.executablePath.empty());
    CHECK_EQ(noDirectory.kind, ApplicationKind::Other);
}

AETHER_TEST("known applications: folder shortcuts and look-alike executables stay distinct") {
    auto folder = ExecutablePin(kExplorer, L"/e,C:\\Fixture\\Projects");
    const auto before = folder.id;
    CHECK_FALSE(CanonicalizeFileExplorer(folder, kWindows));
    CHECK_EQ(folder.id, before);

    auto aumidWithArguments = AumidPin(std::wstring(kFileExplorerAumid));
    aumidWithArguments.arguments = L"/root,C:\\Fixture";
    CHECK_FALSE(IsFileExplorer(aumidWithArguments, kWindows));

    for (const wchar_t* path : {L"C:\\AetherFixtureOther\\explorer.exe", L"C:\\AetherFixtureWindows\\explorer2.exe",
                                L"C:\\AetherFixtureWindows\\System32\\explorer.exe"}) {
        auto other = ExecutablePin(path);
        CHECK_FALSE(CanonicalizeFileExplorer(other, kWindows));
        CHECK(other.id.starts_with(kExecutableIdPrefix));
    }
    // Another explicit AUMID on explorer.exe is another application.
    auto otherAumid = ExecutablePin(kExplorer);
    otherAumid.appUserModelId = L"Aether.Fixture.Other";
    CHECK_FALSE(IsFileExplorer(otherAumid, kWindows));
    CHECK_FALSE(IsFileExplorerExecutable(L"", kWindows));
    CHECK_FALSE(IsFileExplorerExecutable(kExplorer, L""));
    CHECK_FALSE(IsFileExplorerExecutable(L"\\\\server\\share\\explorer.exe", L"\\\\server\\share"));
}

AETHER_TEST("known applications: Explorer windows without an AUMID join the File Explorer group") {
    WindowIdentity window;
    window.id = MakeIdForExecutable(kExplorer).value();
    window.executablePath = kExplorer;
    window.processId = 42;
    REQUIRE(CanonicalizeFileExplorerWindow(window, kWindows));
    CHECK_EQ(window.id, FileExplorerId());
    CHECK_EQ(window.appUserModelId, std::wstring(kFileExplorerAumid));
    CHECK_EQ(window.executablePath, std::wstring(kExplorer));
    CHECK_EQ(window.processId, DWORD{42});

    WindowIdentity declared;
    declared.id = L"aumid:aether.fixture.other";
    declared.appUserModelId = L"Aether.Fixture.Other";
    declared.executablePath = kExplorer;
    CHECK_FALSE(CanonicalizeFileExplorerWindow(declared, kWindows));
    CHECK_EQ(declared.id, std::wstring(L"aumid:aether.fixture.other"));

    WindowIdentity other;
    other.id = L"exe:c:\\aetherfixtureother\\explorer.exe";
    other.executablePath = L"C:\\AetherFixtureOther\\explorer.exe";
    CHECK_FALSE(CanonicalizeFileExplorerWindow(other, kWindows));
}

AETHER_TEST("known applications: the default Explorer pin prefers the catalog and fills the executable") {
    const auto root = test::MakeTempDirectory("apps-known");
    const auto windows = root / L"Windows";
    std::filesystem::create_directories(windows);
    const auto explorerPath = windows / L"explorer.exe";
    std::ofstream(explorerPath, std::ios::binary) << "synthetic non-executable fixture";

    auto catalogEntry = AumidPin(std::wstring(kFileExplorerAumid));
    catalogEntry.displayName = L"Fixture Localized Explorer";
    auto unrelated = ExecutablePin(L"C:\\AetherFixtureOther\\tool.exe");

    auto fromCatalog = MakeFileExplorerApplication({unrelated, catalogEntry}, windows.native());
    REQUIRE(fromCatalog.ok());
    CHECK_EQ(fromCatalog.value().id, FileExplorerId());
    CHECK_EQ(fromCatalog.value().displayName, std::wstring(L"Fixture Localized Explorer"));
    CHECK(MakeIdForExecutable(fromCatalog.value().executablePath).value() ==
          MakeIdForExecutable(explorerPath.native()).value());
    CHECK_EQ(fromCatalog.value().parsingName, std::wstring(kFileExplorerAumid));
    CHECK(fromCatalog.value().arguments.empty());

    auto fallback = MakeFileExplorerApplication({unrelated}, windows.native());
    REQUIRE(fallback.ok());
    CHECK_EQ(fallback.value().displayName, std::wstring(kFileExplorerDefaultName));
    CHECK_EQ(fallback.value().id, FileExplorerId());
    CHECK_FALSE(fallback.value().executablePath.empty());

    // No executable on disk: the catalog entry is still offered, without an executable path.
    const auto empty = root / L"EmptyWindows";
    std::filesystem::create_directories(empty);
    auto withoutFile = MakeFileExplorerApplication({catalogEntry}, empty.native());
    REQUIRE(withoutFile.ok());
    CHECK(withoutFile.value().executablePath.empty());
    CHECK_EQ(withoutFile.value().kind, ApplicationKind::Other);
    auto nothing = MakeFileExplorerApplication({unrelated}, empty.native());
    REQUIRE_FALSE(nothing.ok());
    CHECK_EQ(nothing.error().code, ErrorCode::NotFound);

    // A directory named explorer.exe is not the executable.
    const auto trap = root / L"TrapWindows";
    std::filesystem::create_directories(trap / L"explorer.exe");
    CHECK_FALSE(MakeFileExplorerApplication({}, trap.native()).ok());

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
