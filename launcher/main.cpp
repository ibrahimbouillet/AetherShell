// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/Install.h"
#include "platform/windows/TaskbarReplacement.h"

#include <shellapi.h>
#include <array>
#include <charconv>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    try {
        std::array<wchar_t,32768> executable{};
        const DWORD length=::GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        if(!length || length>=executable.size()) return 2;
        int count=0; auto* argv=::CommandLineToArgvW(::GetCommandLineW(),&count); if(!argv) return 2;
        std::vector<std::wstring> arguments; for(int i=1;i<count;++i) arguments.emplace_back(argv[i]); ::LocalFree(argv);
        if (!arguments.empty() && arguments.front() == L"--taskbar-guard") {
            if (arguments.size() != 5) return 2;
            std::array<HANDLE,4> handles{};
            for (std::size_t i=0;i<handles.size();++i) {
                std::string text;
                for (const auto c : arguments[i+1]) {
                    if (c<L'0' || c>L'9') return 2;
                    text.push_back(static_cast<char>(c));
                }
                std::uintptr_t value=0;
                const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
                if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !value)return 2;
                handles[i]=reinterpret_cast<HANDLE>(value);
            }
            auto guarded=aether::platform::RunTaskbarGuard(handles[0],handles[1],handles[2],handles[3]); return guarded ? *guarded : 1;
        }
        auto layout=aether::install::InstallLayout::Create(std::filesystem::path(executable.data()).parent_path());
        if(!layout) return 2;
        auto launched=aether::install::LaunchActiveVersion(layout.value(),arguments);
        if(!launched) {
            const bool busy=launched.error().code==aether::ErrorCode::Unavailable;
            ::MessageBoxW(nullptr,busy?L"Aether Shell is being installed or updated. Wait for that operation to finish, then try again.":L"Aether Shell could not start. Choose Repair in Windows Settings > Apps > Installed apps.",L"Aether Shell",MB_OK|(busy?MB_ICONINFORMATION:MB_ICONERROR));return busy?3:4;
        }
        return 0;
    } catch(...) { return 5; }
}
