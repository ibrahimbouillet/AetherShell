// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/Win32CleanSessionBackend.h"

#include <Windows.h>
#include <appmodel.h>
#include <dwmapi.h>
#include <propsys.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aether::showcase {
namespace {

using Microsoft::WRL::ComPtr;

class ScopedHandle final {
public:
    explicit ScopedHandle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~ScopedHandle() { if (value_ && value_ != INVALID_HANDLE_VALUE) (void)::CloseHandle(value_); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            if (value_ && value_ != INVALID_HANDLE_VALUE) (void)::CloseHandle(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE Get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_ = nullptr;
};

struct FileTimeValue final {
    std::uint64_t value = 0;
};

FileTimeValue CombineFileTime(FILETIME value) noexcept {
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return {combined.QuadPart};
}

Error Win32Error(ErrorCode code, const char* message) noexcept {
    return MakeError(code, message, static_cast<std::int64_t>(::GetLastError()));
}

bool ReadProcessIdentity(HANDLE process, std::uint32_t processId, ProcessIdentity& identity) noexcept {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!::GetProcessTimes(process, &creation, &exit, &kernel, &user)) return false;
    identity = {processId, CombineFileTime(creation).value};
    return identity.creationTime != 0;
}

bool SameIdentity(HANDLE process, ProcessIdentity expected) noexcept {
    ProcessIdentity actual{};
    return ReadProcessIdentity(process, expected.processId, actual) && actual == expected;
}

Result<std::vector<ProcessIdentity>> CaptureControllerAncestry(DWORD currentProcessId) {
    ScopedHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot)
        return Win32Error(ErrorCode::Unavailable, "showcase could not protect its launching controller");
    std::vector<std::pair<DWORD, DWORD>> parents;
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!::Process32FirstW(snapshot.Get(), &entry))
        return Win32Error(ErrorCode::Unavailable, "showcase could not inspect its controller ancestry");
    do {
        parents.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
    } while (::Process32NextW(snapshot.Get(), &entry));
    if (::GetLastError() != ERROR_NO_MORE_FILES)
        return Win32Error(ErrorCode::Unavailable, "showcase could not finish its controller safety snapshot");

    std::vector<ProcessIdentity> protectedProcesses;
    DWORD processId = currentProcessId;
    std::uint64_t childCreationTime = 0;
    while (processId != 0 && std::none_of(protectedProcesses.begin(), protectedProcesses.end(),
               [processId](const ProcessIdentity& process) { return process.processId == processId; })) {
        ProcessIdentity identity{processId, 0};
        ScopedHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
        if (process) (void)ReadProcessIdentity(process.Get(), processId, identity);
        // Parent PIDs can be reused after their process exits. A newer process is not an ancestor.
        if (childCreationTime != 0 && identity.creationTime != 0 &&
            identity.creationTime > childCreationTime) break;
        protectedProcesses.push_back(identity);
        if (identity.creationTime != 0) childCreationTime = identity.creationTime;
        const auto parent = std::find_if(parents.begin(), parents.end(),
            [processId](const auto& candidate) { return candidate.first == processId; });
        if (parent == parents.end()) break;
        processId = parent->second;
    }
    if (protectedProcesses.empty() || protectedProcesses.front().creationTime == 0)
        return MakeError(ErrorCode::Unavailable, "showcase could not identify its own capture process");
    return protectedProcesses;
}

