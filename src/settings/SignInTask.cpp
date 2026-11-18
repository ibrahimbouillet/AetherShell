// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/SignInTask.h"

#include "platform/windows/ComApartment.h"
#include "settings/StartupRegistration.h"

#include <windows.h>
#include <oleauto.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <vector>

namespace aether::settings {
namespace {

using Microsoft::WRL::ComPtr;

std::wstring Escaped(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (const wchar_t c : text) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

}  // namespace

std::wstring SignInTaskName(std::wstring_view userSid) {
    return L"Aether Shell at sign-in (" + std::wstring(userSid) + L")";
}

std::wstring SignInTaskXml(std::wstring_view launcher, std::wstring_view userSid) {
    const auto user = Escaped(userSid);
    // Priority 5 is a normal process with normal threads (Task Scheduler's default, 7, is below normal and would
    // start the shell with a low disk and memory priority). No time limit: the launcher supervises the shell for the
    // whole session, and the default limit of three days would end it.
    return LR"(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Author>Aether Shell</Author>
    <Description>Starts Aether Shell as soon as you sign in. It follows Aether Shell's entry in Task Manager's Startup apps and its own Start with Windows setting: when either is off, it starts nothing.</Description>
  </RegistrationInfo>
  <Triggers>
    <LogonTrigger>
      <Enabled>true</Enabled>
      <UserId>)" + user + LR"(</UserId>
    </LogonTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>)" + user + LR"(</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>LeastPrivilege</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <StartWhenAvailable>false</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <IdleSettings>
      <StopOnIdleEnd>false</StopOnIdleEnd>
      <RestartOnIdle>false</RestartOnIdle>
    </IdleSettings>
    <AllowStartOnDemand>true</AllowStartOnDemand>
    <Enabled>true</Enabled>
    <Hidden>false</Hidden>
    <RunOnlyIfIdle>false</RunOnlyIfIdle>
    <WakeToRun>false</WakeToRun>
    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
    <Priority>5</Priority>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>)" + Escaped(launcher) + LR"(</Command>
      <Arguments>--autostart</Arguments>
    </Exec>
  </Actions>
</Task>
)";
}

namespace detail {
namespace {

struct Bstr {
    BSTR value = nullptr;
    explicit Bstr(std::wstring_view text) : value(::SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {}
    Bstr() = default;
    ~Bstr() { if (value) ::SysFreeString(value); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
};

struct Variant {
    VARIANT value;
    Variant() { ::VariantInit(&value); }
    ~Variant() { ::VariantClear(&value); }
    Variant(const Variant&) = delete;
    Variant& operator=(const Variant&) = delete;
};

Error Failed(const char* what, HRESULT hr) {
    return MakeError(hr == E_ACCESSDENIED ? ErrorCode::AccessDenied : ErrorCode::Platform, what, hr);
}

bool NotFound(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

// The root task folder, through a connected Task Scheduler service. `apartment` keeps COM up for the caller.
Result<ComPtr<ITaskFolder>> RootFolder() {
    ComPtr<ITaskService> service;
    HRESULT hr = ::CoCreateInstance(__uuidof(TaskScheduler), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(service.GetAddressOf()));
    if (FAILED(hr)) return Failed("Task Scheduler is unavailable", hr);
    Variant server, user, domain, password;
    hr = service->Connect(server.value, user.value, domain.value, password.value);
    if (FAILED(hr)) return Failed("Task Scheduler could not be reached", hr);
    ComPtr<ITaskFolder> folder;
    const Bstr root(L"\\");
    hr = service->GetFolder(root.value, folder.GetAddressOf());
    if (FAILED(hr)) return Failed("Task Scheduler's task folder could not be opened", hr);
    return folder;
}

}  // namespace

Result<std::wstring> CurrentUserSid() {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return Failed("the account could not be read", HRESULT_FROM_WIN32(::GetLastError()));
    DWORD bytes = 0;
    (void)::GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<std::uint8_t> buffer(bytes ? bytes : 1);
    const BOOL read = ::GetTokenInformation(token, TokenUser, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes);
    const DWORD error = ::GetLastError();
    ::CloseHandle(token);
    if (!read) return Failed("the account could not be read", HRESULT_FROM_WIN32(error));
    wchar_t* text = nullptr;
    if (!::ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid, &text) || !text)
        return Failed("the account could not be read", HRESULT_FROM_WIN32(::GetLastError()));
    std::wstring sid(text);
    ::LocalFree(text);
    return sid;
}

Result<std::optional<std::wstring>> ReadWindowsSignInTask() try {
    const platform::ComApartment apartment(COINIT_MULTITHREADED);
    if (!apartment.IsUsable()) return Failed("COM is unavailable", apartment.Status());
    auto sid = CurrentUserSid();
    if (!sid) return sid.error();
    auto folder = RootFolder();
    if (!folder) return folder.error();
    ComPtr<IRegisteredTask> task;
    const Bstr name(SignInTaskName(*sid));
    HRESULT hr = (*folder)->GetTask(name.value, task.GetAddressOf());
    if (NotFound(hr)) return std::optional<std::wstring>{};
    if (FAILED(hr)) return Failed("the sign-in task could not be read", hr);
    ComPtr<ITaskDefinition> definition;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    hr = task->get_Definition(definition.GetAddressOf());
    if (SUCCEEDED(hr)) hr = definition->get_Actions(actions.GetAddressOf());
    if (SUCCEEDED(hr)) hr = actions->get_Item(1, action.GetAddressOf());
    if (SUCCEEDED(hr)) hr = action.As(&exec);
    if (FAILED(hr)) return std::optional<std::wstring>{std::wstring{}};   // present, but not a program we recognise
    Bstr path;
    hr = exec->get_Path(&path.value);
    if (FAILED(hr) || !path.value) return std::optional<std::wstring>{std::wstring{}};
    return std::optional<std::wstring>{std::wstring(path.value, ::SysStringLen(path.value))};
} catch (...) {
    return MakeError(ErrorCode::Internal, "the sign-in task could not be read");
}

Result<void> WriteWindowsSignInTask(const std::wstring& launcher) try {
    const platform::ComApartment apartment(COINIT_MULTITHREADED);
    if (!apartment.IsUsable()) return Failed("COM is unavailable", apartment.Status());
    auto sid = CurrentUserSid();
    if (!sid) return sid.error();
    auto folder = RootFolder();
    if (!folder) return folder.error();
    const Bstr name(SignInTaskName(*sid));
    const Bstr xml(SignInTaskXml(launcher, *sid));
    Variant user, password, sddl;
    ComPtr<IRegisteredTask> registered;
    const HRESULT hr = (*folder)->RegisterTask(name.value, xml.value, TASK_CREATE_OR_UPDATE, user.value, password.value,
                                               TASK_LOGON_INTERACTIVE_TOKEN, sddl.value, registered.GetAddressOf());
    if (FAILED(hr)) return Failed("the sign-in task could not be registered", hr);
    return Ok();
} catch (...) {
    return MakeError(ErrorCode::Internal, "the sign-in task could not be registered");
}

Result<void> DeleteWindowsSignInTask() try {
    const platform::ComApartment apartment(COINIT_MULTITHREADED);
    if (!apartment.IsUsable()) return Failed("COM is unavailable", apartment.Status());
    auto sid = CurrentUserSid();
    if (!sid) return sid.error();
    auto folder = RootFolder();
    if (!folder) return folder.error();
    const Bstr name(SignInTaskName(*sid));
    const HRESULT hr = (*folder)->DeleteTask(name.value, 0);
    if (FAILED(hr) && !NotFound(hr)) return Failed("the sign-in task could not be removed", hr);
    return Ok();
} catch (...) {
    return MakeError(ErrorCode::Internal, "the sign-in task could not be removed");
}

}  // namespace detail
}  // namespace aether::settings
