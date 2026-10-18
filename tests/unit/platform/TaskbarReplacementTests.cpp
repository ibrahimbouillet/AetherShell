// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "core/FileSystem.h"
#include "platform/windows/TaskbarReplacement.h"
#include <shellapi.h>
#include <memory>
#include <string>

using namespace aether;
using namespace aether::platform;
namespace {
HWND Id(std::uintptr_t value) { return reinterpret_cast<HWND>(value); }
class FakeTaskbars final : public ITaskbarBackend {
public:
    unsigned state = ABS_ALWAYSONTOP;
    std::vector<TaskbarWindowState> windows{{Id(11),true},{Id(12),false}};
    bool failState = false, failHide = false, failRestoreOnce = false;
    int writes = 0;
    Result<unsigned> State() override { return state; }
    Result<void> SetState(unsigned value) override {
        if (failState) return MakeError(ErrorCode::Unavailable,"synthetic refusal");
        state=value; ++writes; return Ok();
    }
    Result<std::vector<TaskbarWindowState>> Windows() override { return windows; }
    Result<void> SetVisible(HWND window,bool visible) override {
        for(const auto& item:windows)if(item.window==window && item.visible==visible)return Ok();
        if ((!visible && failHide) || (visible && failRestoreOnce)) { failRestoreOnce=false; return MakeError(ErrorCode::Unavailable,"synthetic visibility refusal"); }
        for(auto& item:windows)if(item.window==window)item.visible=visible;
        return Ok();
    }
};
}
AETHER_TEST("taskbar lease: hides the taskbars, then restores state and shows every taskbar") {
    FakeTaskbars backend; TaskbarVisibilityLease lease(backend);
    REQUIRE(lease.Begin().ok()); CHECK_EQ(backend.state,unsigned(ABS_ALWAYSONTOP|ABS_AUTOHIDE));
    CHECK_FALSE(backend.windows[0].visible); CHECK_FALSE(backend.windows[1].visible);
    REQUIRE(lease.End().ok()); CHECK_EQ(backend.state,unsigned(ABS_ALWAYSONTOP));
    // A window captured as not visible (for example during an Explorer re-layout) is shown too.
    CHECK(backend.windows[0].visible); CHECK(backend.windows[1].visible);
    const auto writes=backend.writes; REQUIRE(lease.End().ok()); CHECK_EQ(writes,backend.writes);
}
AETHER_TEST("taskbar lease: external preference changes and Explorer restart survive restoration") {
    FakeTaskbars backend; TaskbarVisibilityLease lease(backend); REQUIRE(lease.Begin().ok());
    backend.state=ABS_AUTOHIDE; backend.windows={{Id(21),true}};
    REQUIRE(lease.Enforce().ok()); CHECK_FALSE(backend.windows[0].visible);
    REQUIRE(lease.End().ok()); CHECK_EQ(backend.state,unsigned(ABS_AUTOHIDE)); CHECK(backend.windows[0].visible);
}
AETHER_TEST("taskbar lease: failed takeover restores previous taskbar and remains retryable") {
    FakeTaskbars backend; backend.failHide=true; TaskbarVisibilityLease lease(backend);
    CHECK_FALSE(lease.Begin().ok()); CHECK(backend.windows[0].visible); CHECK_EQ(backend.state,unsigned(ABS_ALWAYSONTOP));
    backend.failHide=false; REQUIRE(lease.Begin().ok()); REQUIRE(lease.End().ok());
}
AETHER_TEST("taskbar lease: restoration failure can be retried and repeated enforcement is idle") {
    FakeTaskbars backend; TaskbarVisibilityLease lease(backend); REQUIRE(lease.Begin().ok());
    const auto writes=backend.writes; REQUIRE(lease.Enforce().ok()); CHECK_EQ(writes,backend.writes);
    backend.failRestoreOnce=true; CHECK_FALSE(lease.End().ok()); REQUIRE(lease.End().ok()); CHECK(backend.windows[0].visible);
}
AETHER_TEST("taskbar lease: no shell or state refusal never leaves hidden windows") {
    FakeTaskbars backend; backend.windows.clear(); TaskbarVisibilityLease lease(backend); CHECK_FALSE(lease.Begin().ok());
    backend.windows={{Id(1),true}}; backend.failState=true; CHECK_FALSE(lease.Begin().ok()); CHECK(backend.windows[0].visible);
}
AETHER_TEST("taskbar guard: cancelled or malformed start rejects before accessing real taskbars") {
    CancellationSource cancel; cancel.Cancel();
    CHECK_EQ(TaskbarGuardSession::Start(L"relative.exe",L"",cancel.Token()).error().code,ErrorCode::Cancelled);
    CHECK_EQ(TaskbarGuardSession::Start(L"relative.exe",L"").error().code,ErrorCode::InvalidArgument);
    // A relative baseline location is rejected before any taskbar is read or hidden (D21).
    CHECK_EQ(TaskbarGuardSession::Start(L"C:\\Aether\\AetherShell.exe",L"taskbar-baseline.json").error().code,ErrorCode::InvalidArgument);
}