bool QueryImagePath(HANDLE process, std::wstring& path) {
    std::vector<wchar_t> buffer(32'768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (!::QueryFullProcessImageNameW(process, 0, buffer.data(), &length) || length == 0) return false;
    path.assign(buffer.data(), length);
    return true;
}

bool EndsWithIgnoreCase(std::wstring_view value, std::wstring_view suffix) noexcept {
    if (value.size() < suffix.size()) return false;
    return ::CompareStringOrdinal(value.data() + value.size() - suffix.size(), static_cast<int>(suffix.size()),
                                  suffix.data(), static_cast<int>(suffix.size()), TRUE) == CSTR_EQUAL;
}

bool IsProtectedImage(std::wstring_view imagePath) noexcept {
    constexpr std::wstring_view protectedNames[] = {
        L"\\explorer.exe", L"\\dwm.exe", L"\\winlogon.exe", L"\\csrss.exe", L"\\smss.exe",
        L"\\services.exe", L"\\lsass.exe", L"\\wininit.exe", L"\\sihost.exe",
        L"\\StartMenuExperienceHost.exe", L"\\ShellExperienceHost.exe", L"\\SearchHost.exe",
        L"\\TextInputHost.exe", L"\\SecHealthUI.exe", L"\\SecurityHealthSystray.exe",
        L"\\MsMpEng.exe", L"\\MpDefenderCoreService.exe", L"\\NisSrv.exe",
        L"\\SecurityHealthService.exe", L"\\smartscreen.exe",
        L"\\AetherShellApp.exe", L"\\AetherShowcaseRunner.exe"};
    for (const auto name : protectedNames) if (EndsWithIgnoreCase(imagePath, name)) return true;
    return false;
}

bool IsValidApplicationUserModelId(std::wstring_view applicationId) noexcept {
    if (applicationId.empty() || applicationId.size() > 512) return false;
    for (const wchar_t character : applicationId) {
        if (!(std::iswalnum(character) || character == L'.' || character == L'_' ||
              character == L'-' || character == L'!')) return false;
    }
    return applicationId.find(L'!') != std::wstring_view::npos;
}

std::optional<std::wstring> WindowApplicationUserModelId(HWND window) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(::SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&properties))) || !properties) return std::nullopt;
    PROPVARIANT value{};
    ::PropVariantInit(&value);
    constexpr PROPERTYKEY appUserModelIdKey{
        {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
    const HRESULT status = properties->GetValue(appUserModelIdKey, &value);
    std::optional<std::wstring> result;
    if (SUCCEEDED(status) && value.vt == VT_LPWSTR && value.pwszVal) {
        std::wstring candidate(value.pwszVal);
        if (IsValidApplicationUserModelId(candidate)) result = std::move(candidate);
    }
    (void)::PropVariantClear(&value);
    return result;
}

std::optional<std::wstring> ProcessApplicationUserModelId(HANDLE process) {
    UINT32 required = 0;
    const LONG firstStatus = ::GetApplicationUserModelId(process, &required, nullptr);
    if (firstStatus != ERROR_INSUFFICIENT_BUFFER || required == 0 || required > 513) return std::nullopt;
    std::vector<wchar_t> buffer(required, L'\0');
    if (::GetApplicationUserModelId(process, &required, buffer.data()) != ERROR_SUCCESS || required == 0)
        return std::nullopt;
    std::wstring candidate(buffer.data());
    if (!IsValidApplicationUserModelId(candidate)) return std::nullopt;
    return candidate;
}

struct WindowAppIdList final {
    DWORD processId = 0;
    std::vector<std::wstring> ids;
    bool allocationFailure = false;
};

BOOL CALLBACK CollectWindowApplicationIds(HWND window, LPARAM parameter) noexcept {
    auto& result = *reinterpret_cast<WindowAppIdList*>(parameter);
    if (!::IsWindowVisible(window) || ::GetWindow(window, GW_OWNER) != nullptr ||
        ::GetAncestor(window, GA_ROOT) != window) return TRUE;
    DWORD processId = 0;
    (void)::GetWindowThreadProcessId(window, &processId);
    if (processId != result.processId) return TRUE;
    try {
        if (auto id = WindowApplicationUserModelId(window)) result.ids.push_back(std::move(*id));
    } catch (...) {
        result.allocationFailure = true;
        ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return TRUE;
}

std::wstring QuoteArgument(std::wstring_view value) {
    std::wstring quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back(L'"');
    for (const wchar_t character : value) {
        if (character == L'"') quoted.push_back(L'\\');
        quoted.push_back(character);
    }
    quoted.push_back(L'"');
    return quoted;
}

bool IsCritical(HANDLE process) noexcept {
    BOOL critical = TRUE;
    // A failed query is treated as protected. The close path must fail safe.
    if (!::IsProcessCritical(process, &critical)) return true;
    return critical != FALSE;
}

bool GetTokenUserSid(HANDLE process, std::vector<std::byte>& sid) {
    ScopedHandle token;
    HANDLE rawToken = nullptr;
    if (!::OpenProcessToken(process, TOKEN_QUERY, &rawToken)) return false;
    token = ScopedHandle(rawToken);
    DWORD required = 0;
    (void)::GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &required);
    if (required == 0) return false;
    std::vector<std::byte> tokenData(required);
    if (!::GetTokenInformation(token.Get(), TokenUser, tokenData.data(), required, &required)) return false;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(tokenData.data());
    const DWORD sidBytes = ::GetLengthSid(user->User.Sid);
    if (sidBytes == 0) return false;
    sid.resize(sidBytes);
    return ::CopySid(sidBytes, sid.data(), user->User.Sid) != FALSE;
}

bool IsSameUser(HANDLE process, std::span<const std::byte> currentSid) {
    std::vector<std::byte> candidate;
    if (!GetTokenUserSid(process, candidate) || candidate.empty() || currentSid.empty()) return false;
    return ::EqualSid(const_cast<std::byte*>(candidate.data()), const_cast<std::byte*>(currentSid.data())) != FALSE;
}

struct WindowProcessList final {
    std::vector<DWORD> processIds;
    bool allocationFailure = false;
};

BOOL CALLBACK CollectVisibleWindow(HWND window, LPARAM parameter) noexcept {
    auto& result = *reinterpret_cast<WindowProcessList*>(parameter);
    if (!::IsWindowVisible(window) || ::GetWindow(window, GW_OWNER) != nullptr ||
        ::GetAncestor(window, GA_ROOT) != window) return TRUE;
    const LONG_PTR extended = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((extended & WS_EX_TOOLWINDOW) != 0) return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0)
        return TRUE;
    DWORD processId = 0;
    (void)::GetWindowThreadProcessId(window, &processId);
    if (processId != 0 && std::find(result.processIds.begin(), result.processIds.end(), processId) == result.processIds.end()) {
        try {
            result.processIds.push_back(processId);
        } catch (...) {
            result.allocationFailure = true;
            ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
    }
    return TRUE;
}

struct CloseWindowContext final {
    DWORD processId = 0;
    bool posted = false;
};

BOOL CALLBACK PostCloseToProcess(HWND window, LPARAM parameter) noexcept {
    auto& context = *reinterpret_cast<CloseWindowContext*>(parameter);
    DWORD processId = 0;
    (void)::GetWindowThreadProcessId(window, &processId);
    if (processId == context.processId && ::IsWindowVisible(window) && ::PostMessageW(window, WM_CLOSE, 0, 0))
        context.posted = true;
    return TRUE;
}

std::wstring QuoteExecutable(std::wstring_view executable) {
    std::wstring quoted;
    quoted.reserve(executable.size() + 2);
    quoted.push_back(L'"');
    quoted.append(executable);
    quoted.push_back(L'"');
    return quoted;
}

}  // namespace

