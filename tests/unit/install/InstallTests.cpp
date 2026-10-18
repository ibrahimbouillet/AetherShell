// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "install/Install.h"
#include "install/InstallInternal.h"
#include "core/FileSystem.h"
#include "core/Json.h"

#include <shellapi.h>
#include <thread>
#include <cwctype>

using namespace aether;
using namespace aether::install;
using namespace std::chrono_literals;

namespace {
json::Value Payload(std::string name="AetherShellApp.exe") {
    auto file=json::Value::MakeObject(); file.Set("name",name); file.Set("resource","FILE_APP"); file.Set("size",12);
    const auto hash=updates::Sha256::Hash("payload data"); file.Set("sha256",updates::ToHex(hash.value()));
    auto files=json::Value::MakeArray(); files.Append(std::move(file)); auto value=json::Value::MakeObject();
    value.Set("schemaVersion",1); value.Set("product","AetherShell"); value.Set("version","1.0.0"); value.Set("architecture","x64"); value.Set("files",std::move(files)); return value;
}
InstallState Pending(TransactionState stage=TransactionState::Prepared) {
    InstallState state; state.activeVersion="1.0.0"; state.previousVersion="0.9.0";
    state.transaction=InstallTransaction{"0123456789abcdef0123456789abcdef","1.0.0","2.0.0","",stage,"2026-01-01T00:00:00Z","2026-01-01T00:00:00Z",""}; return state;
}
}