namespace {
// Lends the fake to code that owns its backend, and counts how often one was requested.
class Borrowed final : public ITaskbarBackend {
public:
    explicit Borrowed(ITaskbarBackend& inner) : inner_(inner) {}
    Result<unsigned> State() override { return inner_.State(); }
    Result<void> SetState(unsigned value) override { return inner_.SetState(value); }
    Result<std::vector<TaskbarWindowState>> Windows() override { return inner_.Windows(); }
    Result<void> SetVisible(HWND window,bool visible) override { return inner_.SetVisible(window,visible); }
private:
    ITaskbarBackend& inner_;
};
TaskbarBackendFactory Lend(FakeTaskbars& backend,int& created) {
    return [&backend,&created]() -> Result<std::unique_ptr<ITaskbarBackend>> {
        ++created; return std::unique_ptr<ITaskbarBackend>(std::make_unique<Borrowed>(backend));
    };
}
std::filesystem::path BaselineFile(std::string_view name) { return TaskbarBaselineFile(test::MakeTempDirectory(name)/L"config"); }
// What a crash of both the app and its helper leaves behind: auto-hide on, taskbars hidden.
void Crash(FakeTaskbars& backend) { backend.state|=ABS_AUTOHIDE; for(auto& window:backend.windows) window.visible=false; }
}

AETHER_TEST("taskbar baseline: state and windows round-trip; a missing file is empty") {
    const auto file=BaselineFile("taskbar-baseline-roundtrip");
    auto missing=LoadTaskbarBaseline(file); REQUIRE(missing.ok()); CHECK_FALSE(missing->has_value());
    const TaskbarSnapshot snapshot{ABS_ALWAYSONTOP,{{Id(11),true},{Id(0x7FFF1234),false}}};
    REQUIRE(SaveTaskbarBaseline(file,snapshot).ok());
    auto loaded=LoadTaskbarBaseline(file); REQUIRE(loaded.ok()); REQUIRE(loaded->has_value());
    const auto& value=**loaded;
    CHECK_EQ(value.state,unsigned(ABS_ALWAYSONTOP)); REQUIRE_EQ(value.windows.size(),std::size_t(2));
    CHECK(value.windows[0].window==Id(11)); CHECK(value.windows[0].visible);
    CHECK(value.windows[1].window==Id(0x7FFF1234)); CHECK_FALSE(value.windows[1].visible);
    REQUIRE(RemoveTaskbarBaseline(file).ok()); REQUIRE(RemoveTaskbarBaseline(file).ok());
    auto removed=LoadTaskbarBaseline(file); REQUIRE(removed.ok()); CHECK_FALSE(removed->has_value());
}

AETHER_TEST("taskbar baseline: malformed, foreign-schema and oversized files are rejected") {
    const auto file=BaselineFile("taskbar-baseline-invalid");
    REQUIRE(fs::EnsureDirectory(file.parent_path()).ok());
    for (const std::string text : {std::string("not json"),
            std::string(R"({"schemaVersion":2,"state":0,"windows":[]})"),
            std::string(R"({"schemaVersion":1,"state":-1,"windows":[]})"),
            std::string(R"({"schemaVersion":1,"state":0,"windows":[{"handle":0,"visible":true}]})"),
            std::string(R"({"schemaVersion":1,"state":0,"windows":[{"handle":5}]})"),
            std::string(70*1024,' ')}) {
        REQUIRE(fs::WriteFileAtomic(file,text).ok());
        CHECK_FALSE(LoadTaskbarBaseline(file).ok());
    }
}

AETHER_TEST("taskbar recovery: a saved baseline restores state and visibility, then is removed") {
    FakeTaskbars backend; int created=0; const auto file=BaselineFile("taskbar-recovery-baseline");
    REQUIRE(SaveTaskbarBaseline(file,TaskbarSnapshot{ABS_ALWAYSONTOP,backend.windows}).ok());
    Crash(backend);
    auto recovered=RecoverTaskbars(file,false,Lend(backend,created));
    REQUIRE(recovered.ok()); CHECK(*recovered==TaskbarRecovery::RestoredFromBaseline);
    CHECK_EQ(backend.state,unsigned(ABS_ALWAYSONTOP)); CHECK(backend.windows[0].visible); CHECK(backend.windows[1].visible);
    auto left=LoadTaskbarBaseline(file); REQUIRE(left.ok()); CHECK_FALSE(left->has_value());
    const auto writes=backend.writes;
    auto again=RecoverTaskbars(file,false,Lend(backend,created));
    REQUIRE(again.ok()); CHECK(*again==TaskbarRecovery::NothingToRestore); CHECK_EQ(created,1); CHECK_EQ(backend.writes,writes);
}

