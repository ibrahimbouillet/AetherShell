// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/Install.h"
#include "core/Strings.h"
#include "core/FileSystem.h"
#include "platform/windows/ComApartment.h"
#include "settings/StartupRegistration.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <winrt/base.h>
#include <array>
#include <algorithm>

namespace aether::install {
namespace {
Result<void> RegString(HKEY key,const wchar_t* name,std::wstring_view value) {
    const auto bytes=static_cast<DWORD>((value.size()+1)*sizeof(wchar_t));
    const LSTATUS status=::RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.data()),bytes);
    return status==ERROR_SUCCESS?Ok():Result<void>(MakeError(ErrorCode::Platform,"cannot write uninstall registration",status));
}
Result<std::filesystem::path> ShortcutPath(REFKNOWNFOLDERID folder) {
    PWSTR path=nullptr; const HRESULT hr=::SHGetKnownFolderPath(folder,KF_FLAG_DEFAULT,nullptr,&path);
    if(FAILED(hr)) return MakeError(ErrorCode::Platform,"cannot find shortcut directory",hr);
    const std::filesystem::path result=std::filesystem::path(path)/L"Aether Shell.lnk"; ::CoTaskMemFree(path); return result;
}
Result<void> Shortcut(const InstallLayout& layout,REFKNOWNFOLDERID folder,bool remove) {
    auto path=ShortcutPath(folder); if(!path) return path.error();
    winrt::com_ptr<IShellLinkW> link; HRESULT hr=::CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(link.put()));
    if(FAILED(hr)) return MakeError(ErrorCode::Platform,"cannot create shortcut interface",hr);
    auto persist=link.as<IPersistFile>();
    if(remove) {
        if(!std::filesystem::exists(path.value())) return Ok();
        if(FAILED(persist->Load(path->c_str(),STGM_READ))) return MakeError(ErrorCode::Io,"cannot inspect project shortcut");
        std::array<wchar_t,32768> target{};
        if(FAILED(link->GetPath(target.data(),static_cast<int>(target.size()),nullptr,SLGP_RAWPATH))) return MakeError(ErrorCode::Io,"cannot inspect shortcut target");
        if(::_wcsicmp(target.data(),layout.Launcher().c_str())!=0) return Ok();
        std::error_code error; std::filesystem::remove(path.value(),error); return error?Result<void>(MakeError(ErrorCode::Io,"cannot remove project shortcut",error.value())):Ok();
    }
    hr=link->SetPath(layout.Launcher().c_str()); if(SUCCEEDED(hr)) hr=link->SetDescription(L"Aether Shell — native Dock and Top Status Bar");
    if(SUCCEEDED(hr)) hr=link->SetWorkingDirectory(layout.root.c_str());
    if(SUCCEEDED(hr)) hr=link->SetIconLocation(layout.Launcher().c_str(),0);
    if(SUCCEEDED(hr)) hr=persist->Save(path->c_str(),TRUE);
    return SUCCEEDED(hr)?Ok():Result<void>(MakeError(ErrorCode::Platform,"cannot save project shortcut",hr));
}
}
Result<void> RegisterInstallation(const InstallLayout& layout,std::string_view version,const InstallOptions& options) {
    try {
        platform::ComApartment apartment(COINIT_MULTITHREADED);
        if(!apartment.IsUsable()) return MakeError(ErrorCode::Platform,"cannot initialize shortcut COM",apartment.Status());
        if(options.registerInstallation) {
            platform::UniqueRegKey key; const auto status=::RegCreateKeyExW(HKEY_CURRENT_USER,options.registry.uninstall.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,key.Put(),nullptr);
            if(status!=ERROR_SUCCESS) return MakeError(ErrorCode::Platform,"cannot create per-user uninstall registration",status);
            const std::array<std::pair<const wchar_t*,std::wstring>,9> values{{
                {L"DisplayName",L"Aether Shell"},{L"DisplayVersion",text::Widen(version)},{L"Publisher",L"Ibrahim Bouillet"},
                {L"InstallLocation",layout.root.native()},{L"DisplayIcon",layout.Launcher().native()},
                {L"UninstallString",QuoteArgument(layout.Maintenance().native())+L" --uninstall"},
                {L"ModifyPath",QuoteArgument(layout.Maintenance().native())+L" --repair"},
                {L"URLInfoAbout",L"https://github.com/ibrahimbouillet/AetherShell"},{L"QuietUninstallString",QuoteArgument(layout.Maintenance().native())+L" --silent --uninstall --keep-user-data"}}};
            for(const auto& [name,value]:values) { auto saved=RegString(key.Get(),name,value); if(!saved) return saved; }
            auto folder=layout.Version(version); if(!folder) return folder.error();
            auto bytes=fs::ReadFile(folder.value()/L"payload-manifest.json",64*1024); if(!bytes) return bytes.error();
            auto payload=ParsePayloadManifest(bytes.value()); if(!payload) return payload.error();
            std::uint64_t size=bytes->size(); for(const auto& file:payload->files) size+=file.size;
            size+=std::filesystem::file_size(layout.Launcher())+std::filesystem::file_size(layout.Maintenance());
            const DWORD estimatedSize=static_cast<DWORD>(std::min<std::uint64_t>((size+1023)/1024,MAXDWORD));
            const auto written=::RegSetValueExW(key.Get(),L"EstimatedSize",0,REG_DWORD,reinterpret_cast<const BYTE*>(&estimatedSize),sizeof(estimatedSize));
            if(written!=ERROR_SUCCESS) return MakeError(ErrorCode::Platform,"cannot write estimated install size",written);
        }
        if(options.startMenuShortcut) { auto saved=Shortcut(layout,FOLDERID_Programs,false); if(!saved) return saved; }
        if(options.desktopShortcut) { auto saved=Shortcut(layout,FOLDERID_Desktop,false); if(!saved) return saved; }
        if(options.startWithWindows) {
            auto registry=settings::MakeWindowsStartupRegistry({options.registry.run,options.registry.startupApproval}); if(!registry) return registry.error();
            return settings::StartupRegistration(registry.value()).Enable(layout.Launcher());
        }
        return Ok();
    } catch(...) { return MakeError(ErrorCode::Platform,"cannot register per-user installation"); }
}
Result<void> RemoveRegistration(const InstallLayout& layout,const InstallOptions& options) {
    try {
        platform::ComApartment apartment(COINIT_MULTITHREADED);
        if(options.registerInstallation) {
            platform::UniqueRegKey key; const auto opened=::RegOpenKeyExW(HKEY_CURRENT_USER,options.registry.uninstall.c_str(),0,KEY_QUERY_VALUE,key.Put());
            if(opened==ERROR_SUCCESS) {
                std::array<wchar_t,32768> location{}; DWORD bytes=static_cast<DWORD>(location.size()*sizeof(wchar_t));
                if(::RegGetValueW(key.Get(),nullptr,L"InstallLocation",RRF_RT_REG_SZ,nullptr,location.data(),&bytes)==ERROR_SUCCESS && ::_wcsicmp(location.data(),layout.root.c_str())==0) {
                    key.Reset(); const auto removed=::RegDeleteTreeW(HKEY_CURRENT_USER,options.registry.uninstall.c_str());
                    if(removed!=ERROR_SUCCESS && removed!=ERROR_FILE_NOT_FOUND) return MakeError(ErrorCode::Platform,"cannot remove uninstall registration",removed);
                }
            } else if(opened!=ERROR_FILE_NOT_FOUND) return MakeError(ErrorCode::Platform,"cannot inspect uninstall registration",opened);
        }
        if(options.registerInstallation || options.startWithWindows) {
            auto registry=settings::MakeWindowsStartupRegistry({options.registry.run,options.registry.startupApproval}); if(!registry) return registry.error();
            auto run=registry.value()->ReadRun(); if(!run) return run.error();
            const auto expected=QuoteArgument(layout.Launcher().native())+L" --autostart";
            if(run.value() && ::_wcsicmp(run.value()->c_str(),expected.c_str())==0) { auto removed=settings::StartupRegistration(registry.value()).Disable(); if(!removed) return removed; }
        }
        if(options.startMenuShortcut) { auto removed=Shortcut(layout,FOLDERID_Programs,true); if(!removed) return removed; }
        if(options.desktopShortcut) { auto removed=Shortcut(layout,FOLDERID_Desktop,true); if(!removed) return removed; }
        return Ok();
    } catch(...) { return MakeError(ErrorCode::Platform,"cannot remove installation registration"); }
}
}  // namespace aether::install
