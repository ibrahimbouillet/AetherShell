// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/CommandLine.h"
using namespace aether;
AETHER_TEST("app: startup action and isolated data arguments are retained") {
    auto parsed=app::ParseCommandLine({L"--autostart",L"--settings",L"--data-dir",L"synthetic-data",L"--post-update-check",L"0123456789abcdef"});
    REQUIRE(parsed.ok());CHECK(parsed->autostart);CHECK_EQ(parsed->action,app::StartupAction::OpenSettings);
    REQUIRE(parsed->dataDirectory.has_value());CHECK_EQ(parsed->dataDirectory->wstring(),std::wstring(L"synthetic-data"));CHECK_EQ(parsed->healthToken,std::wstring(L"0123456789abcdef"));
}
AETHER_TEST("app: invalid health tokens and incomplete path arguments are rejected") {
    CHECK_FALSE(app::ParseCommandLine({L"--post-update-check",L"short"}).ok());
    CHECK_FALSE(app::ParseCommandLine({L"--post-update-check",L"0123456789abcdeg"}).ok());
    CHECK_FALSE(app::ParseCommandLine({L"--data-dir"}).ok());CHECK_FALSE(app::ParseCommandLine({L"--unknown"}).ok());
}
AETHER_TEST("app: empty startup and explicit search version switches parse") {
    auto empty=app::ParseCommandLine({});REQUIRE(empty.ok());CHECK_EQ(empty->action,app::StartupAction::None);
    auto search=app::ParseCommandLine({L"--search",L"--version"});REQUIRE(search.ok());CHECK(search->showVersion);CHECK_EQ(search->action,app::StartupAction::OpenSearch);
}
