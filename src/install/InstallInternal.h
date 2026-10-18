// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include "install/Install.h"
#include "core/Json.h"

namespace aether::install::detail {
class InstallLock {
public:
    InstallLock() = default;
    InstallLock(InstallLock&& other) noexcept : mutex_(std::move(other.mutex_)), held_(std::exchange(other.held_,false)) {}
    ~InstallLock() { if(held_) ::ReleaseMutex(mutex_.Get()); }
    InstallLock(const InstallLock&) = delete;
    static Result<InstallLock> Acquire(const InstallLayout& layout);
private:
    platform::UniqueHandle mutex_;
    bool held_ = false;
};
Result<void> Safe(const InstallLayout& layout, const std::filesystem::path& path);
Result<std::wstring> CanonicalRoot(const std::filesystem::path& root);
Result<void> Remove(const InstallLayout& layout, const std::filesystem::path& path);
Result<void> CopyAtomic(const std::filesystem::path& from, const std::filesystem::path& to);
Result<void> CopyStable(const std::filesystem::path& package,const InstallLayout& layout,const InstallState& state);
// Internal transaction seam. Production enters only after VerifyUpdatePackage succeeds while
// holding the package against replacement. Tests supply their own non-executable resource DLL.
Result<void> ApplyVerifiedUpdate(const std::filesystem::path& package,const updates::SemanticVersion& version,
    const InstallLayout& layout,const std::filesystem::path& dataRoot,IProcessOperations& processes,
    std::chrono::milliseconds healthTimeout);
Result<std::string> Resource(HMODULE module, std::string_view name, std::uint64_t limit);
Result<void> WriteState(const InstallLayout& layout, InstallState& state, TransactionState next);
std::unique_ptr<IProcessOperations> WindowsProcesses();
std::filesystem::path Staging(const InstallLayout& layout, const InstallTransaction& transaction);
std::filesystem::path Backup(const InstallLayout& layout, const InstallTransaction& transaction);
bool SafeName(std::string_view name);
std::string UtcNow();
}
