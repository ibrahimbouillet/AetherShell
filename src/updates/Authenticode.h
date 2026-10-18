// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Optional Authenticode check of a downloaded installer with WinVerifyTrust
// (WINTRUST_ACTION_GENERIC_VERIFY_V2, no UI, revocation checked for the whole
// chain). This is defence in depth on top of the signed manifest, not the
// authenticity anchor (docs/decisions/0004-update-trust-model.md).
//
// The project has no code-signing certificate yet, so the default policy is
// NotRequired and official installers are currently NOT Authenticode-signed.
//
// Limitations (documented, not hidden):
//  * Any certificate that chains to a trusted root passes; the publisher identity is not pinned.
//  * Revocation checking may need the network; offline machines get Invalid
//    (CRYPT_E_REVOCATION_OFFLINE / CERT_E_REVOCATION_FAILURE), so Required fails closed offline.
//  * Catalog-signed files (most Windows system binaries) report NotSigned: only embedded
//    signatures are examined.
// WinVerifyTrust can block (file I/O, network): call it on worker threads only.
#pragma once

#include "core/Result.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace aether::updates {

enum class AuthenticodePolicy {
    NotRequired,        // default: no check is performed
    RequiredIfPresent,  // unsigned files pass; a present signature must be valid
    Required,           // the file must carry a valid signature
};

enum class AuthenticodeStatus {
    Valid,      // embedded signature verified, chain trusted, not revoked
    NotSigned,  // no embedded signature, or a file type WinVerifyTrust cannot sign (not a PE)
    Invalid,    // signature present but broken, untrusted, revoked, or revocation unknown
};

struct AuthenticodeResult {
    AuthenticodeStatus status = AuthenticodeStatus::NotSigned;
    std::int32_t trustCode = 0;  // WinVerifyTrust result (HRESULT-style), 0 when Valid
};

[[nodiscard]] std::string_view ToString(AuthenticodePolicy policy) noexcept;
[[nodiscard]] std::string_view ToString(AuthenticodeStatus status) noexcept;

// NotFound when the file does not exist.
[[nodiscard]] Result<AuthenticodeResult> CheckAuthenticode(const std::filesystem::path& file);

// Applies `policy` (NotRequired returns Ok without calling WinVerifyTrust). Failures use
// ErrorCode::Signature.
[[nodiscard]] Result<void> EnforceAuthenticodePolicy(const std::filesystem::path& file, AuthenticodePolicy policy);

}  // namespace aether::updates
