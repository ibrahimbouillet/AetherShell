// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/InstallInternal.h"
#include "core/Strings.h"
#include "platform/windows/SingleInstance.h"

#include <bcrypt.h>
#include <array>
#include <map>

namespace aether::install {
namespace {
std::wstring EventName(std::string_view token) { return L"Local\\AetherShell.Health."+text::Widen(token); }
Result<DWORD> WaitMilliseconds(std::chrono::milliseconds timeout) {
    if(timeout.count()<0 || static_cast<std::uint64_t>(timeout.count())>=INFINITE) return MakeError(ErrorCode::InvalidArgument,"invalid process wait timeout");
    return static_cast<DWORD>(timeout.count());
}
Result<DWORD> Launch(const std::filesystem::path& executable,const std::vector<std::wstring>& arguments,
                     std::map<DWORD,platform::UniqueHandle>* children) {
    if(!executable.is_absolute() || !std::filesystem::is_regular_file(executable)) return MakeError(ErrorCode::NotFound,"application executable is missing");
    std::wstring command=QuoteArgument(executable.native()); for(const auto& arg:arguments) { command+=L' '; command+=QuoteArgument(arg); }
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    if(!::CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,executable.parent_path().c_str(),&startup,&process)) return MakeError(ErrorCode::Platform,"cannot start application",::GetLastError());
    platform::UniqueHandle thread(process.hThread), handle(process.hProcess);
    if(children) children->emplace(process.dwProcessId,std::move(handle));
    return process.dwProcessId;
}
class Processes final:public IProcessOperations {
public:
    Result<void> RequestExit(std::chrono::milliseconds timeout) override {
        auto wait=WaitMilliseconds(timeout); if(!wait) return wait.error();
        const DWORD pid=platform::InstanceChannel::FindListenerProcessId(L"AetherShell"); if(!pid) return Ok();
        platform::UniqueHandle process(::OpenProcess(SYNCHRONIZE,FALSE,pid));
        if(!process) return MakeError(ErrorCode::Platform,"cannot wait for application shutdown",::GetLastError());
        auto sent=platform::InstanceChannel::Send(L"AetherShell",L"shutdown-for-update"); if(!sent) return sent;
        const DWORD result=::WaitForSingleObject(process.Get(),wait.value());
        if(result==WAIT_OBJECT_0) return Ok(); return MakeError(result==WAIT_TIMEOUT?ErrorCode::Timeout:ErrorCode::Platform,"application did not shut down",result==WAIT_TIMEOUT?0:static_cast<int>(::GetLastError()));
    }
    Result<DWORD> Launch(const std::filesystem::path& path,const std::vector<std::wstring>& args) override { return install::Launch(path,args,&children_); }
    Result<bool> WaitForHealth(const HealthCheck& health,std::chrono::milliseconds timeout) override { return health.Wait(timeout); }
    Result<void> StopUnhealthyChild(DWORD child) override {
        const auto found=children_.find(child); if(found==children_.end()) return MakeError(ErrorCode::AccessDenied,"process is not a child of this updater");
        if(::WaitForSingleObject(found->second.Get(),0)==WAIT_OBJECT_0) return Ok();
        if(platform::InstanceChannel::FindListenerProcessId(L"AetherShell")==child) {auto requested=RequestExit(std::chrono::seconds(15));(void)requested;}
        else (void)::WaitForSingleObject(found->second.Get(),15000);
        if(::WaitForSingleObject(found->second.Get(),0)==WAIT_OBJECT_0) return Ok();
        if(!::TerminateProcess(found->second.Get(),1)) return MakeError(ErrorCode::Platform,"cannot stop unhealthy update child",::GetLastError());
        if(::WaitForSingleObject(found->second.Get(),5000)!=WAIT_OBJECT_0) return MakeError(ErrorCode::Timeout,"unhealthy update child is still running");
        return Ok();
    }
private:
    std::map<DWORD,platform::UniqueHandle> children_;
};
}
std::unique_ptr<IProcessOperations> detail::WindowsProcesses() { return std::make_unique<Processes>(); }
bool IsHealthToken(std::string_view token) noexcept {
    if(token.size()<16 || token.size()>64) return false;
    for(char c:token) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return false; return true;
}
Result<std::string> MakeHealthToken() {
    std::array<unsigned char,16> bytes{}; const NTSTATUS status=::BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if(status<0) return MakeError(ErrorCode::Platform,"cannot create health token",status);
    constexpr char hex[]="0123456789abcdef"; std::string token; token.reserve(32);
    for(const auto byte:bytes) { token+=hex[byte>>4]; token+=hex[byte&15]; } return token;
}
Result<HealthCheck> HealthCheck::Create(std::string_view token) {
    if(!IsHealthToken(token)) return MakeError(ErrorCode::InvalidArgument,"invalid health token");
    HealthCheck health; health.event_.Reset(::CreateEventW(nullptr,TRUE,FALSE,EventName(token).c_str()));
    if(!health.event_) return MakeError(ErrorCode::Platform,"cannot create health signal",::GetLastError());
    if(::GetLastError()==ERROR_ALREADY_EXISTS) return MakeError(ErrorCode::AlreadyExists,"health signal already exists"); return health;
}
Result<bool> HealthCheck::Wait(std::chrono::milliseconds timeout) const {
    auto wait=WaitMilliseconds(timeout); if(!wait) return wait.error();
    const DWORD result=::WaitForSingleObject(event_.Get(),wait.value()); if(result==WAIT_OBJECT_0) return true; if(result==WAIT_TIMEOUT) return false;
    return MakeError(ErrorCode::Platform,"cannot wait for health signal",::GetLastError());
}
Result<void> SignalHealthy(std::string_view token) {
    if(!IsHealthToken(token)) return MakeError(ErrorCode::InvalidArgument,"invalid health token");
    platform::UniqueHandle event(::OpenEventW(EVENT_MODIFY_STATE,FALSE,EventName(token).c_str()));
    if(!event || !::SetEvent(event.Get())) return MakeError(ErrorCode::Platform,"cannot send health signal",::GetLastError()); return Ok();
}
std::wstring QuoteArgument(std::wstring_view argument) {
    std::wstring result=L"\""; std::size_t slashes=0;
    for(const wchar_t c:argument) {
        if(c==L'\\') { ++slashes; continue; }
        if(c==L'"') { result.append(slashes*2+1,L'\\'); result+=c; }
        else { result.append(slashes,L'\\'); result+=c; }
        slashes=0;
    }
    result.append(slashes*2,L'\\'); result+=L'"'; return result;
}
Result<DWORD> LaunchActiveVersion(const InstallLayout& layout,const std::vector<std::wstring>& arguments) {
    try {
        auto lock=detail::InstallLock::Acquire(layout);if(!lock)return lock.error();
        auto recovered=RecoverInterrupted(layout);if(!recovered)return recovered.error();
        auto state=LoadState(layout); if(!state) return state.error(); auto version=layout.Version(state->activeVersion); if(!version) return version.error();
        return Launch(version.value()/L"AetherShellApp.exe",arguments,nullptr);
    } catch(...) { return MakeError(ErrorCode::Platform,"cannot launch installed application"); }
}
}  // namespace aether::install
