// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Internal helpers for Windows CNG (bcrypt.h): RAII handle types and NTSTATUS
// error mapping shared by Sha256 and EcdsaP256Verifier. Not part of the public API.
#pragma once

#include "core/Result.h"
#include "platform/windows/Handles.h"

#include <windows.h>
#include <bcrypt.h>

#include <string_view>

namespace aether::updates::cng {

struct AlgorithmTraits {
    using Type = BCRYPT_ALG_HANDLE;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type handle) noexcept { ::BCryptCloseAlgorithmProvider(handle, 0); }
};

struct HashTraits {
    using Type = BCRYPT_HASH_HANDLE;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type handle) noexcept { ::BCryptDestroyHash(handle); }
};

struct KeyTraits {
    using Type = BCRYPT_KEY_HANDLE;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type handle) noexcept { ::BCryptDestroyKey(handle); }
};

using UniqueAlgorithm = platform::UniqueResource<AlgorithmTraits>;
using UniqueHash = platform::UniqueResource<HashTraits>;
using UniqueKey = platform::UniqueResource<KeyTraits>;

// STATUS_INVALID_SIGNATURE from ntstatus.h (which cannot be included next to windows.h without the
// WIN32_NO_STATUS dance).
inline constexpr NTSTATUS kStatusInvalidSignature = static_cast<NTSTATUS>(0xC000A000L);

// "BCryptCreateHash failed (NTSTATUS 0xC0000017)" with ErrorCode::Platform and the status as platformCode.
[[nodiscard]] Error StatusError(NTSTATUS status, std::string_view operation);

// BCryptOpenAlgorithmProvider for a built-in (Microsoft Primitive Provider) algorithm.
[[nodiscard]] Result<UniqueAlgorithm> OpenAlgorithm(const wchar_t* algorithm, ULONG flags = 0);

}  // namespace aether::updates::cng