struct Win32CleanSessionBackend::Impl final {
    std::vector<std::byte> currentUserSid;
    std::vector<ProcessIdentity> controllerProcesses;
    std::uint32_t currentSession = 0;
    DWORD currentProcess = 0;
    DWORD shellProcess = 0;

    [[nodiscard]] bool Protects(ProcessIdentity process) const noexcept {
        return process.processId == currentProcess || process.processId == shellProcess ||
            std::any_of(controllerProcesses.begin(), controllerProcesses.end(),
                [process](const ProcessIdentity& controller) {
                    return controller.processId == process.processId &&
                        (controller.creationTime == 0 || controller.creationTime == process.creationTime);
                });
    }
};

Win32CleanSessionBackend::Win32CleanSessionBackend() noexcept = default;
Win32CleanSessionBackend::Win32CleanSessionBackend(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Win32CleanSessionBackend::~Win32CleanSessionBackend() = default;
Win32CleanSessionBackend::Win32CleanSessionBackend(Win32CleanSessionBackend&& other) noexcept = default;
Win32CleanSessionBackend& Win32CleanSessionBackend::operator=(Win32CleanSessionBackend&& other) noexcept = default;

Result<Win32CleanSessionBackend> Win32CleanSessionBackend::Create(
    std::span<const std::uint32_t> keepProcessIds) noexcept {
    try {
        auto impl = std::make_unique<Impl>();
        if (std::any_of(keepProcessIds.begin(), keepProcessIds.end(),
                        [](std::uint32_t processId) { return processId == 0; }))
            return MakeError(ErrorCode::InvalidArgument, "showcase process exclusions are invalid");
        impl->currentProcess = ::GetCurrentProcessId();
        auto ancestry = CaptureControllerAncestry(impl->currentProcess);
        if (!ancestry) return ancestry.error();
        impl->controllerProcesses = std::move(*ancestry);
        for (const auto processId : keepProcessIds) {
            if (std::any_of(impl->controllerProcesses.begin(), impl->controllerProcesses.end(),
                    [processId](const ProcessIdentity& controller) { return controller.processId == processId; })) continue;
            ProcessIdentity identity{processId, 0};
            ScopedHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
            if (process) (void)ReadProcessIdentity(process.Get(), processId, identity);
            impl->controllerProcesses.push_back(identity);
        }
        DWORD session = 0;
        if (!::ProcessIdToSessionId(impl->currentProcess, &session) || session == 0)
            return Win32Error(ErrorCode::Unavailable, "showcase user session could not be identified");
        impl->currentSession = session;
        if (!GetTokenUserSid(::GetCurrentProcess(), impl->currentUserSid))
            return Win32Error(ErrorCode::AccessDenied, "showcase could not verify the current user identity");
        if (const HWND shell = ::GetShellWindow()) (void)::GetWindowThreadProcessId(shell, &impl->shellProcess);
        return Win32CleanSessionBackend(std::move(impl));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase safety snapshot could not be allocated");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase safety initialization failed");
    }
}

