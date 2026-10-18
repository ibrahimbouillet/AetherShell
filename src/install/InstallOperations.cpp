// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/InstallInternal.h"
#include "core/FileSystem.h"

#include <algorithm>

namespace aether::install {
namespace {
Result<void> CompletePayload(const PayloadManifest& manifest) {
    for(const auto required:{"AetherShell.exe","AetherShellApp.exe","AetherShellUpdater.exe","LICENSE","THIRD_PARTY_NOTICES.md"}) {
        if(std::none_of(manifest.files.begin(),manifest.files.end(),[&](const PayloadFile& file){ return file.name==required; })) return MakeError(ErrorCode::Validation,"setup lacks a required application file");
    }
    return Ok();
}
}  // namespace
Result<void> detail::CopyStable(const std::filesystem::path& package,const InstallLayout& layout,const InstallState& state) {
    auto version=layout.Version(state.activeVersion); if(!version) return version.error();
    auto safe=detail::Safe(layout,layout.Launcher()); if(!safe) return safe;
    auto copied=detail::CopyAtomic(version.value()/L"AetherShell.exe",layout.Launcher()); if(!copied) return copied;
    safe=detail::Safe(layout,layout.Maintenance()); if(!safe) return safe;
    std::error_code error; if(std::filesystem::equivalent(package,layout.Maintenance(),error) && !error) return Ok();
    if(state.transaction && state.transaction->state!=TransactionState::Committed && std::filesystem::exists(layout.Maintenance())) {
        const auto backup=detail::Backup(layout,*state.transaction).native()+L".setup";
        safe=detail::Safe(layout,backup); if(!safe) return safe;
        if(!std::filesystem::exists(backup)) { auto saved=detail::CopyAtomic(layout.Maintenance(),backup); if(!saved) return saved; }
    }
    return detail::CopyAtomic(package,layout.Maintenance());
}
namespace {
Result<void> Install(const std::filesystem::path& package,const InstallLayout& layout,const InstallOptions& options,bool repair) {
    if(options.cancellation.IsCancelled()) return MakeError(ErrorCode::Cancelled,"installation cancelled");
    auto lock=detail::InstallLock::Acquire(layout); if(!lock) return lock.error();
    auto manifest=ReadPayloadManifest(package); if(!manifest) return manifest.error();
    auto complete=CompletePayload(manifest.value()); if(!complete) return complete;
    auto loaded=LoadState(layout); InstallState state;
    if(loaded) { auto recovered=RecoverInterrupted(layout); if(!recovered) return recovered; loaded=LoadState(layout); if(!loaded) return loaded.error(); state=std::move(loaded.value()); }
    else if(loaded.error().code!=ErrorCode::NotFound) return loaded.error();
    else {
        std::error_code error;
        if(std::filesystem::exists(layout.root,error) && !std::filesystem::is_empty(layout.root,error)) return MakeError(ErrorCode::Validation,"new installation directory must be empty");
        if(error) return MakeError(ErrorCode::Io,"cannot inspect installation directory",error.value());
    }
    if(repair && !state.activeVersion.empty() && state.activeVersion!=manifest->version) return MakeError(ErrorCode::Validation,"repair package version must match the active version");
    if(!state.activeVersion.empty()) {
        const auto current=updates::SemanticVersion::Parse(state.activeVersion), next=updates::SemanticVersion::Parse(manifest->version);
        if(!current || !next) return MakeError(ErrorCode::Validation,"invalid installation version");
        if(next.value()<current.value() && !options.allowDowngrade) return MakeError(ErrorCode::Validation,"downgrade requires explicit confirmation");
    }
    auto ownedProcesses=options.processes?nullptr:detail::WindowsProcesses();
    auto* processes=options.processes?options.processes:ownedProcesses.get();
    auto staged=StagePackage(package,layout,state,options.cancellation);
    if(!staged || options.cancellation.IsCancelled()) {
        if(state.transaction) {
            auto ignored=detail::Remove(layout,detail::Staging(layout,*state.transaction)); (void)ignored;
            state.transaction.reset(); ignored=SaveState(layout,state); (void)ignored;
        }
        return staged?MakeError(ErrorCode::Cancelled,"installation cancelled before activation"):staged.error();
    }
    auto stopped=processes->RequestExit(std::chrono::seconds(30)); if(!stopped) return stopped;
    if(options.cancellation.IsCancelled()) {
        auto ignored=detail::Remove(layout,detail::Staging(layout,*state.transaction)); (void)ignored;
        state.transaction.reset(); ignored=SaveState(layout,state); (void)ignored;
        return MakeError(ErrorCode::Cancelled,"installation cancelled before activation");
    }
    auto active=ActivateVersion(layout,state); if(!active) return active;
    auto copied=detail::CopyStable(package,layout,state);
    if(!copied) { if(!state.transaction->fromVersion.empty()) { auto rolled=Rollback(layout,state); (void)rolled; } return copied; }
    auto committed=CommitUpdate(layout,state); if(!committed) return committed;
    return RegisterInstallation(layout,state.activeVersion,options);
}
}
Result<void> InstallPackage(const std::filesystem::path& package,const InstallLayout& layout,const InstallOptions& options) {
    try { return Install(package,layout,options,false); } catch(...) { return MakeError(ErrorCode::Io,"installation failed"); }
}
Result<void> RepairPackage(const std::filesystem::path& package,const InstallLayout& layout,const InstallOptions& options) {
    try { return Install(package,layout,options,true); } catch(...) { return MakeError(ErrorCode::Io,"repair failed"); }
}
Result<void> Uninstall(const InstallLayout& layout,const std::filesystem::path& dataRoot,bool removeUserData,const InstallOptions& options) {
    try {
        auto lock=detail::InstallLock::Acquire(layout); if(!lock) return lock.error();
        auto state=LoadState(layout); if(!state) return state.error();
        auto separate=ValidateSeparateRoots(layout,dataRoot);if(!separate)return separate;
        std::optional<InstallLayout> data;
        if(removeUserData) {
            auto validated=InstallLayout::Create(dataRoot); if(!validated) return validated.error(); data=std::move(validated.value());
        }
        auto ownedProcesses=options.processes?nullptr:detail::WindowsProcesses();
        auto* processes=options.processes?options.processes:ownedProcesses.get();
        auto stopped=processes->RequestExit(std::chrono::seconds(30)); if(!stopped) return stopped;
        auto cleanupOptions=options;
        if(options.registerInstallation) {cleanupOptions.startMenuShortcut=true;cleanupOptions.desktopShortcut=true;}
        auto registration=RemoveRegistration(layout,cleanupOptions); if(!registration) return registration;
        auto removed=detail::Remove(layout,layout.root); if(!removed) return removed;
        if(data) return detail::Remove(*data,data->root);
        return Ok();
    } catch(...) { return MakeError(ErrorCode::Io,"uninstall failed"); }
}
}  // namespace aether::install
