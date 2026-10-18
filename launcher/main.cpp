// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/Install.h"

#include <shellapi.h>
#include <array>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    try {
        std::array<wchar_t,32768> executable{};
        const DWORD length=::GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        if(!length || length>=executable.size()) return 2;
        auto layout=aether::install::InstallLayout::Create(std::filesystem::path(executable.data()).parent_path());
        if(!layout) return 2;
        int count=0; auto* argv=::CommandLineToArgvW(::GetCommandLineW(),&count); if(!argv) return 2;
        std::vector<std::wstring> arguments; for(int i=1;i<count;++i) arguments.emplace_back(argv[i]); ::LocalFree(argv);
        auto launched=aether::install::LaunchActiveVersion(layout.value(),arguments);
        if(!launched) {
            const bool busy=launched.error().code==aether::ErrorCode::Unavailable;
            ::MessageBoxW(nullptr,busy?L"Aether Shell is being installed or updated. Wait for that operation to finish, then try again.":L"Aether Shell could not start. Choose Repair in Windows Settings > Apps > Installed apps.",L"Aether Shell",MB_OK|(busy?MB_ICONINFORMATION:MB_ICONERROR));return busy?3:4;
        }
        return 0;
    } catch(...) { return 5; }
}
