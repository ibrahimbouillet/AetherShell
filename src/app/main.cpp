// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Localization.h"
#include "app/ShellApplication.h"
#include "app/CrashHandler.h"
#include "app/WindowCorners.h"
#include "aether/BuildInfo.h"
#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/DpiUtilities.h"

#include <commctrl.h>
#include <future>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace aether;
    try {
        {   // D70/D74: until Settings are read, the language saved by the earlier run (any version), else Windows'
            const auto root = platform::DefaultDataRoot();
            i18n::SetLanguage(root ? i18n::StartupLanguage(*root / L"config" / L"settings.json") : i18n::SystemLanguage());
        }
        const auto arguments = app::ProcessArguments();
        auto guard = app::DispatchWindowCornerGuard(arguments);
        if (!guard) return 2;
        if (guard->has_value()) return **guard;
        auto command = app::ParseCommandLine(arguments);
        // Documented native fallbacks: these run before any window or renderer exists.
        if (!command) { ::MessageBoxW(nullptr,std::wstring(i18n::Tr(L"Unrecognized or invalid Aether Shell command line.")).c_str(),L"Aether Shell",MB_OK|MB_ICONERROR); return 2; }
        if (command->showVersion) { ::MessageBoxW(nullptr, (L"Aether Shell " + std::wstring(build::kVersionW)).c_str(), L"Aether Shell", MB_OK); return 0; }
        // Bootstrap IO completes before the UI exists; it still executes on a background worker.
        WorkerPool bootstrap{{1,L"Aether bootstrap",ComApartment::None}};
        std::promise<Result<platform::AppPaths>> promise; auto future=promise.get_future();
        bootstrap.Post([&promise,root=command->dataDirectory] {
            try { auto paths=platform::AppPaths::Resolve(root); if(paths){ diag::LoggerOptions log; log.directory=paths->logDir; diag::Logger::Initialize(log); app::InstallCrashHandler(paths->logDir); } promise.set_value(std::move(paths)); }
            catch (...) { promise.set_value(MakeError(ErrorCode::Internal,"application bootstrap failed")); }
        });
        auto paths=future.get(); bootstrap.Shutdown();
        if(!paths) { ::MessageBoxW(nullptr,std::wstring(i18n::Tr(L"Aether Shell could not create its local data folders.")).c_str(),L"Aether Shell",MB_OK|MB_ICONERROR); return 1; }
        platform::EnsurePerMonitorDpiAwareness(); platform::ComApartment apartment;
        if(!apartment.IsUsable())return 1;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES|ICC_HOTKEY_CLASS}; ::InitCommonControlsEx(&controls);
        int code=1;
        { app::ShellApplication application; auto result=application.Run(std::move(*command),std::move(*paths)); if(result)code=*result; }
        diag::Logger::Shutdown(); return code;
    } catch (...) { ::MessageBoxW(nullptr,std::wstring(i18n::Tr(L"Aether Shell stopped after an unexpected error.")).c_str(),L"Aether Shell",MB_OK|MB_ICONERROR); return 1; }
}
