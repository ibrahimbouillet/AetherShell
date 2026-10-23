// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "core/Cancellation.h"
#include "platform/windows/Handles.h"
#include "updates/UpdateManifest.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aether::install {

// Blocking filesystem/registry/process operations: call from a worker, never the UI thread.
struct InstallLayout {
    std::filesystem::path root;
    [[nodiscard]] static Result<InstallLayout> Create(const std::filesystem::path& root);
    [[nodiscard]] std::filesystem::path Journal() const { return root / L"install-state.json"; }
    [[nodiscard]] std::filesystem::path Launcher() const { return root / L"AetherShell.exe"; }
    [[nodiscard]] std::filesystem::path Maintenance() const { return root / L"AetherShellSetup.exe"; }
    [[nodiscard]] Result<std::filesystem::path> Version(std::string_view version) const;
};
[[nodiscard]] Result<void> ValidateSeparateRoots(const InstallLayout& layout,const std::filesystem::path& dataRoot);

enum class TransactionState {
    Prepared, Verified, Staged, Applying, Installed, HealthCheckPending, Committed,
    RollbackRequired, RolledBack
};
struct InstallTransaction {
    std::string id, fromVersion, toVersion, healthToken;
    TransactionState state = TransactionState::Prepared;
    std::string startedUtc, updatedUtc, error;
};
struct InstallState {
    int schemaVersion = 1;
    std::string activeVersion, previousVersion;
    std::optional<InstallTransaction> transaction;
};
[[nodiscard]] Result<InstallState> ParseState(std::string_view bytes);
[[nodiscard]] Result<InstallState> LoadState(const InstallLayout& layout);
[[nodiscard]] Result<void> SaveState(const InstallLayout& layout, const InstallState& state);
[[nodiscard]] Result<void> Transition(InstallState& state, TransactionState next);

struct PayloadFile {
    std::string name, resource;
    std::uint64_t size = 0;
    updates::Sha256Digest sha256{};
};
struct PayloadManifest {
    std::string version;
    updates::Architecture architecture = updates::Architecture::X64;
    std::vector<PayloadFile> files;
};
[[nodiscard]] Result<PayloadManifest> ParsePayloadManifest(std::string_view bytes);
// PAYLOAD_MANIFEST and every file resource are RCDATA, opened strictly as data, never executed.
[[nodiscard]] Result<PayloadManifest> ReadPayloadManifest(const std::filesystem::path& package);
[[nodiscard]] Result<void> StagePackage(const std::filesystem::path& package,
    const InstallLayout& layout, InstallState& state, const CancellationToken& cancellation = {});
[[nodiscard]] Result<void> ActivateVersion(const InstallLayout& layout, InstallState& state);
[[nodiscard]] Result<void> CommitUpdate(const InstallLayout& layout, InstallState& state);
[[nodiscard]] Result<void> Rollback(const InstallLayout& layout, InstallState& state);
[[nodiscard]] Result<void> RecoverInterrupted(const InstallLayout& layout);

struct RegistryPaths {
    std::wstring uninstall = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AetherShell";
    std::wstring run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    std::wstring startupApproval = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
};
class IProcessOperations;
struct InstallOptions {
    bool registerInstallation = true;
    bool startMenuShortcut = true;
    bool desktopShortcut = false;
    bool startWithWindows = false;  // accepted setup choice or explicit CLI consent; no API auto-enable
    bool allowDowngrade = false;
    RegistryPaths registry;
    IProcessOperations* processes = nullptr;  // tests inject fakes; production defaults to the instance channel
    CancellationToken cancellation;  // observed during staging and immediately before activation
};
[[nodiscard]] Result<void> RegisterInstallation(const InstallLayout& layout, std::string_view version,
    const InstallOptions& options);
[[nodiscard]] Result<void> RemoveRegistration(const InstallLayout& layout, const InstallOptions& options);
[[nodiscard]] Result<void> InstallPackage(const std::filesystem::path& package,
    const InstallLayout& layout, const InstallOptions& options = {});
[[nodiscard]] Result<void> RepairPackage(const std::filesystem::path& package,
    const InstallLayout& layout, const InstallOptions& options = {});
[[nodiscard]] Result<void> Uninstall(const InstallLayout& layout, const std::filesystem::path& dataRoot,
    bool removeUserData, const InstallOptions& options = {});

[[nodiscard]] Result<std::string> MakeHealthToken();
[[nodiscard]] bool IsHealthToken(std::string_view token) noexcept;
[[nodiscard]] Result<void> SignalHealthy(std::string_view token);
class HealthCheck {
public:
    [[nodiscard]] static Result<HealthCheck> Create(std::string_view token);
    [[nodiscard]] Result<bool> Wait(std::chrono::milliseconds timeout) const;
private:
    platform::UniqueHandle event_;
};
[[nodiscard]] std::wstring QuoteArgument(std::wstring_view argument);
struct ActiveVersionProcess {
    DWORD processId = 0;
    platform::UniqueHandle process;
    updates::Sha256Digest stateDigest{};
};
// Retains the exact child handle. An expected journal digest makes a crash retry fail closed if
// maintenance changed/removed the active installation; the check and launch share its lifecycle lock.
[[nodiscard]] Result<ActiveVersionProcess> LaunchActiveVersionProcess(const InstallLayout& layout,
    const std::vector<std::wstring>& arguments = {},
    std::optional<updates::Sha256Digest> expectedState = std::nullopt);
[[nodiscard]] Result<DWORD> LaunchActiveVersion(const InstallLayout& layout,
    const std::vector<std::wstring>& arguments = {});
// Normal installer launches go through the stable supervisor; update health-check children remain
// direct versioned launches while their updater holds the transaction lock.
[[nodiscard]] Result<DWORD> LaunchInstalledShell(const InstallLayout& layout,
    const std::vector<std::wstring>& arguments = {});
// Nonblocking lifecycle lock first, then cancel supervision, then dispatch Quit/recovery. The
// supervisor's own launch uses the same nonblocking lock, so it cannot respawn across this command.
[[nodiscard]] Result<DWORD> LaunchControlCommand(const InstallLayout& layout,
    const std::vector<std::wstring>& arguments);

class IProcessOperations {
public:
    virtual ~IProcessOperations() = default;
    virtual Result<void> RequestExit(std::chrono::milliseconds timeout) = 0;
    virtual Result<DWORD> Launch(const std::filesystem::path& executable,
        const std::vector<std::wstring>& arguments) = 0;
    virtual Result<bool> WaitForHealth(const HealthCheck& health, std::chrono::milliseconds timeout) = 0;
    // Only an unhealthy child started by this updater may be terminated after a failed exit request.
    virtual Result<void> StopUnhealthyChild(DWORD child) = 0;
};
[[nodiscard]] Result<updates::UpdateManifest> VerifyUpdatePackage(const std::filesystem::path& package,
    const std::filesystem::path& manifest, const std::filesystem::path& signature);
[[nodiscard]] Result<void> ApplyUpdate(const std::filesystem::path& package,
    const std::filesystem::path& manifest, const std::filesystem::path& signature,
    const InstallLayout& layout, const std::filesystem::path& dataRoot,
    IProcessOperations* processes = nullptr, std::chrono::milliseconds healthTimeout = std::chrono::seconds(90));

}  // namespace aether::install