AETHER_TEST("taskbar recovery: without a baseline only an explicit command shows the taskbar") {
    FakeTaskbars backend; int created=0; const auto file=BaselineFile("taskbar-recovery-none");
    Crash(backend);
    auto implicit=RecoverTaskbars(file,false,Lend(backend,created));
    REQUIRE(implicit.ok()); CHECK(*implicit==TaskbarRecovery::NothingToRestore); CHECK_EQ(created,0);
    CHECK_FALSE(backend.windows[0].visible);
    auto requested=RecoverTaskbars(file,true,Lend(backend,created));
    REQUIRE(requested.ok()); CHECK(*requested==TaskbarRecovery::ShownWithoutBaseline);
    CHECK(backend.windows[0].visible); CHECK(backend.windows[1].visible);
    // The original auto-hide preference is unknown, so it is left as it is.
    CHECK_EQ(backend.state,unsigned(ABS_ALWAYSONTOP|ABS_AUTOHIDE)); CHECK_EQ(backend.writes,0);
}

AETHER_TEST("taskbar recovery: an unreadable baseline shows the taskbar, keeps auto-hide and is removed") {
    FakeTaskbars backend; int created=0; const auto file=BaselineFile("taskbar-recovery-corrupt");
    REQUIRE(fs::EnsureDirectory(file.parent_path()).ok()); REQUIRE(fs::WriteFileAtomic(file,"{").ok());
    Crash(backend);
    auto recovered=RecoverTaskbars(file,false,Lend(backend,created));
    REQUIRE(recovered.ok()); CHECK(*recovered==TaskbarRecovery::ShownWithoutBaseline);
    CHECK(backend.windows[0].visible); CHECK_EQ(backend.writes,0);
    CHECK_FALSE(std::filesystem::exists(file));
}

AETHER_TEST("taskbar recovery: failures keep the baseline so the user can retry") {
    FakeTaskbars backend; int created=0; const auto file=BaselineFile("taskbar-recovery-retry");
    REQUIRE(SaveTaskbarBaseline(file,TaskbarSnapshot{ABS_ALWAYSONTOP,backend.windows}).ok());
    Crash(backend);
    const TaskbarBackendFactory unavailable=[]() -> Result<std::unique_ptr<ITaskbarBackend>> {
        return MakeError(ErrorCode::Unavailable,"synthetic missing Explorer");
    };
    CHECK_FALSE(RecoverTaskbars(file,false,unavailable).ok());
    backend.failState=true;
    CHECK_FALSE(RecoverTaskbars(file,false,Lend(backend,created)).ok());
    auto kept=LoadTaskbarBaseline(file); REQUIRE(kept.ok()); CHECK(kept->has_value());
    backend.failState=false;
    auto recovered=RecoverTaskbars(file,false,Lend(backend,created));
    REQUIRE(recovered.ok()); CHECK(*recovered==TaskbarRecovery::RestoredFromBaseline);
    CHECK_EQ(backend.state,unsigned(ABS_ALWAYSONTOP)); CHECK(backend.windows[0].visible);
}

AETHER_TEST("taskbar baseline preparation: persists before hiding and repairs an earlier crash first") {
    FakeTaskbars backend; const auto file=BaselineFile("taskbar-prepare");
    auto fresh=PrepareTaskbarBaseline(backend,file); REQUIRE(fresh.ok());
    CHECK_EQ(fresh->state,unsigned(ABS_ALWAYSONTOP)); CHECK_EQ(backend.writes,0); CHECK(backend.windows[0].visible);
    auto saved=LoadTaskbarBaseline(file); REQUIRE(saved.ok()); REQUIRE(saved->has_value()); CHECK_EQ((*saved)->state,unsigned(ABS_ALWAYSONTOP));
    // The file survives a crash of both processes; the next start must not capture the hidden state.
    Crash(backend);
    auto next=PrepareTaskbarBaseline(backend,file); REQUIRE(next.ok());
    CHECK_EQ(next->state,unsigned(ABS_ALWAYSONTOP)); CHECK(next->windows[0].visible);
    CHECK_EQ(backend.state,unsigned(ABS_ALWAYSONTOP)); CHECK(backend.windows[0].visible);
}

AETHER_TEST("taskbar baseline preparation: an unwritable location fails before anything is hidden") {
    FakeTaskbars backend; const auto root=test::MakeTempDirectory("taskbar-prepare-blocked");
    REQUIRE(fs::WriteFileAtomic(root/L"blocker","file, not a directory").ok());
    CHECK_FALSE(PrepareTaskbarBaseline(backend,root/L"blocker"/L"taskbar-baseline.json").ok());
    CHECK(backend.windows[0].visible); CHECK_EQ(backend.writes,0);
}

AETHER_TEST("taskbar recovery: a relative baseline location is refused before any taskbar is touched") {
    FakeTaskbars backend; int created=0; Crash(backend);
    auto recovered=RecoverTaskbars(L"taskbar-baseline.json",true,Lend(backend,created));
    REQUIRE_FALSE(recovered.ok()); CHECK_EQ(recovered.error().code,ErrorCode::InvalidArgument);
    CHECK_EQ(created,0); CHECK_FALSE(backend.windows[0].visible);
    CHECK_FALSE(PrepareTaskbarBaseline(backend,L"taskbar-baseline.json").ok()); CHECK_EQ(backend.writes,0);
}
