// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Authenticity half of the update trust model (docs/decisions/0004-update-trust-model.md):
// an ECDSA P-256 / SHA-256 signature over the EXACT bytes of the release
// manifest, verified with Windows CNG against public keys compiled into the
// application (TrustedKeys.h).
//
// The ".sig" release asset holds base64 (strict, RFC 4648) of either
//   * the 64-byte IEEE P1363 form r || s (what scripts/release/sign_manifest.py writes), or
//   * a DER ECDSA-Sig-Value (SEQUENCE { INTEGER r, INTEGER s }), accepted through a strict,
//     minimal-encoding parser (no long-form lengths, no padding bytes, no trailing data).
// optionally followed by one "\n" or "\r\n". Anything else is rejected.
//
// Every failure (malformed file, wrong key, tampered manifest) is ErrorCode::Signature, so callers
// can treat "not authenticated" uniformly.
#pragma once

#include "core/Result.h"
#include "updates/TrustedKeys.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::updates {

inline constexpr std::size_t kP256CoordinateSize = 32;
inline constexpr std::size_t kP256PublicKeySize = 64;  // X || Y
inline constexpr std::size_t kP256SignatureSize = 64;  // r || s
// Upper bound for the .sig asset (base64 of at most 72 DER bytes plus a newline needs ~100).
inline constexpr std::size_t kMaxSignatureFileBytes = 512;

using EcdsaP256Signature = std::array<std::uint8_t, kP256SignatureSize>;

// Strict DER ECDSA-Sig-Value to r || s. Both integers must be minimally encoded, positive and in
// [1, n-1] for the P-256 group order n.
[[nodiscard]] Result<EcdsaP256Signature> ParseDerEcdsaSignature(std::span<const std::uint8_t> der);
// 64 bytes are read as P1363 r || s, anything else as DER. r and s must be in [1, n-1].
[[nodiscard]] Result<EcdsaP256Signature> ParseEcdsaSignature(std::span<const std::uint8_t> bytes);
// Contents of the .sig release asset (see above).
[[nodiscard]] Result<EcdsaP256Signature> ParseSignatureFile(std::string_view text);

class EcdsaP256Verifier {
public:
    // Trusts exactly `keys` (TrustedKey::publicKeyHex is X || Y, 128 hex characters). Every key is
    // imported once with BCryptImportKeyPair, which validates that the point lies on P-256; keys that
    // fail are logged and left out (fail closed: an unusable key can only reduce what is trusted).
    // Tests pass their own test keys here.
    explicit EcdsaP256Verifier(std::span<const trust::TrustedKey> keys);

    // The production verifier: trust::kReleaseSigningKeys and nothing else.
    [[nodiscard]] static EcdsaP256Verifier ForReleaseManifests();

    // Number of keys that imported successfully. Zero means nothing can verify.
    [[nodiscard]] std::size_t TrustedKeyCount() const noexcept { return keys_.size(); }

    // Verifies `signature` over SHA-256(message). Returns the id of the trusted key that verified it
    // (several keys exist during a key rotation). ErrorCode::Signature when no trusted key matches.
    // Thread-safe: no CNG handle is shared between calls.
    [[nodiscard]] Result<std::string> Verify(std::span<const std::uint8_t> message,
                                             const EcdsaP256Signature& signature) const;
    [[nodiscard]] Result<std::string> Verify(std::string_view message, const EcdsaP256Signature& signature) const;

    // ParseSignatureFile + Verify.
    [[nodiscard]] Result<std::string> VerifySignatureFile(std::string_view message,
                                                          std::string_view signatureFileText) const;

private:
    struct Key {
        std::string id;
        std::array<std::uint8_t, kP256PublicKeySize> point{};
    };
    std::vector<Key> keys_;
};

}  // namespace aether::updates
