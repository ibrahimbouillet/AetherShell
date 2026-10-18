// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "install/Install.h"
#include "install/InstallInternal.h"
#include "core/FileSystem.h"
#include "core/Strings.h"
#include "settings/StartupRegistration.h"

#include <array>

using namespace aether;
using namespace aether::install;
using namespace std::chrono_literals;
namespace {
class FakeProcesses final:public IProcessOperations {
public:
    int exits=0,launches=0,stops=0; bool healthy=true;
    CancellationSource* cancelOnExit=nullptr;
    std::vector<std::filesystem::path> launched;
    std::vector<std::vector<std::wstring>> arguments;
    Result<void> RequestExit(std::chrono::milliseconds) override { ++exits; if(cancelOnExit) cancelOnExit->Cancel(); return Ok(); }
    Result<DWORD> Launch(const std::filesystem::path& file,const std::vector<std::wstring>& args) override { ++launches; launched.push_back(file); arguments.push_back(args); return DWORD{42}; }
    Result<bool> WaitForHealth(const HealthCheck&,std::chrono::milliseconds) override { return healthy; }
    Result<void> StopUnhealthyChild(DWORD) override { ++stops; return Ok(); }
};
std::filesystem::path Package(std::wstring_view name=L"aether_install_payload_fixture.dll") {
    std::array<wchar_t,32768> file{}; const auto size=::GetModuleFileNameW(nullptr,file.data(),static_cast<DWORD>(file.size()));
    if(!size || size>=file.size()) throw std::runtime_error("cannot find own test executable"); return std::filesystem::path(file.data()).parent_path()/name;
}
InstallOptions Options(FakeProcesses& processes) { InstallOptions options; options.registerInstallation=false; options.startMenuShortcut=false; options.desktopShortcut=false; options.processes=&processes; return options; }
}
AETHER_TEST("install integration: payload stages activates repairs and uninstalls only temporary roots") {
    const auto temp=test::MakeTempDirectory("install-integration"); const auto root=temp/L"installation",data=temp/L"data";
    FakeProcesses processes; const auto options=Options(processes); auto layout=InstallLayout::Create(root); REQUIRE(layout.ok());
    REQUIRE(fs::WriteFileAtomic(data/L"config"/L"settings.json","user fixture configuration").ok());
    auto payload=ReadPayloadManifest(Package()); REQUIRE(payload.ok()); CHECK_EQ(payload->files.size(),5u);
    REQUIRE(InstallPackage(Package(),layout.value(),options).ok());
    auto state=LoadState(layout.value()); REQUIRE(state.ok()); CHECK_EQ(state->activeVersion,std::string("1.0.0")); CHECK_EQ(state->transaction->state,TransactionState::Committed);
    CHECK(std::filesystem::exists(layout->Launcher())); CHECK(std::filesystem::exists(layout->Maintenance()));
    auto version=layout->Version("1.0.0"); REQUIRE(version.ok());
    for(const auto& file:payload->files) { auto hash=updates::Sha256::HashFile(version.value()/file.name); REQUIRE(hash.ok()); CHECK_EQ(hash.value(),file.sha256); }
    REQUIRE(std::filesystem::remove(version.value()/L"AetherShellApp.exe"));
    REQUIRE(RepairPackage(Package(),layout.value(),options).ok()); CHECK(std::filesystem::exists(version.value()/L"AetherShellApp.exe"));
    REQUIRE(Uninstall(layout.value(),data,false,options).ok()); CHECK_FALSE(std::filesystem::exists(root)); CHECK(std::filesystem::exists(data/L"config"/L"settings.json"));
    CHECK_EQ(processes.launches,0); CHECK_EQ(processes.stops,0); CHECK_EQ(processes.exits,3);
    std::filesystem::remove_all(temp);
}
AETHER_TEST("install integration: resource digest mismatch never creates active installation") {
    const auto temp=test::MakeTempDirectory("install-integrity"); auto layout=InstallLayout::Create(temp/L"installation"); REQUIRE(layout.ok());
    InstallState state; auto staged=StagePackage(Package(L"aether_install_invalid_fixture.dll"),layout.value(),state);
    CHECK_FALSE(staged.ok()); CHECK_EQ(staged.error().code,ErrorCode::Integrity); CHECK(state.activeVersion.empty());
    REQUIRE(RecoverInterrupted(layout.value()).ok()); CHECK_FALSE(std::filesystem::exists(layout->root/L"versions"));
    std::filesystem::remove_all(temp);
}
AETHER_TEST("install integration: healthy update commits and keeps previous version") {
    const auto temp=test::MakeTempDirectory("install-update-success"); auto layout=InstallLayout::Create(temp/L"installation"); REQUIRE(layout.ok());
    FakeProcesses processes; const auto options=Options(processes); REQUIRE(InstallPackage(Package(),layout.value(),options).ok());
    const auto version=updates::SemanticVersion::Parse("2.0.0"); REQUIRE(version.ok());
    REQUIRE(install::detail::ApplyVerifiedUpdate(Package(L"aether_install_upgrade_fixture.dll"),version.value(),layout.value(),temp/L"data",processes,0ms).ok());
    auto state=LoadState(layout.value()); REQUIRE(state.ok()); CHECK_EQ(state->activeVersion,std::string("2.0.0")); CHECK_EQ(state->previousVersion,std::string("1.0.0"));
    CHECK_EQ(state->transaction->state,TransactionState::Committed); CHECK_EQ(processes.stops,0); CHECK_EQ(processes.launches,1);
    CHECK_EQ(processes.arguments.front().at(2),std::wstring(L"--data-dir")); CHECK_EQ(processes.arguments.front().at(3),(temp/L"data").native());
    CHECK(std::filesystem::exists(layout->Version("1.0.0").value()/L"AetherShellApp.exe"));
    REQUIRE(RecoverInterrupted(layout.value()).ok()); CHECK_EQ(LoadState(layout.value())->activeVersion,std::string("2.0.0"));
    std::filesystem::remove_all(temp);
}
AETHER_TEST("install integration: failed health stops only injected child and restores previous version") {
    const auto temp=test::MakeTempDirectory("install-update-rollback"); auto layout=InstallLayout::Create(temp/L"installation"); REQUIRE(layout.ok());
    FakeProcesses processes; const auto options=Options(processes); REQUIRE(InstallPackage(Package(),layout.value(),options).ok()); processes.healthy=false;
    const auto version=updates::SemanticVersion::Parse("2.0.0"); REQUIRE(version.ok());
    const auto updated=install::detail::ApplyVerifiedUpdate(Package(L"aether_install_upgrade_fixture.dll"),version.value(),layout.value(),temp/L"data",processes,0ms);
    CHECK_FALSE(updated.ok()); CHECK_EQ(updated.error().code,ErrorCode::Timeout);
    auto state=LoadState(layout.value()); REQUIRE(state.ok()); CHECK_EQ(state->activeVersion,std::string("1.0.0")); CHECK_EQ(state->transaction->state,TransactionState::RolledBack);
    CHECK_EQ(processes.stops,1); CHECK_EQ(processes.launches,2); CHECK_EQ(processes.launched.back(),layout->Version("1.0.0").value()/L"AetherShellApp.exe");
    CHECK_EQ(processes.arguments.back(),(std::vector<std::wstring>{L"--data-dir",(temp/L"data").native()}));
    CHECK_FALSE(std::filesystem::exists(layout->Version("2.0.0").value())); REQUIRE(RecoverInterrupted(layout.value()).ok());
    std::filesystem::remove_all(temp);
}
AETHER_TEST("install integration: invalid signatures fail before any shutdown or active mutation") {
    const auto temp=test::MakeTempDirectory("install-update-signature"); auto layout=InstallLayout::Create(temp/L"installation"); REQUIRE(layout.ok());
    FakeProcesses processes; const auto options=Options(processes); REQUIRE(InstallPackage(Package(),layout.value(),options).ok()); const auto exits=processes.exits;
    REQUIRE(fs::WriteFileAtomic(temp/L"manifest.json","{}").ok()); REQUIRE(fs::WriteFileAtomic(temp/L"manifest.json.sig","invalid signature").ok());
    CHECK_FALSE(ApplyUpdate(Package(L"aether_install_upgrade_fixture.dll"),temp/L"manifest.json",temp/L"manifest.json.sig",layout.value(),temp/L"data",&processes,0ms).ok());
    CHECK_EQ(processes.exits,exits); CHECK_EQ(processes.launches,0); CHECK_EQ(processes.stops,0); CHECK_EQ(LoadState(layout.value())->activeVersion,std::string("1.0.0"));
    std::filesystem::remove_all(temp);
}
AETHER_TEST("install integration: cancellation before activation preserves active bytes and unrelated directories") {
    const auto temp=test::MakeTempDirectory("install-cancel"); auto layout=InstallLayout::Create(temp/L"installation"); REQUIRE(layout.ok());
    FakeProcesses processes; auto options=Options(processes); REQUIRE(InstallPackage(Package(),layout.value(),options).ok());
    auto before=updates::Sha256::HashFile(layout->Version("1.0.0").value()/L"AetherShellApp.exe"); REQUIRE(before.ok());
    CancellationSource cancel; options.cancellation=cancel.Token(); processes.cancelOnExit=&cancel;
    const auto result=InstallPackage(Package(L"aether_install_upgrade_fixture.dll"),layout.value(),options);
    CHECK_FALSE(result.ok()); CHECK_EQ(result.error().code,ErrorCode::Cancelled); CHECK_EQ(LoadState(layout.value())->activeVersion,std::string("1.0.0"));
    CHECK_EQ(updates::Sha256::HashFile(layout->Version("1.0.0").value()/L"AetherShellApp.exe").value(),before.value());
    CHECK_FALSE(std::filesystem::exists(layout->Version("2.0.0").value())); CHECK(std::filesystem::is_empty(layout->root/L"staging"));
    auto unrelated=InstallLayout::Create(temp/L"unrelated"); REQUIRE(unrelated.ok()); REQUIRE(fs::WriteFileAtomic(unrelated->root/L"keep.txt","unrelated fixture").ok());
    CHECK_FALSE(InstallPackage(Package(),unrelated.value(),Options(processes)).ok()); CHECK_EQ(fs::ReadFile(unrelated->root/L"keep.txt",64).value(),std::string("unrelated fixture"));
    std::filesystem::remove_all(temp);
}
AETHER_TEST("install integration: registration uses isolated test keys and removes only this installation") {
    const auto temp=test::MakeTempDirectory("install-registry");auto layout=InstallLayout::Create(temp/L"installation");REQUIRE(layout.ok());
    FakeProcesses processes;auto options=Options(processes);REQUIRE(InstallPackage(Package(),layout.value(),options).ok());
    auto token=MakeHealthToken();REQUIRE(token.ok());
    struct TestKeys {std::wstring root;~TestKeys(){::RegDeleteTreeW(HKEY_CURRENT_USER,root.c_str());}} keys{L"Software\\AetherShellTests\\Install-"+aether::text::Widen(token.value())};
    options.registerInstallation=true;options.startWithWindows=true;
    options.registry={keys.root+L"\\Uninstall",keys.root+L"\\Run",keys.root+L"\\Approval"};
    REQUIRE(RegisterInstallation(layout.value(),"1.0.0",options).ok());
    std::array<wchar_t,128> publisher{};DWORD bytes=static_cast<DWORD>(publisher.size()*sizeof(wchar_t));
    CHECK_EQ(::RegGetValueW(HKEY_CURRENT_USER,options.registry.uninstall.c_str(),L"Publisher",RRF_RT_REG_SZ,nullptr,publisher.data(),&bytes),LSTATUS{ERROR_SUCCESS});
    CHECK_EQ(std::wstring(publisher.data()),std::wstring(L"Ibrahim Bouillet"));
    auto registry=settings::MakeWindowsStartupRegistry({options.registry.run,options.registry.startupApproval});REQUIRE(registry.ok());
    auto enabled=settings::StartupRegistration(registry.value()).IsEnabled(layout->Launcher());REQUIRE(enabled.ok());CHECK_EQ(enabled.value(),settings::StartupState::Enabled);
    // Direct registration removal respects the false shortcut options; Uninstall separately
    // enables removal of both owned project links. This case touches only isolated test keys.
    REQUIRE(RemoveRegistration(layout.value(),options).ok());
    auto value=registry.value()->ReadRun();REQUIRE(value.ok());CHECK_FALSE(value->has_value());
    HKEY absent=nullptr;CHECK_EQ(::RegOpenKeyExW(HKEY_CURRENT_USER,options.registry.uninstall.c_str(),0,KEY_QUERY_VALUE,&absent),LSTATUS{ERROR_FILE_NOT_FOUND});if(absent)::RegCloseKey(absent);
    std::filesystem::remove_all(temp);
}
