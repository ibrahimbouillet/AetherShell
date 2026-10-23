// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "install/LifecyclePolicy.h"
#include "install/LauncherSupervisor.h"
#include "core/FileSystem.h"
#include <future>
#include <thread>
using namespace aether;
using namespace std::chrono_literals;

AETHER_TEST("install lifecycle: fresh disclosed wizard defaults on but existing disables win") {
    using settings::StartupState;
    CHECK(install::SetupStartupDefault(true,false,std::nullopt,StartupState::Disabled));
    CHECK_FALSE(install::SetupStartupDefault(false,false,std::nullopt,StartupState::Disabled));
    CHECK_FALSE(install::SetupStartupDefault(true,true,false,StartupState::Enabled));
    CHECK_FALSE(install::SetupStartupDefault(true,true,true,StartupState::Disabled));
    CHECK(install::SetupStartupDefault(true,true,true,StartupState::Enabled));
    CHECK_FALSE(install::SetupStartupDefault(true,false,std::nullopt,StartupState::DisabledByUser));
    CHECK_FALSE(install::SetupStartupDefault(true,false,std::nullopt,StartupState::UnknownApproval));
    CHECK_FALSE(install::SetupStartupDefault(true,false,std::nullopt,StartupState::DifferentTarget));
}

AETHER_TEST("install lifecycle: exception restarts have a lifetime budget and bounded backoff") {
    install::CrashRestartPolicy policy;
    CHECK_FALSE(policy.NextDelay(0));CHECK_FALSE(policy.NextDelay(1));CHECK_FALSE(policy.NextDelay(5));
    CHECK_FALSE(policy.NextDelay(0xC000013Au));
    CHECK_EQ(policy.NextDelay(0xC0000005u),std::optional(1000ms));
    CHECK_EQ(policy.NextDelay(0xE06D7363u),std::optional(2000ms));
    CHECK_EQ(policy.NextDelay(0xC0000409u),std::optional(4000ms));
    CHECK_FALSE(policy.NextDelay(0xC0000005u));
}

AETHER_TEST("install lifecycle: changed or removed active installation never restarts") {
    install::CrashRestartPolicy policy;
    CHECK_FALSE(policy.NextDelay(0xC0000005u,false));
    CHECK_EQ(policy.NextDelay(0xC0000005u,true),std::optional(1000ms));
}

AETHER_TEST("install lifecycle: installation-scoped stop interrupts fake supervisor backoff") {
    const auto root=test::MakeTempDirectory("launcher-stop");
    auto layout=install::InstallLayout::Create(root/L"installation");REQUIRE(layout.ok());
    auto other=install::InstallLayout::Create(root/L"other");REQUIRE(other.ok());
    std::promise<bool> acquired;auto ready=acquired.get_future();bool cancelled=false;
    std::thread worker([&] {
        auto supervisor=install::LauncherSupervisor::Acquire(*layout);acquired.set_value(supervisor.ok());
        if(supervisor){auto stopped=supervisor->StopRequested(4s);cancelled=stopped && *stopped;}
    });
    const bool owner=ready.get();
    if(owner) {
        CHECK(install::StopLauncherSupervisor(*other,0ms).ok());
        CHECK_FALSE(install::LauncherSupervisor::Acquire(*layout).ok());
        CHECK(install::StopLauncherSupervisor(*layout,2s).ok());
    }
    worker.join();CHECK(owner);CHECK(cancelled);
    CHECK(install::StopLauncherSupervisor(*layout,0ms).ok());
    auto restarted=install::LauncherSupervisor::Acquire(*layout);REQUIRE(restarted.ok());
    auto clear=restarted->StopRequested(0ms);REQUIRE(clear.ok());CHECK_FALSE(*clear);
    std::error_code ignored;std::filesystem::remove_all(root,ignored);
}

AETHER_TEST("install lifecycle: control cancels a fake supervisor without blocking its initial launch") {
    const auto root=test::MakeTempDirectory("launcher-control");
    auto layout=install::InstallLayout::Create(root/L"installation");REQUIRE(layout.ok());
    std::promise<bool> acquired;auto ready=acquired.get_future();bool launchRefused=false;
    std::thread worker([&] {
        auto supervisor=install::LauncherSupervisor::Acquire(*layout);acquired.set_value(supervisor.ok());
        if(supervisor) {
            auto stop=supervisor->StopRequested(3s);
            if(stop && *stop) {
                // The control thread holds the lifecycle lock. No executable can be started.
                auto launch=install::LaunchActiveVersionProcess(*layout);
                launchRefused=!launch && launch.error().code==ErrorCode::Unavailable;
            }
        }
    });
    const bool owner=ready.get();
    if(owner) {
        auto control=install::LaunchControlCommand(*layout,{L"--quit"});
        CHECK_FALSE(control.ok());if(!control)CHECK_EQ(control.error().code,ErrorCode::NotFound);
    }
    worker.join();CHECK(owner);CHECK(launchRefused);
    std::error_code ignored;std::filesystem::remove_all(root,ignored);
}

AETHER_TEST("install lifecycle: changed journal is rejected before recovery or child launch") {
    const auto root=test::MakeTempDirectory("launcher-identity");
    auto layout=install::InstallLayout::Create(root/L"installation");REQUIRE(layout.ok());
    install::InstallState state;state.activeVersion="1.0.0";REQUIRE(install::SaveState(*layout,state).ok());
    auto before=fs::ReadFile(layout->Journal(),64*1024);REQUIRE(before.ok());
    auto launch=install::LaunchActiveVersionProcess(*layout,{},updates::Sha256Digest{});
    CHECK_FALSE(launch.ok());if(!launch)CHECK_EQ(launch.error().code,ErrorCode::Unavailable);
    CHECK_EQ(*fs::ReadFile(layout->Journal(),64*1024),*before);
    std::error_code ignored;std::filesystem::remove_all(root,ignored);
}
