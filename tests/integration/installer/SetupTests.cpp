// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "install/Install.h"
#include "core/FileSystem.h"
#include "updates/Sha256.h"

#include <windows.h>
#include <filesystem>
#include <vector>
#include <chrono>
#include <thread>
#include <set>

using namespace aether;
namespace {
std::set<std::filesystem::path> preservedRoots;
struct IsolatedRoot {
    std::filesystem::path path=test::MakeTempDirectory("setup-isolated");
    ~IsolatedRoot(){if(preservedRoots.contains(path))return;std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
DWORD Setup(const std::filesystem::path& root,const std::filesystem::path& data,const std::vector<std::wstring>& extra={},const std::filesystem::path& executable=AETHER_SETUP_TEST_EXE) {
    std::wstring line=install::QuoteArgument(executable.wstring())+L" --silent --test-no-registration --no-shortcuts --no-launch --install-root "+install::QuoteArgument(root.wstring())+L" --data-dir "+install::QuoteArgument(data.wstring());
    for(const auto& argument:extra)line+=L" "+install::QuoteArgument(argument);
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    REQUIRE(::CreateProcessW(executable.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE);
    platform::UniqueHandle thread(process.hThread),child(process.hProcess);
    const auto wait=::WaitForSingleObject(child.Get(),120000);
    if(wait!=WAIT_OBJECT_0){preservedRoots.insert(root.parent_path());TEST_NOTE("Setup helper did not finish; isolated fixture retained to avoid deleting beneath the active child.");}
    REQUIRE_EQ(wait,DWORD{WAIT_OBJECT_0});
    DWORD code=999;REQUIRE(::GetExitCodeProcess(child.Get(),&code)!=FALSE);return code;
}
void CheckPayload(const install::InstallLayout& layout) {
    auto state=install::LoadState(layout);REQUIRE(state.ok());REQUIRE_FALSE(state->activeVersion.empty());
    REQUIRE(state->transaction.has_value());CHECK_EQ(state->transaction->state,install::TransactionState::Committed);
    auto folder=layout.Version(state->activeVersion);REQUIRE(folder.ok());
    auto bytes=fs::ReadFile(*folder/L"payload-manifest.json",65536);REQUIRE(bytes.ok());
    auto manifest=install::ParsePayloadManifest(*bytes);REQUIRE(manifest.ok());
    for(const auto& file:manifest->files){auto digest=updates::Sha256::HashFile(*folder/std::filesystem::path(file.name));REQUIRE(digest.ok());CHECK_EQ(*digest,file.sha256);CHECK_EQ(std::filesystem::file_size(*folder/std::filesystem::path(file.name)),file.size);}
    CHECK(std::filesystem::is_regular_file(layout.Launcher()));CHECK(std::filesystem::is_regular_file(layout.Maintenance()));
}
}
AETHER_TEST("setup: isolated silent install, repair and uninstall preserve user data") {
    IsolatedRoot fixture;const auto root=fixture.path/L"install";const auto data=fixture.path/L"data";
    REQUIRE(fs::EnsureDirectory(data).ok());REQUIRE(fs::WriteFileAtomic(data/L"settings.json","synthetic test settings").ok());
    REQUIRE_EQ(Setup(root,data),DWORD{0});auto layout=install::InstallLayout::Create(root);REQUIRE(layout.ok());CheckPayload(*layout);
    auto state=install::LoadState(*layout);REQUIRE(state.ok());auto folder=layout->Version(state->activeVersion);REQUIRE(folder.ok());
    REQUIRE(std::filesystem::remove(*folder/L"LICENSE"));
    REQUIRE_EQ(Setup(root,data,{L"--repair"}),DWORD{0});CheckPayload(*layout);
    CHECK_EQ(fs::ReadFile(data/L"settings.json",1024).value(),std::string("synthetic test settings"));
    REQUIRE_EQ(Setup(root,data,{L"--uninstall"}),DWORD{0});CHECK_FALSE(std::filesystem::exists(root));CHECK(std::filesystem::exists(data/L"settings.json"));
}
AETHER_TEST("setup: explicit removal deletes only isolated user data") {
    IsolatedRoot fixture;const auto root=fixture.path/L"install";const auto data=fixture.path/L"data";const auto outside=fixture.path/L"sentinel.txt";
    REQUIRE(fs::WriteFileAtomic(outside,"synthetic sibling").ok());REQUIRE(fs::EnsureDirectory(data).ok());REQUIRE(fs::WriteFileAtomic(data/L"settings.json","synthetic").ok());
    REQUIRE_EQ(Setup(root,data),DWORD{0});REQUIRE_EQ(Setup(root,data,{L"--uninstall",L"--remove-user-data"}),DWORD{0});
    CHECK_FALSE(std::filesystem::exists(root));CHECK_FALSE(std::filesystem::exists(data));CHECK_EQ(fs::ReadFile(outside,1024).value(),std::string("synthetic sibling"));
}
AETHER_TEST("setup: malformed arguments fail without creating installation") {
    IsolatedRoot fixture;const auto root=fixture.path/L"install";const auto data=fixture.path/L"data";
    CHECK_EQ(Setup(root,data,{L"--unknown"}),DWORD{2});CHECK_FALSE(std::filesystem::exists(root));
}

AETHER_TEST("setup: installed maintenance relocates before removing its own root") {
    IsolatedRoot fixture;const auto root=fixture.path/L"install";const auto data=fixture.path/L"data";
    REQUIRE_EQ(Setup(root,data),DWORD{0});auto layout=install::InstallLayout::Create(root);REQUIRE(layout.ok());
    REQUIRE_EQ(Setup(root,data,{L"--uninstall"},layout->Maintenance()),DWORD{0});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(std::filesystem::exists(root) && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(25));
    CHECK_FALSE(std::filesystem::exists(root));
}
