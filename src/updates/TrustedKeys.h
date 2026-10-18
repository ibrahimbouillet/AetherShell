// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Public keys that may sign release manifests (ECDSA P-256, see
// docs/decisions/0004-update-trust-model.md). Production code builds its
// verifier from this list only; tests pass their own keys to the
// EcdsaP256Verifier constructor instead.
//
// Each key is the uncompressed curve point X || Y (32 + 32 bytes) in hex,
// without the 0x04 prefix. The matching private keys are held offline by the
// project owner and never enter this repository.
//
// Rotation: add the new key here and ship a release signed with the OLD key
// that already contains the new one; once every supported version trusts the
// new key, sign with it and remove the old entry in a later release.
#pragma once

#include <array>
#include <string_view>

namespace aether::updates::trust {

struct TrustedKey {
    std::string_view id;            // label for logs and documentation only
    std::string_view publicKeyHex;  // 128 hex characters: X || Y
};

inline constexpr std::array kReleaseSigningKeys{
    TrustedKey{"aether-release-2026-1",
               "2b0310c62dd1d8ec4e6b16c9caf44fced9f8df43f3807bc9d3e425ca28f4e12f"
               "66b5ed190dad7d96f79405a82e7f5e60e5caa622a379bc19edfb42c92cb0ce02"},
};

}  // namespace aether::updates::trust
