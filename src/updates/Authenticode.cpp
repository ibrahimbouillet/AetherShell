// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/Authenticode.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "updates/UpdateTypes.h"

#include <windows.h>
#include <softpub.h>
#include <wintrust.h>

#include <format>

namespace aether::updates {
namespace {

// Closes the WinVerifyTrust state (WTD_STATEACTION_CLOSE) on every path.
class TrustStateGuard {
public:
    TrustStateGuard(GUID& action, WINTRUST_DATA& data) noexcept : action_(action), data_(data) {}
    TrustStateGuard(const TrustStateGuard&) = delete;
    TrustStateGuard& operator=(const TrustStateGuard&) = delete;
    ~TrustStateGuard() {
        data_.dwStateAction = WTD_STATEACTION_CLOSE;
        ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action_, &data_);
    }

private:
    GUID& action_;
    WINTRUST_DATA& data_;
};

}  // namespace

std::string_view ToString(AuthenticodePolicy policy) noexcept {
    switch (policy) {
        case AuthenticodePolicy::NotRequired: return "not-required";
        case AuthenticodePolicy::RequiredIfPresent: return "required-if-present";
        case AuthenticodePolicy::Required: return "required";
    }
    return "unknown";
}

std::string_view ToString(AuthenticodeStatus status) noexcept {
    switch (status) {
        case AuthenticodeStatus::Valid: return "valid";
        case AuthenticodeStatus::NotSigned: return "not-signed";
        case AuthenticodeStatus::Invalid: return "invalid";
    }
    return "unknown";
}

Result<AuthenticodeResult> CheckAuthenticode(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return MakeError(ErrorCode::NotFound, "file to verify does not exist");
    }

    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = file.c_str();

    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN | WTD_DISABLE_MD2_MD4;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    // INVALID_HANDLE_VALUE as the window: there is no interactive user, never show UI.
    const LONG status = ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    const DWORD lastError = ::GetLastError();
    TrustStateGuard guard(action, data);

    AuthenticodeResult result;
    result.trustCode = static_cast<std::int32_t>(status);
    switch (status) {
        case ERROR_SUCCESS:
            result.status = AuthenticodeStatus::Valid;
            break;
        case TRUST_E_NOSIGNATURE:
            // Per the WinVerifyTrust sample: these last errors mean "no signature"; any other means a
            // signature exists but could not be validated.
            result.status = (lastError == static_cast<DWORD>(TRUST_E_NOSIGNATURE) ||
                             lastError == static_cast<DWORD>(TRUST_E_SUBJECT_FORM_UNKNOWN) ||
                             lastError == static_cast<DWORD>(TRUST_E_PROVIDER_UNKNOWN))
                                ? AuthenticodeStatus::NotSigned
                                : AuthenticodeStatus::Invalid;
            break;
        case TRUST_E_SUBJECT_FORM_UNKNOWN:
        case TRUST_E_PROVIDER_UNKNOWN:
            result.status = AuthenticodeStatus::NotSigned;
            break;
        default:
            // TRUST_E_EXPLICIT_DISTRUST, TRUST_E_SUBJECT_NOT_TRUSTED, CERT_E_REVOKED, CERT_E_EXPIRED,
            // TRUST_E_BAD_DIGEST, CRYPT_E_REVOCATION_OFFLINE, ...
            result.status = AuthenticodeStatus::Invalid;
            break;
    }
    return result;
}

Result<void> EnforceAuthenticodePolicy(const std::filesystem::path& file, AuthenticodePolicy policy) {
    if (policy == AuthenticodePolicy::NotRequired) return Ok();
    auto checked = CheckAuthenticode(file);
    if (!checked) return checked.error();
    const auto status = checked->status;
    AETHER_LOG_INFO(kLogComponent, "Authenticode {} (policy {}, code 0x{:08X})", ToString(status), ToString(policy),
                    static_cast<std::uint32_t>(checked->trustCode));
    if (status == AuthenticodeStatus::Valid) return Ok();
    if (status == AuthenticodeStatus::NotSigned && policy == AuthenticodePolicy::RequiredIfPresent) return Ok();
    return MakeError(ErrorCode::Signature,
                     status == AuthenticodeStatus::NotSigned
                         ? std::string("the installer has no Authenticode signature, which the policy requires")
                         : std::format("the installer's Authenticode signature is not valid (0x{:08X})",
                                       static_cast<std::uint32_t>(checked->trustCode)),
                     checked->trustCode);
}

}  // namespace aether::updates
