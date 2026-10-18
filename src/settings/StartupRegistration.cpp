// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/StartupRegistration.h"

#include "platform/windows/Win32Error.h"

#include <windows.h>

#include <cwctype>
#include <cstring>
#include <utility>

namespace aether::settings {
namespace {

constexpr wchar_t kValueName[] = L"AetherShell";
constexpr DWORD kMaxRegistryBytes = 64 * 1024;

struct RegistryKey {
    HKEY value = nullptr;
    ~RegistryKey() { if (value) ::RegCloseKey(value); }
};

class WindowsStartupRegistry final : public IStartupRegistry {
public:
    explicit WindowsStartupRegistry(StartupRegistryPaths paths) : paths_(std::move(paths)) {}
    Result<std::optional<std::wstring>> ReadRun() override {
        auto raw = Read(paths_.run, REG_SZ);
        if (!raw) return raw.error();
        if (raw->empty()) return std::optional<std::wstring>{};
        if (raw->size() % sizeof(wchar_t) != 0) return MakeError(ErrorCode::Validation, "invalid startup command encoding");
        std::wstring command(raw->size() / sizeof(wchar_t), L'\0');
        std::memcpy(command.data(), raw->data(), raw->size());
        if (command.empty() || command.back() != L'\0') return MakeError(ErrorCode::Validation, "unterminated startup command");
        command.pop_back();
        if (command.find(L'\0') != std::wstring::npos) return MakeError(ErrorCode::Validation, "invalid startup command");
        return std::optional<std::wstring>{std::move(command)};
    }
    Result<std::vector<std::uint8_t>> ReadApproval() override { return Read(paths_.approval, REG_BINARY); }
    Result<void> WriteRun(std::wstring command) override {
        RegistryKey key;
        const auto opened = ::RegCreateKeyExW(HKEY_CURRENT_USER, paths_.run.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
            nullptr, &key.value, nullptr);
        if (opened != ERROR_SUCCESS) return platform::ErrorFromWin32(opened, "cannot open per-user startup registration");
        const auto written = ::RegSetValueExW(key.value, kValueName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        if (written != ERROR_SUCCESS) return platform::ErrorFromWin32(written, "cannot write per-user startup registration");
        return Ok();
    }
    Result<void> DeleteRun() override {
        RegistryKey key;
        const auto opened = ::RegOpenKeyExW(HKEY_CURRENT_USER, paths_.run.c_str(), 0, KEY_SET_VALUE, &key.value);
        if (opened == ERROR_FILE_NOT_FOUND) return Ok();
        if (opened != ERROR_SUCCESS) return platform::ErrorFromWin32(opened, "cannot open per-user startup registration");
        const auto removed = ::RegDeleteValueW(key.value, kValueName);
        if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) {
            return platform::ErrorFromWin32(removed, "cannot remove per-user startup registration");
        }
        return Ok();
    }

private:
    Result<std::vector<std::uint8_t>> Read(const std::wstring& path, DWORD expectedType) {
        RegistryKey key;
        const auto opened = ::RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key.value);
        if (opened == ERROR_FILE_NOT_FOUND) return std::vector<std::uint8_t>{};
        if (opened != ERROR_SUCCESS) return platform::ErrorFromWin32(opened, "cannot read per-user startup registration");
        DWORD type = 0;
        DWORD bytes = 0;
        auto read = ::RegQueryValueExW(key.value, kValueName, nullptr, &type, nullptr, &bytes);
        if (read == ERROR_FILE_NOT_FOUND) return std::vector<std::uint8_t>{};
        if (read != ERROR_SUCCESS) return platform::ErrorFromWin32(read, "cannot query startup value");
        if (type != expectedType || bytes > kMaxRegistryBytes) return MakeError(ErrorCode::Unsupported, "unsupported startup value format");
        std::vector<std::uint8_t> value(bytes);
        read = ::RegQueryValueExW(key.value, kValueName, nullptr, &type, value.data(), &bytes);
        if (read != ERROR_SUCCESS) return platform::ErrorFromWin32(read, "cannot read startup value");
        if (type != expectedType) return MakeError(ErrorCode::Unsupported, "startup value changed format");
        value.resize(bytes);
        return value;
    }
    StartupRegistryPaths paths_;
};

Result<std::wstring> Command(const std::filesystem::path& launcher, bool requireFile) {
    const auto native = launcher.native();
    if (!launcher.is_absolute() || native.empty() || native.size() > 30'000 ||
        native.find_first_of(L"\"\r\n") != std::wstring::npos || native.find(L'\0') != std::wstring::npos) {
        return MakeError(ErrorCode::InvalidArgument, "startup launcher must be an absolute executable path");
    }
    auto extension = launcher.extension().wstring();
    for (auto& c : extension) c = static_cast<wchar_t>(std::towlower(c));
    if (extension != L".exe") return MakeError(ErrorCode::InvalidArgument, "startup launcher must have an exe extension");
    if (requireFile) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(launcher, error) || error) {
            return MakeError(ErrorCode::NotFound, "startup launcher file does not exist");
        }
    }
    return L"\"" + native + L"\" --autostart";
}