Result<std::uint32_t> Win32CleanSessionBackend::CurrentSessionId() noexcept {
    DWORD session = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session) || session == 0)
        return Win32Error(ErrorCode::Unavailable, "showcase user session could not be identified");
    return static_cast<std::uint32_t>(session);
}

std::span<const ProcessIdentity> Win32CleanSessionBackend::ProtectedControllerProcesses() const noexcept {
    return impl_ ? std::span<const ProcessIdentity>(impl_->controllerProcesses) : std::span<const ProcessIdentity>{};
}

Result<std::vector<VisibleApplication>> Win32CleanSessionBackend::EnumerateVisibleApplications(
    std::uint32_t sessionId) {
    if (!impl_ || sessionId != impl_->currentSession)
        return MakeError(ErrorCode::InvalidArgument, "showcase session does not match the active user session");
    try {
        WindowProcessList windows;
        if (!::EnumWindows(&CollectVisibleWindow, reinterpret_cast<LPARAM>(&windows)) || windows.allocationFailure)
            return Win32Error(ErrorCode::Platform, "showcase could not enumerate visible desktop windows");
        std::vector<VisibleApplication> applications;
        applications.reserve(windows.processIds.size());
        for (const DWORD processId : windows.processIds) {
            ScopedHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, processId));
            if (!process)
                return Win32Error(ErrorCode::Unavailable, "showcase could not safely classify a visible application");
            ProcessIdentity identity{};
            if (!ReadProcessIdentity(process.Get(), processId, identity))
                return Win32Error(ErrorCode::Unavailable, "showcase could not identify a visible application");
            DWORD ownerSession = 0;
            if (!::ProcessIdToSessionId(processId, &ownerSession))
                return Win32Error(ErrorCode::Unavailable, "showcase could not classify a visible application session");
            std::wstring imagePath;
            if (!QueryImagePath(process.Get(), imagePath))
                return Win32Error(ErrorCode::Unavailable, "showcase could not classify a visible application image");
            const bool userMatches = IsSameUser(process.Get(), impl_->currentUserSid);
            const bool protectedProcess = impl_->Protects(identity) || IsCritical(process.Get()) ||
                IsProtectedImage(imagePath);
            applications.push_back({identity, ownerSession, userMatches, true, protectedProcess,
                                    processId == impl_->currentProcess ||
                                        EndsWithIgnoreCase(imagePath, L"\\AetherShowcaseRunner.exe")});
        }
        return applications;
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase window snapshot could not be allocated");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase window snapshot failed");
    }
}

