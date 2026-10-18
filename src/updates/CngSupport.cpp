// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/CngSupport.h"

#include <format>

namespace aether::updates::cng {

Error StatusError(NTSTATUS status, std::string_view operation) {
    return MakeError(ErrorCode::Platform,
                     std::format("{} failed (NTSTATUS 0x{:08X})", operation, static_cast<std::uint32_t>(status)),
                     static_cast<std::int64_t>(static_cast<std::uint32_t>(status)));
}

Result<UniqueAlgorithm> OpenAlgorithm(const wchar_t* algorithm, ULONG flags) {
    UniqueAlgorithm handle;
    const NTSTATUS status = ::BCryptOpenAlgorithmProvider(handle.Put(), algorithm, nullptr, flags);
    if (!BCRYPT_SUCCESS(status)) return StatusError(status, "BCryptOpenAlgorithmProvider");
    return handle;
}

}  // namespace aether::updates::cng
