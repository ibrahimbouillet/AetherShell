// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// SHA-256 through Windows CNG (BCRYPT_SHA256_ALGORITHM). Used for the
// integrity half of the update trust model: the digest of a downloaded asset
// must equal the digest listed in the signed release manifest
// (docs/decisions/0004-update-trust-model.md). A matching digest proves the
// bytes are the ones the manifest describes; it never proves who published them.
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace aether::updates {

inline constexpr std::size_t kSha256DigestSize = 32;
using Sha256Digest = std::array<std::uint8_t, kSha256DigestSize>;

// Incremental hasher. Not thread-safe: one instance is used by one thread at a time.
class Sha256 {
public:
    [[nodiscard]] static Result<Sha256> Create();

    Sha256(Sha256&& other) noexcept;
    Sha256& operator=(Sha256&& other) noexcept;
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    ~Sha256();

    Result<void> Update(std::span<const std::byte> data);
    Result<void> Update(std::string_view data);

    // Returns the digest of everything passed to Update() and resets the hasher for a new message.
    [[nodiscard]] Result<Sha256Digest> Finish();

    // One-shot helpers.
    [[nodiscard]] static Result<Sha256Digest> Hash(std::span<const std::byte> data);
    [[nodiscard]] static Result<Sha256Digest> Hash(std::string_view data);
    // Streams the file in 1 MiB blocks; observes `cancel` between blocks. Disk work: worker threads only.
    [[nodiscard]] static Result<Sha256Digest> HashFile(const std::filesystem::path& file,
                                                       const CancellationToken& cancel = {});

private:
    struct Impl;
    explicit Sha256(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

// 64 lowercase hex characters.
[[nodiscard]] std::string ToHex(const Sha256Digest& digest);
// Exactly 64 lowercase hex characters (the manifest format). Errors use ErrorCode::Parse.
[[nodiscard]] Result<Sha256Digest> ParseSha256Hex(std::string_view hex);

}  // namespace aether::updates