Result<RelaunchDescriptor> Win32CleanSessionBackend::CaptureRelaunchDescriptor(ProcessIdentity expected) {
    if (!impl_ || expected.processId == 0 || expected.creationTime == 0)
        return MakeError(ErrorCode::InvalidArgument, "showcase application identity is invalid");
    if (impl_->Protects(expected))
        return MakeError(ErrorCode::AccessDenied, "showcase refused its protected controller or shell process");
    ScopedHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, expected.processId));
    if (!process || !SameIdentity(process.Get(), expected))
        return Win32Error(ErrorCode::Unavailable, "showcase application changed during the safety snapshot");
    DWORD session = 0;
    if (!::ProcessIdToSessionId(expected.processId, &session) || session != impl_->currentSession ||
        !IsSameUser(process.Get(), impl_->currentUserSid) || IsCritical(process.Get()))
        return MakeError(ErrorCode::AccessDenied, "showcase refused an application outside the active user session");
    std::wstring path;
    if (!QueryImagePath(process.Get(), path) || IsProtectedImage(path))
        return MakeError(ErrorCode::Unsupported, "showcase could not safely prepare this application for restoration");
    WindowAppIdList windowIds{expected.processId};
    (void)::EnumWindows(&CollectWindowApplicationIds, reinterpret_cast<LPARAM>(&windowIds));
    if (windowIds.allocationFailure)
        return MakeError(ErrorCode::Unavailable, "showcase could not snapshot Windows app activation identities");
    if (windowIds.ids.empty()) {
        if (auto processId = ProcessApplicationUserModelId(process.Get()))
            windowIds.ids.push_back(std::move(*processId));
    }
    if (!windowIds.ids.empty()) {
        std::vector<std::wstring> uniqueIds;
        uniqueIds.reserve(windowIds.ids.size());
        for (auto& id : windowIds.ids) {
            if (std::find(uniqueIds.begin(), uniqueIds.end(), id) == uniqueIds.end())
                uniqueIds.push_back(std::move(id));
        }
        return RelaunchDescriptor(L"explorer.exe", L"explorer.exe", std::move(uniqueIds));
    }
    if (EndsWithIgnoreCase(path, L"\\ApplicationFrameHost.exe"))
        return MakeError(ErrorCode::Unsupported,
                         "showcase cannot safely restart this Windows app without an activation identity");
    // Only the executable path is retained in memory. Windows does not expose a documented,
    // privacy-safe way to restore arbitrary unsaved application state or arguments.
    return RelaunchDescriptor(path, QuoteExecutable(path));
}

Result<void> Win32CleanSessionBackend::RequestGracefulClose(ProcessIdentity expected) {
    if (!impl_ || expected.processId == 0 || expected.creationTime == 0)
        return MakeError(ErrorCode::InvalidArgument, "showcase application identity is invalid");
    if (impl_->Protects(expected))
        return MakeError(ErrorCode::AccessDenied, "showcase refused to close its protected controller or shell process");
    ScopedHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, expected.processId));
    if (!process || !SameIdentity(process.Get(), expected)) return Ok();
    std::wstring path;
    if (!QueryImagePath(process.Get(), path) || IsProtectedImage(path) || IsCritical(process.Get()))
        return MakeError(ErrorCode::AccessDenied, "showcase refused to close a protected process");
    CloseWindowContext context{expected.processId, false};
    (void)::EnumWindows(&PostCloseToProcess, reinterpret_cast<LPARAM>(&context));
    if (!context.posted) return MakeError(ErrorCode::Unavailable, "showcase close request could not reach the application");
    return Ok();
}

