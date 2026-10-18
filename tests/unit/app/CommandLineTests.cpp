// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/CommandLine.h"
#include "app/ShellApplication.h"
#include "platform/windows/SingleInstance.h"
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
AETHER_TEST("app: taskbar recovery and quit are explicit startup actions") {
    auto quit=app::ParseCommandLine({L"--quit"});REQUIRE(quit.ok());CHECK_EQ(quit->action,app::StartupAction::Quit);
    auto restore=app::ParseCommandLine({L"--restore-taskbar"});REQUIRE(restore.ok());CHECK_EQ(restore->action,app::StartupAction::RestoreTaskbar);
}
AETHER_TEST("app: recovery commands without a running instance never start desktop surfaces") {
    const auto name=L"AetherShell.CommandTest."+std::to_wstring(::GetCurrentProcessId());
    {
        app::CommandLine command;command.action=app::StartupAction::Quit;
        app::ShellApplication application;auto result=application.Run(command,{},name);
        REQUIRE(result.ok());CHECK_EQ(*result,0);
        CHECK_EQ(platform::InstanceChannel::FindListenerProcessId(name),DWORD{0});
    }
    // D21: without an instance, --restore-taskbar restores Windows itself (covered with a simulated
    // taskbar in TaskbarRecoveryTests). Without resolved data folders it refuses before touching any
    // taskbar or showing UI, so this test never reaches the real Windows taskbar.
    {
        app::CommandLine command;command.action=app::StartupAction::RestoreTaskbar;
        app::ShellApplication application;auto result=application.Run(command,{},name);
        REQUIRE_FALSE(result.ok());CHECK_EQ(result.error().code,ErrorCode::InvalidArgument);
        CHECK_EQ(platform::InstanceChannel::FindListenerProcessId(name),DWORD{0});
    }
}
