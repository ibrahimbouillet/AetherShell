// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/CleanSession.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::showcase;

namespace {

VisibleApplication App(std::uint32_t pid, bool sameUser = true, bool visible = true,
                       std::uint32_t session = 4, std::uint64_t created = 100) {
    return {{pid, created + pid}, session, sameUser, visible, false, false};
}

class FakeBackend final : public ICleanSessionBackend {
public:
    std::vector<VisibleApplication> applications;
    std::vector<std::uint32_t> closeRequests;
    std::vector<std::uint32_t> terminateRequests;
    std::vector<std::uint32_t> launches;
    std::vector<std::wstring> launchArguments;
    std::map<std::uint32_t, bool> running;
    std::map<std::uint32_t, bool> exitsOnClose;
    std::map<std::uint32_t, bool> exitsOnTerminate;
    std::uint32_t failSnapshotPid = 0;
    std::uint32_t failRelaunchPid = 0;
    std::uint32_t failTerminatePid = 0;

    Result<std::vector<VisibleApplication>> EnumerateVisibleApplications(std::uint32_t) override {
        for (const auto& application : applications) running.try_emplace(application.process.processId, true);
        return applications;
    }

    Result<RelaunchDescriptor> CaptureRelaunchDescriptor(ProcessIdentity process) override {
        if (process.processId == failSnapshotPid) return MakeError(ErrorCode::Unavailable, "snapshot failed");
        return RelaunchDescriptor(L"showcase-fixture.exe", L"--fixture");
    }

    Result<void> RequestGracefulClose(ProcessIdentity process) override {
        closeRequests.push_back(process.processId);
        if (exitsOnClose[process.processId]) running[process.processId] = false;
        return Ok();
    }

    Result<bool> WaitForExit(ProcessIdentity process, std::uint32_t) override {
        return !running[process.processId];
    }

    Result<void> TerminateApplication(ProcessIdentity process) override {
        terminateRequests.push_back(process.processId);
        if (process.processId == failTerminatePid) return MakeError(ErrorCode::AccessDenied, "termination failed");
        if (exitsOnTerminate[process.processId]) running[process.processId] = false;
        return Ok();
    }

    Result<bool> IsRunning(ProcessIdentity process) override { return running[process.processId]; }

    Result<void> Relaunch(ProcessIdentity process, const RelaunchDescriptor& descriptor) override {
        launches.push_back(process.processId);
        launchArguments.push_back(descriptor.CommandLine());
        if (process.processId == failRelaunchPid) return MakeError(ErrorCode::Unavailable, "launch failed");
        running[process.processId] = true;
        return Ok();
    }
};

}  // namespace

AETHER_TEST("showcase session: select only visible current-user apps and deduplicate windows") {
    std::vector<VisibleApplication> apps{
        App(10), App(10), App(11, false), App(12, true, false), App(13, true, true, 3),
        App(14), App(15), App(16), App(0), App(17, true, true, 4, 0)};
    apps[5].isSystemOrShell = true;
    apps[6].isShowcaseProcess = true;
    apps[7].process.creationTime = 0;
    apps[9].process.creationTime = 0;
    const auto selected = CleanSession::SelectApplicationsToClose(apps, 4);
    CHECK_EQ(selected.size(), 1U);
    CHECK_EQ(selected.front().processId, 10U);
}

AETHER_TEST("showcase session: packaged activation identities are held in memory and cleared") {
    RelaunchDescriptor descriptor(L"explorer.exe", L"explorer.exe",
                                  {L"Vendor.Package_alpha!App", L"Vendor.Package_alpha!App"});
    CHECK_EQ(descriptor.PackagedApplicationIds().size(), 2U);
    CHECK_EQ(descriptor.PackagedApplicationIds().front(), L"Vendor.Package_alpha!App");

    descriptor.Clear();
    CHECK(descriptor.ExecutablePath().empty());
    CHECK(descriptor.CommandLine().empty());
    CHECK(descriptor.PackagedApplicationIds().empty());
}

AETHER_TEST("showcase session: preflight snapshots every app before closing any window") {
    FakeBackend backend;
    backend.applications = {App(20), App(21)};
    backend.failSnapshotPid = 21;
    CleanSession session(backend, 4);
    const auto result = session.Begin();
    CHECK_FALSE(result.ok());
    CHECK_EQ(session.State(), CleanSessionState::Failed);
    CHECK(backend.closeRequests.empty());
    CHECK(backend.terminateRequests.empty());
}

AETHER_TEST("showcase session: close normally, terminate a remaining app, then relaunch in reverse order") {
    FakeBackend backend;
    backend.applications = {App(30), App(31)};
    backend.exitsOnClose[30] = true;
    backend.exitsOnTerminate[31] = true;
    CleanSession session(backend, 4, 1);

    CHECK(session.Begin().ok());
    CHECK_EQ(session.State(), CleanSessionState::Ready);
    CHECK_EQ(backend.closeRequests.size(), 2U);
    CHECK_EQ(backend.terminateRequests.size(), 1U);
    CHECK_EQ(backend.terminateRequests.front(), 31U);

    CHECK(session.Restore().ok());
    CHECK_EQ(session.State(), CleanSessionState::Restored);
    CHECK_EQ(backend.launches.size(), 2U);
    CHECK_EQ(backend.launches[0], 31U);
    CHECK_EQ(backend.launches[1], 30U);
    CHECK_EQ(backend.launchArguments[0], L"--fixture");
    CHECK(session.Restore().ok());
    CHECK_EQ(backend.launches.size(), 2U);
}

AETHER_TEST("showcase session: failure rolls back apps already closed and leaves running apps alone") {
    FakeBackend backend;
    backend.applications = {App(40), App(41)};
    backend.exitsOnClose[40] = true;
    backend.exitsOnTerminate[41] = false;
    CleanSession session(backend, 4, 1);

    const auto result = session.Begin();
    CHECK_FALSE(result.ok());
    CHECK_EQ(session.State(), CleanSessionState::Restored);
    CHECK_EQ(backend.terminateRequests.size(), 1U);
    CHECK_EQ(backend.launches.size(), 1U);
    CHECK_EQ(backend.launches.front(), 40U);
    CHECK(backend.running[41]);
}

AETHER_TEST("showcase session: relaunch failure reports no executable path or command line") {
    FakeBackend backend;
    backend.applications = {App(50)};
    backend.exitsOnClose[50] = true;
    backend.failRelaunchPid = 50;
    CleanSession session(backend, 4);
    CHECK(session.Begin().ok());

    const auto restored = session.Restore();
    CHECK_FALSE(restored.ok());
    CHECK_EQ(session.State(), CleanSessionState::Failed);
    CHECK(restored.error().message.find("showcase-fixture") == std::string::npos);
    CHECK(restored.error().message.find("--fixture") == std::string::npos);
}

AETHER_TEST("showcase session: destructor attempts restoration of an active capture session") {
    FakeBackend backend;
    backend.applications = {App(60)};
    backend.exitsOnClose[60] = true;
    {
        CleanSession session(backend, 4);
        CHECK(session.Begin().ok());
        CHECK(backend.launches.empty());
    }
    CHECK_EQ(backend.launches.size(), 1U);
    CHECK_EQ(backend.launches.front(), 60U);
}
