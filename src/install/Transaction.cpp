// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/InstallInternal.h"
#include "core/FileSystem.h"
#include "core/Strings.h"

#include <system_error>

namespace aether::install {
namespace {
Error Failure(std::string message,int code=0) { return MakeError(ErrorCode::Io,std::move(message),code); }
Result<std::wstring> CanonicalLockRoot(const std::filesystem::path& root) {
    auto existing=root;
    while(::GetFileAttributesW(existing.c_str())==INVALID_FILE_ATTRIBUTES) {
        const auto parent=existing.parent_path(); if(parent==existing || parent.empty()) return Failure("cannot resolve installation lock root"); existing=parent;
    }
    platform::UniqueFileHandle directory(::CreateFileW(existing.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr));
    if(!directory) return Failure("cannot resolve installation lock root",static_cast<int>(::GetLastError()));
    const DWORD size=::GetFinalPathNameByHandleW(directory.Get(),nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!size) return Failure("cannot normalize installation lock root",static_cast<int>(::GetLastError()));
    std::wstring buffer(size,L'\0'); const DWORD count=::GetFinalPathNameByHandleW(directory.Get(),buffer.data(),size,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!count || count>=size) return Failure("cannot normalize installation lock root"); buffer.resize(count);
    if(buffer.starts_with(L"\\\\?\\UNC\\")) buffer=L"\\\\"+buffer.substr(8);
    else if(buffer.starts_with(L"\\\\?\\")) buffer.erase(0,4);
    auto canonical=std::filesystem::path(buffer);
    if(existing!=root) canonical/=root.lexically_relative(existing);
    buffer=canonical.lexically_normal().native();
    const int mapped=::LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,buffer.data(),static_cast<int>(buffer.size()),nullptr,0,nullptr,nullptr,0);
    if(!mapped) return Failure("cannot normalize installation lock case");
    std::wstring folded(static_cast<std::size_t>(mapped),L'\0');
    if(!::LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,buffer.data(),static_cast<int>(buffer.size()),folded.data(),mapped,nullptr,nullptr,0)) return Failure("cannot normalize installation lock case");
    return folded;
}
Result<void> Rename(const InstallLayout& layout,const std::filesystem::path& from,const std::filesystem::path& to) {
    auto safe=detail::Safe(layout,from); if(!safe) return safe; safe=detail::Safe(layout,to); if(!safe) return safe;
    std::error_code ec; std::filesystem::rename(from,to,ec); return ec?Result<void>(Failure("cannot switch staged version",ec.value())):Ok();
}
Result<void> RestoreRepairBackup(const InstallLayout& layout,const InstallTransaction& t) {
    const auto backup=detail::Backup(layout,t); if(!std::filesystem::exists(backup)) return Ok();
    auto target=layout.Version(t.toVersion); if(!target) return target.error();
    auto removed=detail::Remove(layout,target.value()); if(!removed) return removed;
    return Rename(layout,backup,target.value());
}
}

Result<detail::InstallLock> detail::InstallLock::Acquire(const InstallLayout& layout) {
    auto safe=Safe(layout,layout.root); if(!safe) return safe.error();
    auto canonical=CanonicalLockRoot(layout.root); if(!canonical) return canonical.error();
    auto hash=updates::Sha256::Hash(text::Narrow(canonical.value())); if(!hash) return hash.error();
    const auto name=L"Local\\AetherShell.Install."+text::Widen(updates::ToHex(hash.value()));
    InstallLock lock; lock.mutex_.Reset(::CreateMutexW(nullptr,FALSE,name.c_str()));
    if(!lock.mutex_) return MakeError(ErrorCode::Platform,"cannot create installation lock",::GetLastError());
    const DWORD wait=::WaitForSingleObject(lock.mutex_.Get(),0);
    if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) return MakeError(ErrorCode::Unavailable,"another installation operation is running");
    lock.held_=true; return lock;
}
Result<std::wstring> detail::CanonicalRoot(const std::filesystem::path& root) {return CanonicalLockRoot(root);}

