// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/InstallInternal.h"
#include "core/FileSystem.h"
#include "core/Strings.h"
#include "platform/windows/OsVersion.h"
#include "updates/SignatureVerifier.h"

namespace aether::install {
namespace {
void Outcome(const std::filesystem::path& data,std::string_view version,std::string_view outcome) {
    if(data.empty()) return;
    auto value=json::Value::MakeObject(); value.Set("version",version); value.Set("outcome",outcome);
    value.Set("error",outcome=="Committed"?"":"update did not complete"); value.Set("timeUtc",detail::UtcNow());
    const auto ignored=fs::WriteFileAtomic(data/L"updates"/L"last-update-result.json",json::Serialize(value)); (void)ignored;
}
}
Result<updates::UpdateManifest> VerifyUpdatePackage(const std::filesystem::path& package,const std::filesystem::path& manifestPath,const std::filesystem::path& signaturePath) {
    try {
        auto bytes=fs::ReadFile(manifestPath,updates::kMaxManifestBytes); if(!bytes) return bytes.error();
        auto signature=fs::ReadFile(signaturePath,updates::kMaxSignatureFileBytes); if(!signature) return signature.error();
        const auto verifier=updates::EcdsaP256Verifier::ForReleaseManifests(); auto verified=verifier.VerifySignatureFile(bytes.value(),signature.value()); if(!verified) return verified.error();
        // These fields are used only after exact-byte signature verification. Unlike the app's
        // GitHub metadata query, the updater receives the authenticated manifest directly.
        auto json=json::Parse(bytes.value(),json::ParseOptions{updates::kMaxManifestBytes,8}); if(!json) return json.error();
        auto version=updates::SemanticVersion::Parse(json->GetString("version")); if(!version) return version.error();
        auto manifest=updates::UpdateManifest::Parse(bytes.value(),{version.value(),version->IsPrerelease(),updates::GitHubRepository::Official()}); if(!manifest) return manifest.error();
        auto asset=manifest->SelectAsset(updates::CurrentArchitecture(),platform::GetOsVersion().build); if(!asset) return asset.error();
        std::error_code error; const auto size=std::filesystem::file_size(package,error);
        if(error) return MakeError(ErrorCode::Io,"cannot read update package size",error.value());
        if(size!=asset->size) return MakeError(ErrorCode::Integrity,"update package size differs from signed manifest");
        auto hash=updates::Sha256::HashFile(package); if(!hash) return hash.error();
        if(hash.value()!=asset->sha256) return MakeError(ErrorCode::Integrity,"update package digest differs from signed manifest");
        auto payload=ReadPayloadManifest(package); if(!payload) return payload.error();
        if(payload->version!=manifest->version.ToString() || payload->architecture!=asset->architecture) return MakeError(ErrorCode::Integrity,"payload metadata differs from signed manifest");
        return manifest;
    } catch(...) { return MakeError(ErrorCode::Integrity,"cannot verify update package"); }
}
Result<void> ApplyUpdate(const std::filesystem::path& package,const std::filesystem::path& manifestPath,const std::filesystem::path& signaturePath,
    const InstallLayout& layout,const std::filesystem::path& dataRoot,IProcessOperations* injected,std::chrono::milliseconds healthTimeout) {
    try {
        if(healthTimeout.count()<0 || static_cast<std::uint64_t>(healthTimeout.count())>=INFINITE) return MakeError(ErrorCode::InvalidArgument,"invalid update health timeout");
        auto separate=ValidateSeparateRoots(layout,dataRoot);if(!separate)return separate;
        auto lock=detail::InstallLock::Acquire(layout); if(!lock) return lock.error();
        // Deny writes/deletion for the whole verification/extraction operation; extraction cannot
        // silently read a replacement package after hashing the original one.
        platform::UniqueFileHandle packageLock(::CreateFileW(package.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(!packageLock) return MakeError(ErrorCode::Io,"cannot lock update package",::GetLastError());
        auto verified=VerifyUpdatePackage(package,manifestPath,signaturePath); if(!verified) return verified.error();
        auto owned=injected?nullptr:detail::WindowsProcesses(); auto* processes=injected?injected:owned.get();
        return detail::ApplyVerifiedUpdate(package,verified->version,layout,dataRoot,*processes,healthTimeout);
    } catch(...) { return MakeError(ErrorCode::Io,"update transaction failed"); }
}
Result<void> detail::ApplyVerifiedUpdate(const std::filesystem::path& package,const updates::SemanticVersion& version,
    const InstallLayout& layout,const std::filesystem::path& dataRoot,IProcessOperations& processOperations,std::chrono::milliseconds healthTimeout) {
    try {
        auto lock=detail::InstallLock::Acquire(layout); if(!lock) return lock.error();
        auto* processes=&processOperations;
        auto recovered=RecoverInterrupted(layout); if(!recovered) return recovered;
        auto loaded=LoadState(layout); if(!loaded) return loaded.error(); auto state=std::move(loaded.value());
        auto old=updates::SemanticVersion::Parse(state.activeVersion); if(!old || version<=old.value()) return MakeError(ErrorCode::Validation,"automatic update must be newer than active version");
        auto staged=StagePackage(package,layout,state); if(!staged) return staged;
        if(state.transaction->toVersion!=version.ToString()) return MakeError(ErrorCode::Integrity,"staged update differs from verified version");
        auto stopped=processes->RequestExit(std::chrono::seconds(30)); if(!stopped) return stopped;
        auto active=ActivateVersion(layout,state); if(!active) return active;
        const auto restore=[&](Error error,std::optional<DWORD> child={}) -> Result<void> {
            if(child) { auto ended=processes->StopUnhealthyChild(*child); if(!ended) return ended; }
            auto rolled=Rollback(layout,state); if(!rolled) return rolled;
            auto previous=layout.Version(state.activeVersion); if(!previous) return previous.error();
            auto restarted=processes->Launch(previous.value()/L"AetherShellApp.exe",{L"--data-dir",dataRoot.native()}); if(!restarted) return restarted.error();
            Outcome(dataRoot,version.ToString(),"RolledBack"); return error;
        };
        auto token=MakeHealthToken(); if(!token) return restore(token.error());
        auto health=HealthCheck::Create(token.value()); if(!health) return restore(health.error());
        state.transaction->healthToken=token.value(); auto pending=detail::WriteState(layout,state,TransactionState::HealthCheckPending); if(!pending) return restore(pending.error());
        auto folder=layout.Version(state.activeVersion); if(!folder) return folder.error();
        auto launched=processes->Launch(folder.value()/L"AetherShellApp.exe",{L"--post-update-check",text::Widen(token.value()),L"--data-dir",dataRoot.native()});
        Result<bool> healthy=launched?processes->WaitForHealth(health.value(),healthTimeout):Result<bool>(launched.error());
        if(healthy && healthy.value()) {
            // The active app is healthy. Commit before maintenance refresh so a locked maintenance
            // copy cannot force termination or rollback of a working new application.
            auto committed=CommitUpdate(layout,state); if(!committed) return committed;
            auto copied=detail::CopyStable(package,layout,state); if(!copied) { Outcome(dataRoot,state.activeVersion,"MaintenanceRefreshFailed"); return copied; }
            Outcome(dataRoot,state.activeVersion,"Committed"); return Ok();
        }
        return restore(healthy?MakeError(ErrorCode::Timeout,"updated application did not signal health"):healthy.error(),launched?std::optional<DWORD>(launched.value()):std::nullopt);
    } catch(...) { return MakeError(ErrorCode::Io,"update transaction failed"); }
}
}  // namespace aether::install