StartupState ApprovalState(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.front() == 0x02) return StartupState::Enabled;
    if (bytes.front() == 0x03) return StartupState::DisabledByUser;
    return StartupState::UnknownApproval;
}

}  // namespace

Result<std::shared_ptr<IStartupRegistry>> MakeWindowsStartupRegistry(StartupRegistryPaths paths) {
    try {
        if (paths.run.empty() || paths.approval.empty()) return MakeError(ErrorCode::InvalidArgument, "startup registry key paths must not be empty");
        return std::shared_ptr<IStartupRegistry>(std::make_shared<WindowsStartupRegistry>(std::move(paths)));
    } catch (...) { return MakeError(ErrorCode::Internal, "startup registry adapter could not be created"); }
}

StartupRegistration::StartupRegistration(std::shared_ptr<IStartupRegistry> registry) : registry_(std::move(registry)) {}

Result<StartupState> StartupRegistration::IsEnabled(const std::filesystem::path& expectedPath) {
    try {
        if (!registry_) return MakeError(ErrorCode::InvalidState, "startup registry adapter is missing");
        auto expected = Command(expectedPath, false);
        if (!expected) return expected.error();
        auto approval = registry_->ReadApproval();
        if (!approval) return approval.error();
        const auto state = ApprovalState(*approval);
        if (state != StartupState::Enabled) return state;
        auto value = registry_->ReadRun();
        if (!value) return value.error();
        if (!*value || (**value).empty()) return StartupState::Disabled;
        return ::_wcsicmp((**value).c_str(), expected->c_str()) == 0 ? StartupState::Enabled : StartupState::DifferentTarget;
    } catch (...) { return MakeError(ErrorCode::Internal, "startup registration could not be inspected"); }
}

Result<void> StartupRegistration::Enable(const std::filesystem::path& launcherPath) {
    try {
        if (!registry_) return MakeError(ErrorCode::InvalidState, "startup registry adapter is missing");
        auto command = Command(launcherPath, true);
        if (!command) return command.error();
        auto approval = registry_->ReadApproval();
        if (!approval) return approval.error();
        const auto state = ApprovalState(*approval);
        if (state == StartupState::DisabledByUser) return MakeError(ErrorCode::AccessDenied, "startup was disabled by the user in Task Manager");
        if (state == StartupState::UnknownApproval) return MakeError(ErrorCode::Unsupported, "startup approval format is unsupported");
        return registry_->WriteRun(std::move(*command));
    } catch (...) { return MakeError(ErrorCode::Internal, "startup registration could not be enabled"); }
}

Result<void> StartupRegistration::Disable() {
    try {
        if (!registry_) return MakeError(ErrorCode::InvalidState, "startup registry adapter is missing");
        return registry_->DeleteRun();
    } catch (...) { return MakeError(ErrorCode::Internal, "startup registration could not be disabled"); }
}

}  // namespace aether::settings
