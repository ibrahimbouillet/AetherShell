// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/CleanSession.h"

#include <algorithm>
#include <utility>

namespace aether::showcase {
namespace {

void Wipe(std::wstring& value) noexcept {
    volatile wchar_t* cursor = value.empty() ? nullptr : value.data();
    for (std::size_t index = 0; index < value.size(); ++index) cursor[index] = L'\0';
    value.clear();
}

}  // namespace

RelaunchDescriptor::RelaunchDescriptor(std::wstring executablePath, std::wstring commandLine,
                                       std::vector<std::wstring> packagedApplicationIds)
    : executablePath_(std::move(executablePath)), commandLine_(std::move(commandLine)),
      packagedApplicationIds_(std::move(packagedApplicationIds)) {}

RelaunchDescriptor::~RelaunchDescriptor() { Clear(); }

RelaunchDescriptor::RelaunchDescriptor(RelaunchDescriptor&& other) noexcept
    : executablePath_(std::move(other.executablePath_)), commandLine_(std::move(other.commandLine_)),
      packagedApplicationIds_(std::move(other.packagedApplicationIds_)) {
    other.Clear();
}

RelaunchDescriptor& RelaunchDescriptor::operator=(RelaunchDescriptor&& other) noexcept {
    if (this == &other) return *this;
    Clear();
    executablePath_ = std::move(other.executablePath_);
    commandLine_ = std::move(other.commandLine_);
    packagedApplicationIds_ = std::move(other.packagedApplicationIds_);
    other.Clear();
    return *this;
}

void RelaunchDescriptor::Clear() noexcept {
    Wipe(executablePath_);
    Wipe(commandLine_);
    for (auto& applicationId : packagedApplicationIds_) Wipe(applicationId);
    packagedApplicationIds_.clear();
}

CleanSession::CleanSession(ICleanSessionBackend& backend, std::uint32_t sessionId,
                           std::uint32_t gracefulCloseTimeoutMilliseconds)
    : backend_(backend), sessionId_(sessionId),
      gracefulCloseTimeoutMilliseconds_(gracefulCloseTimeoutMilliseconds) {}

CleanSession::~CleanSession() { (void)Restore(); }

std::vector<ProcessIdentity> CleanSession::SelectApplicationsToClose(
    const std::vector<VisibleApplication>& applications, std::uint32_t sessionId) {
    std::vector<ProcessIdentity> selected;
    selected.reserve(applications.size());
    for (const auto& application : applications) {
        const auto& process = application.process;
        if (process.processId == 0 || process.creationTime == 0 || application.sessionId != sessionId ||
            !application.belongsToCurrentUser || !application.hasVisibleWindow || application.isSystemOrShell ||
            application.isShowcaseProcess) {
            continue;
        }
        if (std::find(selected.begin(), selected.end(), process) == selected.end()) selected.push_back(process);
    }
    return selected;
}

Result<void> CleanSession::Begin() {
    if (state_ != CleanSessionState::Idle) {
        return MakeError(ErrorCode::InvalidState, "clean showcase session can only be started once");
    }
    if (sessionId_ == 0 || gracefulCloseTimeoutMilliseconds_ == 0) {
        state_ = CleanSessionState::Failed;
        return MakeError(ErrorCode::InvalidArgument, "clean showcase session settings are invalid");
    }

    try {
        auto discovered = backend_.EnumerateVisibleApplications(sessionId_);
        if (!discovered) {
            state_ = CleanSessionState::Failed;
            return discovered.error();
        }
        const auto selected = SelectApplicationsToClose(*discovered, sessionId_);

        // Snapshot every restart descriptor before closing the first process. If preflight fails,
        // the desktop remains untouched.
        std::vector<SavedApplication> pending;
        pending.reserve(selected.size());
        for (const auto process : selected) {
            auto descriptor = backend_.CaptureRelaunchDescriptor(process);
            if (!descriptor) {
                state_ = CleanSessionState::Failed;
                return MakeError(ErrorCode::Unavailable,
                                 "showcase preflight could not prepare application restoration; no windows were closed");
            }
            pending.push_back({process, std::move(descriptor).value()});
        }

        saved_ = std::move(pending);
        state_ = CleanSessionState::Closing;
        for (const auto& application : saved_) {
            auto requested = backend_.RequestGracefulClose(application.process);
            auto exited = backend_.WaitForExit(application.process, gracefulCloseTimeoutMilliseconds_);
            if (exited && !*exited) {
                auto terminated = backend_.TerminateApplication(application.process);
                if (!terminated) return RollBackAfterFailure(terminated.error());
                exited = backend_.WaitForExit(application.process, gracefulCloseTimeoutMilliseconds_);
            }
            if (!exited) return RollBackAfterFailure(exited.error());
            if (!*exited) {
                return RollBackAfterFailure(MakeError(ErrorCode::Timeout,
                    "a visible application did not stop after the showcase close request"));
            }
            // Some applications exit after a close message even when the request API reports an
            // error. The observed exit is the authority; the request result is not logged.
            (void)requested;
        }
        state_ = CleanSessionState::Ready;
        return Ok();
    } catch (...) {
        return RollBackAfterFailure(MakeError(ErrorCode::Internal,
            "clean showcase session failed; attempting to relaunch applications already closed"));
    }
}

Result<void> CleanSession::RollBackAfterFailure(const Error& cause) noexcept {
    state_ = CleanSessionState::Failed;
    auto restored = Restore();
    if (!restored) {
        return MakeError(cause.code, "showcase failed and one or more applications could not be relaunched",
                         cause.platformCode);
    }
    return MakeError(cause.code, "showcase could not prepare a clean desktop; closed applications were relaunched",
                     cause.platformCode);
}

Result<void> CleanSession::Restore() noexcept {
    if (state_ == CleanSessionState::Idle || state_ == CleanSessionState::Restored) return Ok();
    if (state_ == CleanSessionState::Restoring) return Ok();
    state_ = CleanSessionState::Restoring;
    bool failed = false;
    for (auto iterator = saved_.rbegin(); iterator != saved_.rend(); ++iterator) {
        try {
            auto running = backend_.IsRunning(iterator->process);
            if (!running) {
                failed = true;
            } else if (!*running) {
                auto relaunched = backend_.Relaunch(iterator->process, iterator->descriptor);
                if (!relaunched) failed = true;
            }
        } catch (...) {
            failed = true;
        }
        iterator->descriptor.Clear();
    }
    saved_.clear();
    state_ = failed ? CleanSessionState::Failed : CleanSessionState::Restored;
    if (failed) return MakeError(ErrorCode::Unavailable, "one or more showcase applications could not be relaunched");
    return Ok();
}

}  // namespace aether::showcase
