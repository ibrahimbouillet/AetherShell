// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "install/Install.h"
#include "core/FileSystem.h"
#include "platform/windows/ComApartment.h"
#include "updates/Sha256.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winrt/base.h>
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
DWORD Setup(const std::filesystem::path& root,const std::filesystem::path& data,const std::vector<std::wstring>& extra={},const std::filesystem::path& executable=AETHER_SETUP_TEST_EXE,bool noShortcuts=true) {
    std::wstring line=install::QuoteArgument(executable.wstring())+L" --silent --test-no-registration"+(noShortcuts?L" --no-shortcuts":L"")+L" --no-launch --install-root "+install::QuoteArgument(root.wstring())+L" --data-dir "+install::QuoteArgument(data.wstring());
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

namespace {
struct ShortcutTarget { std::wstring path, arguments; };
ShortcutTarget ReadShortcut(const std::filesystem::path& file) {
    platform::ComApartment apartment;REQUIRE(apartment.IsUsable());
    winrt::com_ptr<IShellLinkW> link;REQUIRE(SUCCEEDED(::CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(link.put()))));
    REQUIRE(SUCCEEDED(link.as<IPersistFile>()->Load(file.c_str(),STGM_READ)));
    std::vector<wchar_t> path(32768),arguments(32768);
    REQUIRE(SUCCEEDED(link->GetPath(path.data(),static_cast<int>(path.size()),nullptr,SLGP_RAWPATH)));
    REQUIRE(SUCCEEDED(link->GetArguments(arguments.data(),static_cast<int>(arguments.size()))));
    return {path.data(),arguments.data()};
}
}
AETHER_TEST("setup: the Restore Windows taskbar shortcut is created in an isolated folder and removed on uninstall") {
    // The test-only folder replaces the user's Start menu; nothing is registered, launched or run.
    IsolatedRoot fixture;const auto root=fixture.path/L"install";const auto data=fixture.path/L"data";const auto menu=fixture.path/L"start-menu";
    REQUIRE(fs::EnsureDirectory(menu).ok());
    const auto shortcut=menu/L"Restore Windows taskbar.lnk";
    REQUIRE_EQ(Setup(root,data,{L"--test-shortcut-dir",menu.native()},AETHER_SETUP_TEST_EXE,false),DWORD{0});
    auto layout=install::InstallLayout::Create(root);REQUIRE(layout.ok());CheckPayload(*layout);
    REQUIRE(std::filesystem::is_regular_file(shortcut));
    const auto target=ReadShortcut(shortcut);
    CHECK(::_wcsicmp(target.path.c_str(),layout->Launcher().c_str())==0);
    CHECK_EQ(target.arguments,std::wstring(L"--restore-taskbar"));
    CHECK_FALSE(std::filesystem::exists(menu/L"Aether Shell.lnk"));  // registration shortcuts stay off in tests
    CHECK_FALSE(std::filesystem::exists(data/L"config"/L"settings.json"));  // setup does not write the choice
    // A foreign file with the same name is neither replaced nor removed.
    const auto foreignMenu=fixture.path/L"foreign-menu";REQUIRE(fs::EnsureDirectory(foreignMenu).ok());
    REQUIRE(fs::WriteFileAtomic(foreignMenu/L"Restore Windows taskbar.lnk","synthetic foreign file").ok());
    REQUIRE_EQ(Setup(root,data,{L"--repair",L"--test-shortcut-dir",foreignMenu.native()},AETHER_SETUP_TEST_EXE,false),DWORD{0});
    CHECK_EQ(fs::ReadFile(foreignMenu/L"Restore Windows taskbar.lnk",1024).value(),std::string("synthetic foreign file"));
    REQUIRE_EQ(Setup(root,data,{L"--uninstall",L"--test-shortcut-dir",menu.native()},AETHER_SETUP_TEST_EXE,false),DWORD{0});
    CHECK_FALSE(std::filesystem::exists(root));CHECK_FALSE(std::filesystem::exists(shortcut));
}
AETHER_TEST("setup: the isolated shortcut folder requires the isolation flag and an absolute path") {
    IsolatedRoot fixture;const auto root=fixture.path/L"install";const auto data=fixture.path/L"data";
    CHECK_EQ(Setup(root,data,{L"--test-shortcut-dir",L"relative-menu"},AETHER_SETUP_TEST_EXE,false),DWORD{2});
    CHECK_EQ(Setup(root,data,{L"--test-shortcut-dir"},AETHER_SETUP_TEST_EXE,false),DWORD{2});
    CHECK_FALSE(std::filesystem::exists(root));
}