AETHER_TEST("install layout: rejects relative roots and traversal versions") {
    CHECK_FALSE(InstallLayout::Create(L"relative").ok()); CHECK_FALSE(InstallLayout::Create(L"C:\\").ok());
    const auto root=test::MakeTempDirectory("install-layout"); auto layout=InstallLayout::Create(root); REQUIRE(layout.ok());
    CHECK(layout->Version("1.2.3-alpha.1+build.2").ok());
    for(const auto* bad:{"../outside","..","CON","1.0.0/../other","1.0.0:stream","v1.0.0"}) CHECK_FALSE(layout->Version(bad).ok());
    CHECK_FALSE(ValidateSeparateRoots(layout.value(),root/L"data").ok());CHECK_FALSE(ValidateSeparateRoots(layout.value(),root.parent_path()).ok());
    auto alias=root.native();for(auto& c:alias)c=static_cast<wchar_t>(::towupper(c));CHECK_FALSE(ValidateSeparateRoots(layout.value(),alias).ok());
    CHECK(ValidateSeparateRoots(layout.value(),root.parent_path()/(root.filename().native()+L"-data")).ok());
    std::filesystem::remove_all(root);
}
AETHER_TEST("install journal: strict roundtrip and legal transitions") {
    const auto root=test::MakeTempDirectory("install-state"); auto layout=InstallLayout::Create(root); REQUIRE(layout.ok());
    auto state=Pending(); REQUIRE(SaveState(layout.value(),state).ok()); auto loaded=LoadState(layout.value()); REQUIRE(loaded.ok());
    CHECK_EQ(loaded->activeVersion,std::string("1.0.0")); CHECK_EQ(loaded->transaction->toVersion,std::string("2.0.0"));
    CHECK_FALSE(Transition(state,TransactionState::Installed).ok());
    for(const auto next:{TransactionState::Verified,TransactionState::Staged,TransactionState::Applying,TransactionState::Installed,TransactionState::HealthCheckPending,TransactionState::Committed}) REQUIRE(Transition(state,next).ok());
    CHECK_FALSE(Transition(state,TransactionState::RollbackRequired).ok());
    auto bytes=fs::ReadFile(layout->Journal(),64*1024); REQUIRE(bytes.ok()); auto json=json::Parse(bytes.value()); REQUIRE(json.ok());
    json->Set("unknown",true); CHECK_FALSE(ParseState(json::Serialize(json.value())).ok());
    json->AsObject()->pop_back(); json->Find("transaction")->Set("healthToken",false); CHECK_FALSE(ParseState(json::Serialize(json.value())).ok());
    CHECK_FALSE(ParseState("{}").ok());
    std::filesystem::remove_all(root);
}
AETHER_TEST("install payload: traversal aliases duplicate names and bounds fail closed") {
    REQUIRE(ParsePayloadManifest(json::Serialize(Payload())).ok());
    for(const auto* name:{"../escape.exe","a/../../escape","/absolute","C:/absolute","a:stream","NUL.exe","COM1","LPT9.txt","a/CON","trailing.","trailing ","a//b","a\\\\b","payload-manifest.json"}) CHECK_FALSE(ParsePayloadManifest(json::Serialize(Payload(name))).ok());
    auto duplicate=Payload("folder/file.exe"); auto* files=duplicate.Find("files")->AsArray(); auto second=files->front(); second.Set("name","FOLDER\\FILE.EXE"); second.Set("resource","SECOND"); files->push_back(second);
    CHECK_FALSE(ParsePayloadManifest(json::Serialize(duplicate)).ok());
    auto large=Payload(); large.Find("files")->AsArray()->front().Set("size",updates::kMaxInstallerBytes+1); CHECK_FALSE(ParsePayloadManifest(json::Serialize(large)).ok());
    auto wrong=Payload(); wrong.Set("architecture","unknown"); CHECK_FALSE(ParsePayloadManifest(json::Serialize(wrong)).ok());
}
AETHER_TEST("install recovery: each preactivation state keeps active and previous versions") {
    for(const auto stage:{TransactionState::Prepared,TransactionState::Verified,TransactionState::Staged,TransactionState::Applying}) {
        const auto root=test::MakeTempDirectory("install-recovery"); auto layout=InstallLayout::Create(root); REQUIRE(layout.ok());
        auto active=layout->Version("1.0.0"), previous=layout->Version("0.9.0"); REQUIRE(active.ok()); REQUIRE(previous.ok());
        REQUIRE(fs::WriteFileAtomic(active.value()/L"AetherShellApp.exe","active fixture").ok());
        REQUIRE(fs::WriteFileAtomic(previous.value()/L"AetherShellApp.exe","previous fixture").ok());
        auto state=Pending(stage); REQUIRE(SaveState(layout.value(),state).ok()); REQUIRE(RecoverInterrupted(layout.value()).ok());
        auto recovered=LoadState(layout.value()); REQUIRE(recovered.ok()); CHECK_EQ(recovered->activeVersion,std::string("1.0.0")); CHECK_FALSE(recovered->transaction.has_value());
        CHECK(std::filesystem::exists(active.value()/L"AetherShellApp.exe")); CHECK(std::filesystem::exists(previous.value()/L"AetherShellApp.exe"));
        std::filesystem::remove_all(root);
    }
}
AETHER_TEST("install commit: active and previous versions are never removed") {
    const auto root=test::MakeTempDirectory("install-commit"); auto layout=InstallLayout::Create(root); REQUIRE(layout.ok());
    for(const auto version:{"0.9.0","1.0.0","2.0.0"}) { auto folder=layout->Version(version); REQUIRE(folder.ok()); REQUIRE(fs::WriteFileAtomic(folder.value()/L"AetherShellApp.exe","fixture").ok()); }
    auto state=Pending(TransactionState::Installed); state.activeVersion="2.0.0"; state.previousVersion="1.0.0";
    REQUIRE(SaveState(layout.value(),state).ok()); REQUIRE(CommitUpdate(layout.value(),state).ok());
    CHECK(std::filesystem::exists(layout->Version("2.0.0").value())); CHECK(std::filesystem::exists(layout->Version("1.0.0").value())); CHECK_FALSE(std::filesystem::exists(layout->Version("0.9.0").value()));
    std::filesystem::remove_all(root);
}
AETHER_TEST("install recovery: installed pending and rollback-required restore previous version idempotently") {
    for(const auto stage:{TransactionState::Installed,TransactionState::HealthCheckPending,TransactionState::RollbackRequired}) {
        const auto root=test::MakeTempDirectory("install-recover-active"); auto layout=InstallLayout::Create(root); REQUIRE(layout.ok());
        for(const auto version:{"1.0.0","2.0.0"}) REQUIRE(fs::WriteFileAtomic(layout->Version(version).value()/L"AetherShellApp.exe","fixture").ok());
        auto state=Pending(stage); state.activeVersion="2.0.0"; state.previousVersion="1.0.0";
        REQUIRE(SaveState(layout.value(),state).ok()); REQUIRE(RecoverInterrupted(layout.value()).ok()); REQUIRE(RecoverInterrupted(layout.value()).ok());
        auto recovered=LoadState(layout.value()); REQUIRE(recovered.ok()); CHECK_EQ(recovered->activeVersion,std::string("1.0.0")); CHECK_EQ(recovered->transaction->state,TransactionState::RolledBack);
        CHECK(std::filesystem::exists(layout->Version("1.0.0").value())); CHECK_FALSE(std::filesystem::exists(layout->Version("2.0.0").value()));
        std::filesystem::remove_all(root);
    }
}
AETHER_TEST("install recovery: interrupted first installation remains repairable without a previous version") {
    const auto root=test::MakeTempDirectory("install-recover-first"); auto layout=InstallLayout::Create(root); REQUIRE(layout.ok());
    auto state=Pending(TransactionState::Installed); state.activeVersion="2.0.0"; state.previousVersion.clear(); state.transaction->fromVersion.clear();
    REQUIRE(fs::WriteFileAtomic(layout->Version("2.0.0").value()/L"AetherShellApp.exe","fixture").ok()); REQUIRE(SaveState(layout.value(),state).ok());
    REQUIRE(RecoverInterrupted(layout.value()).ok()); auto recovered=LoadState(layout.value()); REQUIRE(recovered.ok());
    CHECK(recovered->activeVersion.empty()); CHECK_FALSE(recovered->transaction.has_value()); CHECK_FALSE(std::filesystem::exists(layout->Version("2.0.0").value()));
    std::filesystem::remove_all(root);
}
AETHER_TEST("install health: random bounded tokens, success timeout and collisions") {
    auto token=MakeHealthToken(); REQUIRE(token.ok()); CHECK_EQ(token->size(),32u); CHECK(IsHealthToken(token.value()));
    CHECK_FALSE(IsHealthToken("not a token")); CHECK_FALSE(IsHealthToken(std::string(65,'a')));
    auto health=HealthCheck::Create(token.value()); REQUIRE(health.ok()); CHECK_FALSE(HealthCheck::Create(token.value()).ok());
    auto timed=health->Wait(0ms); REQUIRE(timed.ok()); CHECK_FALSE(timed.value());
    REQUIRE(SignalHealthy(token.value()).ok()); auto ready=health->Wait(50ms); REQUIRE(ready.ok()); CHECK(ready.value()); CHECK_FALSE(health->Wait(-1ms).ok());
}
AETHER_TEST("install lock: Windows case aliases and normalized paths share one lifecycle lock") {
    const auto root=test::MakeTempDirectory("install-lock"); auto layout=InstallLayout::Create(root/L"Installation"); REQUIRE(layout.ok());
    auto held=install::detail::InstallLock::Acquire(layout.value()); REQUIRE(held.ok());
    auto alias=layout->root.native(); for(auto& c:alias) c=static_cast<wchar_t>(::towupper(c));
    auto aliasLayout=InstallLayout::Create(std::filesystem::path(alias)/L"ignored"/L".."); REQUIRE(aliasLayout.ok());
    bool refused=false,recoveryRefused=false,launchRefused=false;
    std::thread other([&]{auto second=install::detail::InstallLock::Acquire(aliasLayout.value()); refused=!second && second.error().code==ErrorCode::Unavailable;auto recovered=RecoverInterrupted(aliasLayout.value());recoveryRefused=!recovered && recovered.error().code==ErrorCode::Unavailable;auto launched=LaunchActiveVersion(aliasLayout.value());launchRefused=!launched && launched.error().code==ErrorCode::Unavailable;}); other.join();
    CHECK(refused);CHECK(recoveryRefused);CHECK(launchRefused);std::filesystem::remove_all(root);
}
AETHER_TEST("install argv: quoting roundtrips spaces quotes empty strings and trailing slashes") {
    const std::vector<std::wstring> expected{L"",L"plain",L"with space",L"a\"quoted",L"trailing\\",L"space and trailing\\",L"\\\""};
    std::wstring command=L"launcher"; for(const auto& value:expected) command+=L" "+QuoteArgument(value);
    int count=0; auto* argv=::CommandLineToArgvW(command.c_str(),&count); REQUIRE(argv!=nullptr);
    CHECK_EQ(count,static_cast<int>(expected.size()+1)); for(std::size_t i=0;i<expected.size();++i) CHECK_EQ(std::wstring(argv[i+1]),expected[i]); ::LocalFree(argv);
}
