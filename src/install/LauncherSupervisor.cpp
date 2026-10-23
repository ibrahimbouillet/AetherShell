// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/LauncherSupervisor.h"
#include "install/InstallInternal.h"
#include "core/Strings.h"

namespace aether::install {
namespace {
Result<std::wstring> Name(const InstallLayout& layout) {
    auto canonical=detail::CanonicalRoot(layout.root);if(!canonical)return canonical.error();
    auto hash=updates::Sha256::Hash(text::Narrow(*canonical));if(!hash)return hash.error();
    return L"Local\\AetherShell.Supervisor."+text::Widen(updates::ToHex(*hash));
}
Result<DWORD> Timeout(std::chrono::milliseconds timeout) {
    if(timeout.count()<0 || static_cast<std::uint64_t>(timeout.count())>=INFINITE)
        return MakeError(ErrorCode::InvalidArgument,"invalid supervisor wait timeout");
    return static_cast<DWORD>(timeout.count());
}
}
LauncherSupervisor::LauncherSupervisor(LauncherSupervisor&& other) noexcept
    :lease_(std::move(other.lease_)),stop_(std::move(other.stop_)),held_(std::exchange(other.held_,false)) {}
LauncherSupervisor::~LauncherSupervisor() { Release(); }
void LauncherSupervisor::Release() noexcept {
    if(held_){::ReleaseMutex(lease_.Get());held_=false;}
    stop_.Reset();lease_.Reset();
}
Result<LauncherSupervisor> LauncherSupervisor::Acquire(const InstallLayout& layout) {
    try {
        auto name=Name(layout);if(!name)return name.error();LauncherSupervisor owner;
        owner.lease_.Reset(::CreateMutexW(nullptr,FALSE,name->c_str()));if(!owner.lease_)return MakeError(ErrorCode::Platform,"cannot create launcher lease",::GetLastError());
        const auto wait=::WaitForSingleObject(owner.lease_.Get(),0);
        if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED)return MakeError(ErrorCode::Unavailable,"launcher supervisor is already running");
        owner.held_=true;owner.stop_.Reset(::CreateEventW(nullptr,TRUE,FALSE,(*name+L".Stop").c_str()));
        // Never reset an existing event: a simultaneous Quit/maintenance stop wins even if it
        // arrived between lease acquisition and opening the event. A later launch gets a fresh
        // nonsignaled object once all participants have released the old one.
        if(!owner.stop_)return MakeError(ErrorCode::Platform,"cannot create launcher stop signal",::GetLastError());
        return owner;
    } catch(...) { return MakeError(ErrorCode::Internal,"cannot create launcher supervision"); }
}
Result<bool> LauncherSupervisor::StopRequested(std::chrono::milliseconds timeout) const {
    auto milliseconds=Timeout(timeout);if(!milliseconds)return milliseconds.error();
    const auto wait=::WaitForSingleObject(stop_.Get(),*milliseconds);
    if(wait==WAIT_OBJECT_0)return true;if(wait==WAIT_TIMEOUT)return false;
    return MakeError(ErrorCode::Platform,"cannot wait for launcher stop",::GetLastError());
}
Result<void> StopLauncherSupervisor(const InstallLayout& layout,std::chrono::milliseconds timeout) {
    try {
        auto milliseconds=Timeout(timeout);if(!milliseconds)return milliseconds.error();
        auto name=Name(layout);if(!name)return name.error();
        platform::UniqueHandle lease(::CreateMutexW(nullptr,FALSE,name->c_str()));if(!lease)return MakeError(ErrorCode::Platform,"cannot inspect launcher lease",::GetLastError());
        auto wait=::WaitForSingleObject(lease.Get(),0);
        if(wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED){::ReleaseMutex(lease.Get());return Ok();}
        if(wait!=WAIT_TIMEOUT)return MakeError(ErrorCode::Platform,"cannot inspect launcher lease",::GetLastError());
        platform::UniqueHandle stop(::CreateEventW(nullptr,TRUE,FALSE,(*name+L".Stop").c_str()));
        if(!stop || !::SetEvent(stop.Get()))return MakeError(ErrorCode::Platform,"cannot signal launcher stop",::GetLastError());
        wait=::WaitForSingleObject(lease.Get(),*milliseconds);
        if(wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED){::ReleaseMutex(lease.Get());return Ok();}
        return MakeError(wait==WAIT_TIMEOUT?ErrorCode::Timeout:ErrorCode::Platform,"launcher supervisor has not stopped");
    } catch(...) { return MakeError(ErrorCode::Internal,"cannot stop launcher supervision"); }
}

}  // namespace aether::install