Result<bool> Win32CleanSessionBackend::WaitForExit(ProcessIdentity expected, std::uint32_t timeoutMilliseconds) {
    if (expected.processId == 0 || expected.creationTime == 0)
        return MakeError(ErrorCode::InvalidArgument, "showcase application identity is invalid");
    ScopedHandle process(::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, expected.processId));
    if (!process) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_INVALID_PARAMETER) return true;
        return MakeError(ErrorCode::AccessDenied, "showcase could not verify application shutdown", error);
    }
    if (!SameIdentity(process.Get(), expected)) return true;
    const DWORD wait = ::WaitForSingleObject(process.Get(), timeoutMilliseconds);
    if (wait == WAIT_OBJECT_0) return true;
    if (wait == WAIT_TIMEOUT) return false;
    return Win32Error(ErrorCode::Platform, "showcase could not wait for application shutdown");
}

Result<void> Win32CleanSessionBackend::TerminateApplication(ProcessIdentity expected) {
    if (!impl_ || expected.processId == 0 || expected.creationTime == 0)
        return MakeError(ErrorCode::InvalidArgument, "showcase application identity is invalid");
    if (impl_->Protects(expected))
        return MakeError(ErrorCode::AccessDenied, "showcase refused to terminate its protected controller or shell process");
    ScopedHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                                       FALSE, expected.processId));
    if (!process || !SameIdentity(process.Get(), expected)) return Ok();
    DWORD session = 0;
    std::wstring path;
    if (!::ProcessIdToSessionId(expected.processId, &session) || session != impl_->currentSession ||
        !IsSameUser(process.Get(), impl_->currentUserSid) || !QueryImagePath(process.Get(), path) ||
        IsCritical(process.Get()) || IsProtectedImage(path))
        return MakeError(ErrorCode::AccessDenied, "showcase refused to terminate a protected process");
    if (!::TerminateProcess(process.Get(), ERROR_CANCELLED))
        return Win32Error(ErrorCode::AccessDenied, "showcase could not stop a remaining user application");
    return Ok();
}

Result<bool> Win32CleanSessionBackend::IsRunning(ProcessIdentity expected) {
    if (expected.processId == 0 || expected.creationTime == 0) return false;
    ScopedHandle process(::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, expected.processId));
    if (!process) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_INVALID_PARAMETER) return false;
        return MakeError(ErrorCode::AccessDenied, "showcase could not check an application process");
    }
    if (!SameIdentity(process.Get(), expected)) return false;
    const DWORD wait = ::WaitForSingleObject(process.Get(), 0);
    if (wait == WAIT_TIMEOUT) return true;
    if (wait == WAIT_OBJECT_0) return false;
    return Win32Error(ErrorCode::Platform, "showcase could not check application process state");
}

Result<void> Win32CleanSessionBackend::Relaunch(ProcessIdentity expected, const RelaunchDescriptor& descriptor) {
    (void)expected;
    const auto& executable = descriptor.ExecutablePath();
    if (executable.empty() || descriptor.CommandLine().empty())
        return MakeError(ErrorCode::InvalidArgument, "showcase restoration descriptor is empty");
    if (!descriptor.PackagedApplicationIds().empty()) {
        for (const auto& applicationId : descriptor.PackagedApplicationIds()) {
            if (!IsValidApplicationUserModelId(applicationId))
                return MakeError(ErrorCode::Validation, "showcase refused an invalid Windows app activation identity");
            const std::wstring target = L"shell:AppsFolder\\" + applicationId;
            const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", L"explorer.exe",
                QuoteArgument(target).c_str(), nullptr, SW_SHOWNORMAL));
            if (result <= 32)
                return Win32Error(ErrorCode::Unavailable, "showcase could not relaunch a Windows app");
        }
        return Ok();
    }
    try {
        std::wstring commandLine = descriptor.CommandLine();
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!::CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                              CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process))
            return Win32Error(ErrorCode::Unavailable, "showcase could not relaunch a closed application");
        (void)::CloseHandle(process.hThread);
        (void)::CloseHandle(process.hProcess);
        return Ok();
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase could not allocate an application restart request");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase application restart failed");
    }
}

}  // namespace aether::showcase
