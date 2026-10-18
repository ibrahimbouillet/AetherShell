// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Discovers the applications installed on this machine (read-only: nothing is launched).
#include "AetherTest.h"

#include "applications/ApplicationIdentity.h"
#include "applications/CatalogRules.h"
#include "applications/ShellApplicationCatalog.h"
#include "applications/ShellItems.h"
#include "core/FileSystem.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/windows/ComApartment.h"

#include <windows.h>
#include <objbase.h>

#include <chrono>
#include <optional>
#include <thread>
#include <unordered_set>

using namespace aether;
using namespace aether::apps;

namespace {

std::string KindName(ApplicationKind kind) {
    switch (kind) {
        case ApplicationKind::Desktop: return "Desktop";
        case ApplicationKind::Packaged: return "Packaged";
        case ApplicationKind::Other: return "Other";
    }
    return "?";
}

}  // namespace

AETHER_TEST("catalog: discovers installed packaged and desktop applications") {
    // Capture the module's log to check that the discovery duration is reported.
    diag::LoggerOptions logOptions;
    logOptions.directory = test::MakeTempDirectory("apps-catalog-log");
    logOptions.writeToDebugger = false;
    diag::Logger::Initialize(logOptions);

    ShellApplicationCatalog catalog;
    CancellationSource source;
    const auto start = std::chrono::steady_clock::now();
    auto result = catalog.DiscoverApplications(source.Token());
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (!result.ok()) TEST_NOTE(result.error().Describe());
    REQUIRE(result.ok());
    const auto& apps = result.value();

    std::size_t packaged = 0, desktop = 0, other = 0, withShortcut = 0, withAumidAndExe = 0;
    std::unordered_set<std::wstring> ids;
    for (const auto& app : apps) {
        switch (app.kind) {
            case ApplicationKind::Packaged: ++packaged; break;
            case ApplicationKind::Desktop: ++desktop; break;
            case ApplicationKind::Other: ++other; break;
        }
        if (!app.shortcutPath.empty()) ++withShortcut;
        if (!app.appUserModelId.empty() && !app.executablePath.empty()) ++withAumidAndExe;

        CHECK_FALSE(app.id.empty());
        CHECK_FALSE(app.displayName.empty());
        CHECK(GetIdScheme(app.id) != IdScheme::Invalid);
        CHECK(ids.insert(text::ToLower(app.id)).second);  // merged: ids are unique
        CHECK_FALSE(IsUninstallerName(app.displayName));
        if (!app.executablePath.empty()) {
            CHECK_FALSE(IsUninstallerExecutable(app.executablePath));
            CHECK(NormalizeExecutablePath(app.executablePath).ok());
        }
        if (app.kind == ApplicationKind::Desktop) CHECK_FALSE(app.executablePath.empty());
        if (app.kind == ApplicationKind::Packaged) CHECK_FALSE(app.appUserModelId.empty());
    }
    TEST_NOTE("discovered " + std::to_string(apps.size()) + " applications in " + std::to_string(elapsed) + " ms (" +
              std::to_string(packaged) + " packaged, " + std::to_string(desktop) + " desktop, " +
              std::to_string(other) + " other; " + std::to_string(withShortcut) + " with a Start-menu shortcut, " +
              std::to_string(withAumidAndExe) + " with AUMID and executable)");
    std::size_t shown = 0;
    for (const auto& app : apps) {
        if (shown++ >= 12) break;
        TEST_NOTE("  " + KindName(app.kind) + "  " + text::Narrow(app.displayName) + "  [" + text::Narrow(app.id) + "]");
    }
    for (const auto& app : apps) {
        if (app.kind == ApplicationKind::Other) {
            TEST_NOTE("  Other  " + text::Narrow(app.displayName) + "  parsing name: " + text::Narrow(app.parsingName));
        }
    }
    CHECK(apps.size() > 10);
    CHECK(packaged >= 1);
    CHECK(desktop >= 1);

    const auto logFile = diag::Logger::CurrentFile();
    diag::Logger::Shutdown();  // flushes and closes the file
    const auto log = fs::ReadFile(logFile, 1u << 20);
    REQUIRE(log.ok());
    const auto line = log.value().find("discovered ");
    REQUIRE(line != std::string::npos);
    const auto lineEnd = log.value().find_first_of("\r\n", line);
    TEST_NOTE("log: " + log.value().substr(line, lineEnd == std::string::npos ? std::string::npos : lineEnd - line));
}

AETHER_TEST("catalog: every Apps-folder entry resolves to its shell item") {
    // The launcher's first route and the icon provider both start from "shell:AppsFolder\<parsing
    // name>" (packaged apps also by AUMID). Resolving the item launches nothing.
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    auto result = ShellApplicationCatalog().DiscoverApplications(CancellationToken{});
    REQUIRE(result.ok());
    std::size_t checked = 0, failures = 0, packagedByAumid = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& app : result.value()) {
        if (app.parsingName.empty()) continue;
        ++checked;
        CHECK_FALSE(shell::HasRelativeSegment(app.parsingName));  // the hardening never rejects real names
        auto item = shell::CreateAppsFolderItem(app.parsingName);
        if (!item.ok()) {
            ++failures;
            TEST_NOTE("unresolvable: " + text::Narrow(app.displayName) + " [" + text::Narrow(app.parsingName) +
                      "]: " + item.error().Describe());
            continue;
        }
        const auto name = shell::GetDisplayName(item.value().get(), SIGDN_NORMALDISPLAY);
        CHECK(name.has_value());
        if (app.kind == ApplicationKind::Packaged) {
            CHECK(shell::CreateAppsFolderItem(app.appUserModelId).ok());
            ++packagedByAumid;
        }
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    TEST_NOTE(std::to_string(checked) + " Apps-folder entries resolved in " + std::to_string(elapsed) + " ms (" +
              std::to_string(packagedByAumid) + " packaged also by AUMID), " + std::to_string(failures) +
              " unresolvable");
    CHECK(checked > 10);
    CHECK_EQ(failures, std::size_t{0});
}

AETHER_TEST("catalog: works when called from a multithreaded apartment") {
    std::optional<Result<std::vector<ApplicationInfo>>> result;
    std::thread worker([&] {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ShellApplicationCatalog::Options options;
        options.includeStartMenuShortcuts = false;  // the Apps folder alone is enough here
        result.emplace(ShellApplicationCatalog(options).DiscoverApplications(CancellationToken{}));
        if (SUCCEEDED(hr)) ::CoUninitialize();
    });
    worker.join();
    REQUIRE(result.has_value());
    if (!result->ok()) TEST_NOTE(result->error().Describe());
    REQUIRE(result->ok());
    TEST_NOTE("Apps folder only, from the MTA: " + std::to_string(result->value().size()) + " applications");
    CHECK(result->value().size() > 10);
}

AETHER_TEST("catalog: honours cancellation") {
    CancellationSource source;
    source.Cancel();
    auto result = ShellApplicationCatalog().DiscoverApplications(source.Token());
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::Cancelled);
}
