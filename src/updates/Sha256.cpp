// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/Sha256.h"

#include "platform/windows/Handles.h"
#include "platform/windows/Win32Error.h"
#include "updates/CngSupport.h"
#include "updates/Encoding.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace aether::updates {

struct Sha256::Impl {
    cng::UniqueAlgorithm algorithm;
    cng::UniqueHash hash;
};

Sha256::Sha256(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Sha256::Sha256(Sha256&& other) noexcept = default;
Sha256& Sha256::operator=(Sha256&& other) noexcept = default;
Sha256::~Sha256() = default;

Result<Sha256> Sha256::Create() {
    // BCRYPT_HASH_REUSABLE_FLAG (Windows 8+) lets Finish() reset the object instead of recreating it.
    auto algorithm = cng::OpenAlgorithm(BCRYPT_SHA256_ALGORITHM, BCRYPT_HASH_REUSABLE_FLAG);
    if (!algorithm) return algorithm.error();
    auto impl = std::make_unique<Impl>();
    impl->algorithm = std::move(algorithm).value();
    // A null hash-object buffer asks CNG to allocate and free it (Windows 7+).
    const NTSTATUS status = ::BCryptCreateHash(impl->algorithm.Get(), impl->hash.Put(), nullptr, 0, nullptr, 0,
                                               BCRYPT_HASH_REUSABLE_FLAG);
    if (!BCRYPT_SUCCESS(status)) return cng::StatusError(status, "BCryptCreateHash");
    return Sha256(std::move(impl));
}

Result<void> Sha256::Update(std::span<const std::byte> data) {
    if (!impl_) return MakeError(ErrorCode::InvalidState, "SHA-256 hasher was moved from");
    // BCryptHashData takes a ULONG length and a non-const pointer (it does not modify the input).
    constexpr std::size_t kMaxChunk = std::numeric_limits<ULONG>::max();
    while (!data.empty()) {
        const auto chunk = std::min(data.size(), kMaxChunk);
        auto* bytes = reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data()));
        const NTSTATUS status = ::BCryptHashData(impl_->hash.Get(), bytes, static_cast<ULONG>(chunk), 0);
        if (!BCRYPT_SUCCESS(status)) return cng::StatusError(status, "BCryptHashData");
        data = data.subspan(chunk);
    }
    return Ok();
}

Result<void> Sha256::Update(std::string_view data) { return Update(std::as_bytes(std::span(data.data(), data.size()))); }

Result<Sha256Digest> Sha256::Finish() {
    if (!impl_) return MakeError(ErrorCode::InvalidState, "SHA-256 hasher was moved from");
    Sha256Digest digest{};
    const NTSTATUS status =
        ::BCryptFinishHash(impl_->hash.Get(), digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (!BCRYPT_SUCCESS(status)) return cng::StatusError(status, "BCryptFinishHash");
    return digest;
}

Result<Sha256Digest> Sha256::Hash(std::span<const std::byte> data) {
    auto hasher = Create();
    if (!hasher) return hasher.error();
    if (auto updated = hasher->Update(data); !updated) return updated.error();
    return hasher->Finish();
}

Result<Sha256Digest> Sha256::Hash(std::string_view data) {
    return Hash(std::as_bytes(std::span(data.data(), data.size())));
}

Result<Sha256Digest> Sha256::HashFile(const std::filesystem::path& file, const CancellationToken& cancel) {
    platform::UniqueFileHandle handle(::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!handle) return platform::LastWin32Error("cannot open file for hashing");
    auto hasher = Create();
    if (!hasher) return hasher.error();
    std::vector<std::byte> buffer(1u << 20);
    for (;;) {
        if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "hashing was cancelled");
        DWORD read = 0;
        if (!::ReadFile(handle.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            return platform::LastWin32Error("cannot read file for hashing");
        }
        if (read == 0) break;
        if (auto updated = hasher->Update(std::span(buffer.data(), read)); !updated) return updated.error();
    }
    return hasher->Finish();
}

std::string ToHex(const Sha256Digest& digest) { return encoding::ToHex(digest); }

Result<Sha256Digest> ParseSha256Hex(std::string_view hex) {
    if (hex.size() != kSha256DigestSize * 2) {
        return MakeError(ErrorCode::Parse, "a SHA-256 digest must be exactly 64 hex characters");
    }
    for (const char c : hex) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return MakeError(ErrorCode::Parse, "a SHA-256 digest must use lowercase hex characters only");
        }
    }
    auto bytes = encoding::FromHex(hex);
    if (!bytes) return bytes.error();
    Sha256Digest digest{};
    std::copy(bytes->begin(), bytes->end(), digest.begin());
    return digest;
}

}  // namespace aether::updates
