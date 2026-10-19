// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// IApplicationLauncher backed by the Windows shell.
//
// Launch routes, tried in order (see LaunchRules.h for when a failure falls through to the next one):
//   1. The Apps-folder item "shell:AppsFolder\<parsing name>", executed by PIDL with ShellExecuteEx
//      (SEE_MASK_INVOKEIDLIST, which implies SEE_MASK_IDLIST, plus SEE_MASK_FLAG_NO_UI). This is how
//      Start launches entries, so packaged apps, desktop shortcuts with their arguments and shell
//      objects all behave as they do from Start.
//   2. IApplicationActivationManager::ActivateApplication for packaged AUMIDs
//      ("<PackageFamilyName>!<AppId>").
//   3. ShellExecuteEx on the validated .lnk path (the shortcut supplies its own arguments and working
//      directory).
//   4. ShellExecuteEx on the validated executable path, with its folder as working directory and no
//      arguments. Entries whose arguments came from a shortcut that no longer exists are refused
//      rather than started without them.
// Command lines are never built from text: ShellExecuteEx receives a PIDL or a file path, and
// arguments only ever come from the shortcut file itself.
//
// Log lines name the launch route and the identity scheme, never the display name: a running
// group's name can be a browser profile name chosen by a person (see RunningPresentation.h).
//
// Threading: launches run on a dedicated single-threaded-apartment worker (shell verbs and context
// menu handlers require an STA); completions are posted to the UI dispatcher. Activate() runs on the
// UI thread. The destructor discards queued launches and drops completions that have not run yet, but
// waits for a launch already in progress: ShellExecuteEx (SEE_MASK_NOASYNC) does not return while a
// UAC consent prompt it raised is open, so destroying the launcher at that moment waits for the
// user's answer. Destroy it at shutdown only.
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Dispatcher.h"
#include "core/WorkerPool.h"

#include <functional>
#include <memory>

namespace aether::apps {

class ShellApplicationLauncher final : public IApplicationLauncher {
public:
    explicit ShellApplicationLauncher(IUiDispatcher& dispatcher);
    ~ShellApplicationLauncher() override;
    ShellApplicationLauncher(const ShellApplicationLauncher&) = delete;
    ShellApplicationLauncher& operator=(const ShellApplicationLauncher&) = delete;

    // `completion` (optional) runs on the UI thread with Ok() once Windows accepted the launch, or
    // with a user-presentable error: NotFound (nothing launchable left), Cancelled (UAC prompt
    // declined), AccessDenied, Unsupported, InvalidArgument (invalid paths), Platform.
    void LaunchAsync(const ApplicationInfo& app, std::function<void(Result<void>)> completion) override;

    // UI thread only (InvalidState otherwise). See IApplicationLauncher and ChooseWindowToActivate:
    // activates, restores a minimized window, cycles several windows, and minimizes the application's
    // only window when it is already in front (ShowWindowAsync, so a hung application cannot block).
    // Never uses AttachThreadInput or synthesized input to get around the foreground lock: when
    // SetForegroundWindow is refused the window's taskbar button flashes and AccessDenied
    // ("Windows prevented activation") is returned.
    Result<void> Activate(const RunningApplication& app) override;

    // The launch itself, synchronously on the calling thread (which must be able to host an STA; see
    // shell::RunOnSta). Used by LaunchAsync's worker; exposed for diagnostics tools.
    [[nodiscard]] static Result<void> LaunchNow(const ApplicationInfo& app);

private:
    struct State;
    std::shared_ptr<State> state_;
    std::unique_ptr<WorkerPool> pool_;
};

}  // namespace aether::apps
