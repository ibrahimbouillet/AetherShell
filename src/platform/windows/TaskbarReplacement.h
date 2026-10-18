// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"
#include "platform/windows/Handles.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace aether::platform {

struct TaskbarWindowState { HWND window = nullptr; bool visible = false; };
struct TaskbarSnapshot { unsigned state = 0; std::vector<TaskbarWindowState> windows; };

// Injectable for pure tests. The real backend touches Explorer only after explicit user consent.
class ITaskbarBackend {
public:
    virtual ~ITaskbarBackend() = default;
    virtual Result<unsigned> State() = 0;
    virtual Result<void> SetState(unsigned state) = 0;
    virtual Result<std::vector<TaskbarWindowState>> Windows() = 0;
    virtual Result<void> SetVisible(HWND window, bool visible) = 0;
};

// Shell window class names are a compatibility detail, not a supported Windows replacement API.
// Blocking shell/process operations: use a worker. Never replace Explorer or change Winlogon.
[[nodiscard]] Result<std::unique_ptr<ITaskbarBackend>> MakeWindowsTaskbarBackend();
[[nodiscard]] Result<TaskbarSnapshot> CaptureTaskbars(ITaskbarBackend& backend);
[[nodiscard]] Result<void> RestoreTaskbars(ITaskbarBackend& backend, const TaskbarSnapshot& snapshot);

// Recovery without a running Aether Shell (D21, ADR 0009 amendment). The baseline is the taskbar
// state captured before anything is hidden, persisted in the per-user configuration directory.
// Saved HWND values are hints only: the Windows backend touches a window only while it is still an
// Explorer taskbar window, and RestoreTaskbars also shows taskbars created by a restarted Explorer.
[[nodiscard]] std::filesystem::path TaskbarBaselineFile(const std::filesystem::path& configDirectory);
[[nodiscard]] Result<void> SaveTaskbarBaseline(const std::filesystem::path& file, const TaskbarSnapshot& snapshot);
// Empty when no baseline exists; an error when the file is unreadable or malformed.
[[nodiscard]] Result<std::optional<TaskbarSnapshot>> LoadTaskbarBaseline(const std::filesystem::path& file);
[[nodiscard]] Result<void> RemoveTaskbarBaseline(const std::filesystem::path& file);

using TaskbarBackendFactory = std::function<Result<std::unique_ptr<ITaskbarBackend>>()>;
enum class TaskbarRecovery { NothingToRestore, RestoredFromBaseline, ShownWithoutBaseline };

// Blocking; call on a worker, never while Aether Shell's own guard is active. A valid baseline
// restores the auto-hide state and window visibility. Without a baseline nothing changes unless
// `showWithoutBaseline` (an explicit user command) is set; an unreadable baseline only makes the
// current taskbar windows visible, since the original auto-hide state is unknown. The file is
// removed only after success, so a failed attempt can be retried. The backend is created only
// when something has to be restored.
[[nodiscard]] Result<TaskbarRecovery> RecoverTaskbars(
    const std::filesystem::path& baselineFile, bool showWithoutBaseline,
    const TaskbarBackendFactory& makeBackend = TaskbarBackendFactory(MakeWindowsTaskbarBackend));

// Runs before a guard session hides anything. It first restores a baseline left by an earlier
// crash, so that a hidden taskbar is never captured as the new baseline, then captures and persists
// the current state. Fails closed: no snapshot is returned unless it was saved.
[[nodiscard]] Result<TaskbarSnapshot> PrepareTaskbarBaseline(ITaskbarBackend& backend,
                                                             const std::filesystem::path& baselineFile);

class TaskbarVisibilityLease {
public:
    explicit TaskbarVisibilityLease(ITaskbarBackend& backend) : backend_(backend) {}
    ~TaskbarVisibilityLease();
    Result<void> Begin();
    Result<void> Enforce();
    Result<void> End();
private:
    ITaskbarBackend& backend_;
    TaskbarSnapshot original_;
    unsigned lastApplied_ = 0;
    bool active_ = false;
};

class TaskbarGuardSession {
public:
    ~TaskbarGuardSession();
    TaskbarGuardSession(const TaskbarGuardSession&) = delete;
    TaskbarGuardSession& operator=(const TaskbarGuardSession&) = delete;
    // Persists the baseline to `baselineFile` before the helper hides anything; the file is
    // removed once restoration is confirmed.
    static Result<std::unique_ptr<TaskbarGuardSession>> Start(
        const std::filesystem::path& versionedLauncher, const std::filesystem::path& baselineFile,
        const CancellationToken& cancellation = {});
    Result<bool> Running();
    Result<void> Stop();
private:
    TaskbarGuardSession() = default;
    UniqueHandle stop_, process_, restoredEvent_;
    TaskbarSnapshot backup_;
    std::filesystem::path baselineFile_;
    bool restored_ = false, baselineRemoved_ = false;
};

// Internal launcher mode. All four handles must be inherited via an explicit handle list.
// The helper checks its parent image and session before changing native taskbar visibility.
[[nodiscard]] Result<int> RunTaskbarGuard(HANDLE parent, HANDLE ready, HANDLE stop, HANDLE restored);

}  // namespace aether::platform
