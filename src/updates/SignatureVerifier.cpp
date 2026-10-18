// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/SignatureVerifier.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "updates/CngSupport.h"
#include "updates/Encoding.h"
#include "updates/Sha256.h"
#include "updates/UpdateTypes.h"

#include <algorithm>
#include <cstring>

namespace aether::updates {
namespace {

// Order n of the P-256 base point (FIPS 186-4, D.1.2.3), big-endian.
constexpr std::array<std::uint8_t, kP256CoordinateSize> kP256Order{
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51};

// Smallest and largest possible DER ECDSA-Sig-Value for P-256: 30 06 02 01 xx 02 01 xx, and two
// 33-byte integers (a leading 0x00 plus 32 value bytes).
constexpr std::size_t kMinDerSignatureSize = 8;
constexpr std::size_t kMaxDerSignatureSize = 72;

Error SignatureError(std::string_view reason) {
    return MakeError(ErrorCode::Signature, "invalid manifest signature: " + std::string(reason));
}

// True when the 32-byte big-endian integer is in [1, n-1].
bool IsValidScalar(std::span<const std::uint8_t> value) noexcept {
    const bool zero = std::all_of(value.begin(), value.end(), [](std::uint8_t b) { return b == 0; });
    return !zero && std::lexicographical_compare(value.begin(), value.end(), kP256Order.begin(), kP256Order.end());
}

Result<EcdsaP256Signature> CheckRange(const EcdsaP256Signature& signature) {
    const std::span<const std::uint8_t> all(signature);
    if (!IsValidScalar(all.first(kP256CoordinateSize)) || !IsValidScalar(all.last(kP256CoordinateSize))) {
        return SignatureError("r or s is outside [1, n-1]");
    }
    return signature;
}

Result<cng::UniqueKey> ImportPublicKey(BCRYPT_ALG_HANDLE algorithm,
                                       const std::array<std::uint8_t, kP256PublicKeySize>& point) {
    // BCRYPT_ECCPUBLIC_BLOB: BCRYPT_ECCKEY_BLOB header followed by X and Y (big-endian, cbKey bytes each).
    std::array<std::uint8_t, sizeof(BCRYPT_ECCKEY_BLOB) + kP256PublicKeySize> blob{};
    const BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, static_cast<ULONG>(kP256CoordinateSize)};
    std::memcpy(blob.data(), &header, sizeof(header));
    std::memcpy(blob.data() + sizeof(header), point.data(), point.size());
    cng::UniqueKey key;
    // Without BCRYPT_NO_KEY_VALIDATION, CNG checks that the point is on the curve.
    const NTSTATUS status = ::BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, key.Put(), blob.data(),
                                                  static_cast<ULONG>(blob.size()), 0);
    if (!BCRYPT_SUCCESS(status)) return cng::StatusError(status, "BCryptImportKeyPair");
    return key;
}

}  // namespace

Result<EcdsaP256Signature> ParseDerEcdsaSignature(std::span<const std::uint8_t> der) {
    if (der.size() < kMinDerSignatureSize || der.size() > kMaxDerSignatureSize) {
        return SignatureError("DER signature has an impossible length for P-256");
    }
    if (der[0] != 0x30) return SignatureError("DER signature is not a SEQUENCE");
    // Content is at most 70 bytes, so only the short length form is valid DER here.
    if ((der[1] & 0x80) != 0) return SignatureError("DER signature uses a long-form length");
    if (der[1] != der.size() - 2) return SignatureError("DER SEQUENCE length does not match the data");

    EcdsaP256Signature signature{};
    std::size_t position = 2;
    for (std::size_t index = 0; index < 2; ++index) {
        if (position + 2 > der.size() || der[position] != 0x02) {
            return SignatureError("DER signature component is not an INTEGER");
        }
        const std::size_t length = der[position + 1];
        if ((length & 0x80) != 0) return SignatureError("DER INTEGER uses a long-form length");
        if (length == 0 || length > kP256CoordinateSize + 1) return SignatureError("DER INTEGER has a bad length");
        if (position + 2 + length > der.size()) return SignatureError("DER INTEGER exceeds the SEQUENCE");
        auto body = der.subspan(position + 2, length);
        if ((body[0] & 0x80) != 0) return SignatureError("DER INTEGER is negative");
        if (body.size() > 1 && body[0] == 0x00 && (body[1] & 0x80) == 0) {
            return SignatureError("DER INTEGER is not minimally encoded");
        }
        if (body[0] == 0x00 && body.size() > 1) body = body.subspan(1);  // sign padding of a high-bit value
        if (body.size() > kP256CoordinateSize) return SignatureError("DER INTEGER is larger than 256 bits");
        // Right-align into the 32-byte field.
        std::copy(body.begin(), body.end(),
                  signature.begin() + static_cast<std::ptrdiff_t>(index * kP256CoordinateSize + kP256CoordinateSize -
                                                                  body.size()));
        position += 2 + length;
    }
    if (position != der.size()) return SignatureError("trailing bytes after the DER signature");
    return CheckRange(signature);
}