Result<void> ActivateVersion(const InstallLayout& layout,InstallState& state) {
    try {
        if(!state.transaction || state.transaction->state!=TransactionState::Staged) return MakeError(ErrorCode::InvalidState,"no staged version");
        const auto& t=*state.transaction; auto target=layout.Version(t.toVersion); if(!target) return target.error();
        const auto staging=detail::Staging(layout,t), backup=detail::Backup(layout,t);
        auto safe=detail::Safe(layout,staging); if(!safe) return safe; safe=detail::Safe(layout,backup); if(!safe) return safe;
        if(!std::filesystem::exists(staging)) return MakeError(ErrorCode::NotFound,"staged payload is missing");
        if(std::filesystem::exists(target.value()) && state.activeVersion!=t.toVersion) return MakeError(ErrorCode::AlreadyExists,"version folder already exists");
        auto directories=fs::EnsureDirectory(layout.root/L"versions"); if(!directories) return directories;
        auto applying=detail::WriteState(layout,state,TransactionState::Applying); if(!applying) return applying;
        if(std::filesystem::exists(target.value())) {
            auto moved=Rename(layout,target.value(),backup); if(!moved) return moved;
        }
        auto moved=Rename(layout,staging,target.value());
        if(!moved) { auto restored=RestoreRepairBackup(layout,t); (void)restored; return moved; }
        if(t.fromVersion!=t.toVersion) state.previousVersion=t.fromVersion;
        state.activeVersion=t.toVersion;
        return detail::WriteState(layout,state,TransactionState::Installed);
    } catch(...) { return Failure("cannot activate staged version"); }
}
Result<void> CommitUpdate(const InstallLayout& layout,InstallState& state) {
    try {
        if(!state.transaction) return MakeError(ErrorCode::InvalidState,"no installation transaction to commit");
        if(state.transaction->state!=TransactionState::Committed) { auto committed=detail::WriteState(layout,state,TransactionState::Committed); if(!committed) return committed; }
        const auto& t=*state.transaction; auto removed=detail::Remove(layout,detail::Backup(layout,t)); if(!removed) return removed;
        removed=detail::Remove(layout,detail::Backup(layout,t).native()+L".setup"); if(!removed) return removed;
        const auto versions=layout.root/L"versions";
        if(!std::filesystem::exists(versions)) return Ok();
        for(const auto& child:std::filesystem::directory_iterator(versions)) {
            const auto name=text::Narrow(child.path().filename().native());
            if(name==state.activeVersion || name==state.previousVersion || !updates::SemanticVersion::Parse(name)) continue;
            removed=detail::Remove(layout,child.path()); if(!removed) return removed;
        }
        return Ok();
    } catch(...) { return Failure("cannot commit installation transaction"); }
}
Result<void> Rollback(const InstallLayout& layout,InstallState& state) {
    try {
        if(!state.transaction || state.transaction->fromVersion.empty()) return MakeError(ErrorCode::Unavailable,"no previous version is available");
        const auto t=*state.transaction;
        if(state.transaction->state!=TransactionState::RollbackRequired) { auto pending=detail::WriteState(layout,state,TransactionState::RollbackRequired); if(!pending) return pending; }
        auto restored=RestoreRepairBackup(layout,t); if(!restored) return restored;
        auto previous=layout.Version(t.fromVersion); if(!previous) return previous.error();
        if(!std::filesystem::exists(previous.value()/L"AetherShellApp.exe")) return MakeError(ErrorCode::NotFound,"previous application payload is missing");
        state.activeVersion=t.fromVersion; state.previousVersion.clear();
        if(std::filesystem::exists(previous.value()/L"AetherShell.exe")) {
            auto launcher=detail::CopyAtomic(previous.value()/L"AetherShell.exe",layout.Launcher()); if(!launcher) return launcher;
        }
        const auto setupBackup=detail::Backup(layout,t).native()+L".setup";
        if(std::filesystem::exists(setupBackup)) { auto copied=detail::CopyAtomic(setupBackup,layout.Maintenance()); if(!copied) return copied; }
        auto saved=detail::WriteState(layout,state,TransactionState::RolledBack); if(!saved) return saved;
        auto removed=detail::Remove(layout,setupBackup); if(!removed) return removed;
        if(t.toVersion!=t.fromVersion) { auto failed=layout.Version(t.toVersion); if(!failed) return failed.error(); return detail::Remove(layout,failed.value()); }
        return Ok();
    } catch(...) { return Failure("cannot roll back installation"); }
}
Result<void> RecoverInterrupted(const InstallLayout& layout) {
    try {
        auto lock=detail::InstallLock::Acquire(layout);
        if(!lock) return lock.error();
        auto loaded=LoadState(layout); if(!loaded) return loaded.error(); auto state=std::move(loaded.value());
        if(!state.transaction) return Ok(); const auto t=*state.transaction;
        if(t.state==TransactionState::Installed || t.state==TransactionState::HealthCheckPending || t.state==TransactionState::RollbackRequired) {
            if(!t.fromVersion.empty()) return Rollback(layout,state);
            auto failed=layout.Version(t.toVersion); if(!failed) return failed.error();
            auto removed=detail::Remove(layout,failed.value()); if(!removed) return removed;
            removed=detail::Remove(layout,detail::Staging(layout,t)); if(!removed) return removed;
            state.activeVersion.clear(); state.previousVersion.clear(); state.transaction.reset(); return SaveState(layout,state);
        }
        if(t.state==TransactionState::Committed) return CommitUpdate(layout,state);
        if(t.state==TransactionState::RolledBack) {
            auto removed=detail::Remove(layout,detail::Staging(layout,t)); if(!removed) return removed;
            removed=detail::Remove(layout,detail::Backup(layout,t)); if(!removed) return removed;
            removed=detail::Remove(layout,detail::Backup(layout,t).native()+L".setup"); if(!removed) return removed;
            if(t.toVersion!=state.activeVersion && t.toVersion!=state.previousVersion) { auto failed=layout.Version(t.toVersion); if(!failed) return failed.error(); return detail::Remove(layout,failed.value()); }
            return Ok();
        }
        auto restored=RestoreRepairBackup(layout,t); if(!restored) return restored;
        if(t.state==TransactionState::Applying && t.toVersion!=state.activeVersion && t.toVersion!=state.previousVersion) {
            auto target=layout.Version(t.toVersion); if(!target) return target.error(); auto removed=detail::Remove(layout,target.value()); if(!removed) return removed;
        }
        auto removed=detail::Remove(layout,detail::Staging(layout,t)); if(!removed) return removed;
        state.transaction.reset(); return SaveState(layout,state);
    } catch(...) { return Failure("cannot recover interrupted installation"); }
}
}  // namespace aether::install
