// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/ShellApplicationLauncher.h"

#include "applications/ApplicationIdentity.h"
#include "applications/LaunchRules.h"
#include "applications/ShellItems.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>

#include <atomic>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

namespace aether::apps {
namespace {

constexpr std::string_view kLog = "applications";

enum class Route { AppsFolder, Activation, Shortcut, Executable };

std::string_view RouteName(Route route) noexcept {
    switch (route) {
        case Route::AppsFolder: return "Apps folder";
        case Route::Activation: return "activation manager";
        case Route::Shortcut: return "shortcut";
        case Route::Executable: return "executable";
    }
    return "unknown";
}

std::wstring NameOf(const ApplicationInfo& app) {
    const auto name = text::Trim(app.displayName);
    if (!name.empty()) return std::wstring(name);
    if (!app.executablePath.empty()) return std::filesystem::path(app.executablePath).stem().native();
    return {};
}

bool IsExistingFile(const std::wstring& path) noexcept {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

Result<void> ExecuteItem(IShellItem* item, std::wstring_view name) {
    PIDLIST_ABSOLUTE raw = nullptr;
    const HRESULT hr = ::SHGetIDListFromObject(item, &raw);
    if (FAILED(hr) || !raw) return MakeLaunchErrorFromHResult(FAILED(hr) ? hr : E_FAIL, name);
    const shell::UniqueIdList idList(raw);

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    // SEE_MASK_INVOKEIDLIST implies SEE_MASK_IDLIST and runs the item's default context-menu verb,
    // which is what Apps-folder items (not file-system objects) need. SEE_MASK_FLAG_NO_UI: no error
    // message boxes (the UAC consent prompt is not an error UI and still appears). SEE_MASK_NOASYNC:
    // report the outcome synchronously; this thread stays alive anyway.
    info.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpIDList = idList.get();
    info.nShow = SW_SHOWNORMAL;
    if (!::ShellExecuteExW(&info)) return MakeLaunchError(::GetLastError(), name);
    return Ok();
}

Result<void> ExecuteFile(const std::wstring& path, const wchar_t* workingDirectory, std::wstring_view name) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpFile = path.c_str();       // a file, never a command line
    info.lpParameters = nullptr;      // .lnk files carry their own arguments
    info.lpDirectory = workingDirectory;
    info.nShow = SW_SHOWNORMAL;
    if (!::ShellExecuteExW(&info)) return MakeLaunchError(::GetLastError(), name);
    return Ok();
}

Result<void> ActivatePackaged(const std::wstring& aumid, std::wstring_view name) {
    winrt::com_ptr<IApplicationActivationManager> manager;
    HRESULT hr = ::CoCreateInstance(__uuidof(ApplicationActivationManager), nullptr, CLSCTX_LOCAL_SERVER,
                                    IID_PPV_ARGS(manager.put()));
    if (FAILED(hr)) return MakeLaunchErrorFromHResult(hr, name);
    // Lets the activated app take the foreground (works only while this process may set it, i.e.
    // right after the user clicked the Dock). Failure only means the app may open behind.
    (void)::CoAllowSetForegroundWindow(manager.get(), nullptr);
    DWORD processId = 0;
    hr = manager->ActivateApplication(aumid.c_str(), nullptr, AO_NONE, &processId);
    if (FAILED(hr)) return MakeLaunchErrorFromHResult(hr, name);
    return Ok();
}

// One launch route: nullopt when the route does not apply or its target does not exist.
using RouteOutcome = std::optional<Result<void>>;

RouteOutcome TryAppsFolder(const ApplicationInfo& app, std::wstring_view name) {
    if (text::Trim(app.parsingName).empty()) return std::nullopt;
    auto item = shell::CreateAppsFolderItem(app.parsingName);
    if (!item) return std::nullopt;
    return ExecuteItem(item.value().get(), name);
}

RouteOutcome TryActivation(const ApplicationInfo& app, std::wstring_view name) {
    const auto aumid = text::Trim(app.appUserModelId);
    if (!IsPackagedAumid(aumid)) return std::nullopt;
    return ActivatePackaged(std::wstring(aumid), name);
}

RouteOutcome TryShortcut(const ApplicationInfo& app, std::wstring_view name) {
    if (app.shortcutPath.empty()) return std::nullopt;
    auto path = NormalizeShortcutPath(app.shortcutPath);
    if (!path || !IsExistingFile(path.value())) return std::nullopt;
    return ExecuteFile(path.value(), nullptr, name);
}

// Display names are untrusted (shell or shortcut file names): bounded, control characters removed,
// like the names inside MakeLaunchError's messages.
std::string NameInMessage(std::wstring_view name) {
    const auto trimmed = text::Trim(name);
    return trimmed.empty() ? std::string("the application") : shell::ForLog(trimmed, 120);
}

RouteOutcome TryExecutable(const ApplicationInfo& app, std::wstring_view name) {
    if (app.executablePath.empty()) return std::nullopt;
    auto path = NormalizeExecutablePath(app.executablePath);
    if (!path) {
        return Result<void>(MakeError(ErrorCode::InvalidArgument,
                                      "The location of " + NameInMessage(name) + " is not a valid application path."));
    }
    if (!IsExistingFile(path.value())) return std::nullopt;
    if (!text::Trim(app.arguments).empty()) {
        // The arguments belong to a shortcut that is gone; starting the bare executable would run
        // something else than what the user picked (and arguments are never passed from data).
        return Result<void>(
            MakeError(ErrorCode::NotFound, "The shortcut for " + NameInMessage(name) + " no longer exists."));
    }
    const std::wstring folder = std::filesystem::path(path.value()).parent_path().native();
    return ExecuteFile(path.value(), folder.empty() ? nullptr : folder.c_str(), name);
}

Result<void> Launch(const ApplicationInfo& app) {
    const std::wstring name = NameOf(app);
    std::optional<Error> lastFailure;
    for (const Route route : {Route::AppsFolder, Route::Activation, Route::Shortcut, Route::Executable}) {
        RouteOutcome outcome;
        switch (route) {
            case Route::AppsFolder: outcome = TryAppsFolder(app, name); break;
            case Route::Activation: outcome = TryActivation(app, name); break;
            case Route::Shortcut: outcome = TryShortcut(app, name); break;
            case Route::Executable: outcome = TryExecutable(app, name); break;
        }
        if (!outcome) continue;
        if (outcome->ok()) {
            AETHER_LOG_INFO(kLog, "launched {} through the {}", shell::ForLog(name), RouteName(route));
            return Ok();
        }
        const Error& error = outcome->error();
        AETHER_LOG_WARN(kLog, "launching {} through the {} failed: {}", shell::ForLog(name), RouteName(route),
                        error.Describe());
        if (!ShouldTryNextLaunchRoute(error)) return error;
        lastFailure = error;
    }
    if (lastFailure) return *lastFailure;
    AETHER_LOG_WARN(kLog, "nothing launchable for {}", shell::ForLog(name));
    return MakeLaunchError(ERROR_FILE_NOT_FOUND, name);
}

}  // namespace

struct ShellApplicationLauncher::State {
    explicit State(IUiDispatcher& uiDispatcher) : dispatcher(uiDispatcher) {}
    IUiDispatcher& dispatcher;     // used only while the launcher (and its pool) is alive
    std::atomic<bool> alive{true};  // completions posted before destruction check it on the UI thread
};

ShellApplicationLauncher::ShellApplicationLauncher(IUiDispatcher& dispatcher)
    : state_(std::make_shared<State>(dispatcher)),
      pool_(std::make_unique<WorkerPool>(
          WorkerPool::Options{1, L"Aether launcher", ComApartment::SingleThreaded})) {}

ShellApplicationLauncher::~ShellApplicationLauncher() {
    state_->alive = false;
    pool_->Shutdown();
}

Result<void> ShellApplicationLauncher::LaunchNow(const ApplicationInfo& app) {
    return shell::RunOnSta<void>([&app] { return Launch(app); }, "launching an application");
}

void ShellApplicationLauncher::LaunchAsync(const ApplicationInfo& app, std::function<void(Result<void>)> completion) {
    auto deliver = [state = state_](std::function<void(Result<void>)> callback, Result<void> result) {
        if (!callback) return;
        state->dispatcher.Post([state, callback = std::move(callback), result = std::move(result)] {
            if (!state->alive) return;
            try {
                callback(result);
            } catch (...) {
                AETHER_LOG_ERROR(kLog, "launch completion threw: {}",
                                 shell::CurrentExceptionToError("launch completion").Describe());
            }
        });
    };
    try {
        auto task = [deliver, app, completion]() mutable {
            Result<void> result = LaunchNow(app);
            deliver(std::move(completion), std::move(result));
        };
        if (!pool_->Post(std::move(task))) {
            deliver(std::move(completion), MakeError(ErrorCode::InvalidState, "The launcher is shutting down."));
        }
    } catch (...) {
        deliver(std::move(completion), shell::CurrentExceptionToError("queueing a launch"));
    }
}

Result<void> ShellApplicationLauncher::Activate(const RunningApplication& app) {
    if (!state_->dispatcher.IsUiThread()) {
        return MakeError(ErrorCode::InvalidState, "Activate must be called on the UI thread");
    }
    try {
        std::vector<RunningWindow> live;
        live.reserve(app.windows.size());
        for (const auto& window : app.windows) {
            if (!window.handle || !::IsWindow(window.handle)) continue;
            live.push_back(RunningWindow{window.handle, window.title, ::IsIconic(window.handle) != FALSE});
        }
        const std::string name =
            app.displayName.empty() ? std::string("The application") : shell::ForLog(app.displayName, 120);
        if (live.empty()) return MakeError(ErrorCode::NotFound, name + " has no open window.");

        const HWND foreground = ::GetForegroundWindow();
        const HWND foregroundRoot = foreground ? ::GetAncestor(foreground, GA_ROOTOWNER) : nullptr;
        const auto choice = ChooseWindowToActivate(live, foreground, foregroundRoot);
        if (!choice) return MakeError(ErrorCode::NotFound, name + " has no open window.");
        if (choice->alreadyActive) return Ok();

        const HWND target = choice->window;
        if (::IsIconic(target)) ::ShowWindowAsync(target, SW_RESTORE);  // asynchronous: a hung app cannot block us
        if (!::SetForegroundWindow(target)) {
            // Foreground lock: Windows decides; tell the user where the window is instead.
            FLASHWINFO flash{};
            flash.cbSize = sizeof(flash);
            flash.hwnd = target;
            flash.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
            ::FlashWindowEx(&flash);
            return MakeError(ErrorCode::AccessDenied, "Windows prevented activation");
        }
        return Ok();
    } catch (...) {
        return shell::CurrentExceptionToError("activating an application");
    }
}

}  // namespace aether::apps