Result<EcdsaP256Signature> ParseEcdsaSignature(std::span<const std::uint8_t> bytes) {
    if (bytes.size() == kP256SignatureSize) {
        // A DER encoding of exactly 64 bytes needs both r and s below 2^232 (probability ~2^-48), so
        // reading 64 bytes as P1363 is unambiguous in practice; sign_manifest.py always writes P1363.
        EcdsaP256Signature signature{};
        std::copy(bytes.begin(), bytes.end(), signature.begin());
        return CheckRange(signature);
    }
    if (!bytes.empty() && bytes[0] == 0x30) return ParseDerEcdsaSignature(bytes);
    return SignatureError("signature is neither 64-byte r||s nor DER");
}

Result<EcdsaP256Signature> ParseSignatureFile(std::string_view text) {
    if (text.empty()) return SignatureError("signature file is empty");
    if (text.size() > kMaxSignatureFileBytes) return SignatureError("signature file is too large");
    // Exactly one optional line terminator; no other whitespace anywhere.
    if (text.ends_with("\r\n")) {
        text.remove_suffix(2);
    } else if (text.ends_with('\n')) {
        text.remove_suffix(1);
    }
    auto bytes = encoding::DecodeBase64(text);
    if (!bytes) return SignatureError("signature file is not strict base64 (" + bytes.error().message + ")");
    return ParseEcdsaSignature(*bytes);
}

EcdsaP256Verifier::EcdsaP256Verifier(std::span<const trust::TrustedKey> keys) {
    auto algorithm = cng::OpenAlgorithm(BCRYPT_ECDSA_P256_ALGORITHM);
    if (!algorithm) {
        AETHER_LOG_ERROR(kLogComponent, "ECDSA P-256 provider unavailable: {}", algorithm.error().Describe());
        return;
    }
    for (const auto& trusted : keys) {
        auto bytes = encoding::FromHex(trusted.publicKeyHex);
        if (!bytes || bytes->size() != kP256PublicKeySize) {
            AETHER_LOG_ERROR(kLogComponent, "trusted key '{}' is not 64 bytes of hex (X||Y); ignored",
                             text::SanitizeForLog(trusted.id));
            continue;
        }
        Key key{std::string(trusted.id), {}};
        std::copy(bytes->begin(), bytes->end(), key.point.begin());
        if (auto imported = ImportPublicKey(algorithm->Get(), key.point); !imported) {
            AETHER_LOG_ERROR(kLogComponent, "trusted key '{}' is not a valid P-256 point; ignored: {}",
                             text::SanitizeForLog(trusted.id), imported.error().Describe());
            continue;
        }
        keys_.push_back(std::move(key));
    }
}

EcdsaP256Verifier EcdsaP256Verifier::ForReleaseManifests() { return EcdsaP256Verifier(trust::kReleaseSigningKeys); }

Result<std::string> EcdsaP256Verifier::Verify(std::span<const std::uint8_t> message,
                                              const EcdsaP256Signature& signature) const {
    if (keys_.empty()) return SignatureError("no trusted release key is available");
    auto digest = Sha256::Hash(std::as_bytes(message));
    if (!digest) return digest.error();
    auto algorithm = cng::OpenAlgorithm(BCRYPT_ECDSA_P256_ALGORITHM);
    if (!algorithm) return algorithm.error();

    // BCryptVerifySignature takes non-const buffers; it does not modify them.
    auto signatureCopy = signature;
    for (const auto& key : keys_) {
        auto handle = ImportPublicKey(algorithm->Get(), key.point);
        if (!handle) return handle.error();
        const NTSTATUS status =
            ::BCryptVerifySignature(handle->Get(), nullptr, digest->data(), static_cast<ULONG>(digest->size()),
                                    signatureCopy.data(), static_cast<ULONG>(signatureCopy.size()), 0);
        if (BCRYPT_SUCCESS(status)) return key.id;
        if (status != cng::kStatusInvalidSignature) return cng::StatusError(status, "BCryptVerifySignature");
    }
    return SignatureError("the signature does not match any trusted release key");
}

Result<std::string> EcdsaP256Verifier::Verify(std::string_view message, const EcdsaP256Signature& signature) const {
    return Verify(std::span(reinterpret_cast<const std::uint8_t*>(message.data()), message.size()), signature);
}

Result<std::string> EcdsaP256Verifier::VerifySignatureFile(std::string_view message,
                                                           std::string_view signatureFileText) const {
    auto signature = ParseSignatureFile(signatureFileText);
    if (!signature) return signature.error();
    return Verify(message, *signature);
}

}  // namespace aether::updates
