// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include <cstdint>

namespace aether::platform {

struct OsVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t build = 0;
};

// True OS version via RtlGetVersion (not subject to manifest-based version lies).
[[nodiscard]] OsVersion GetOsVersion() noexcept;
[[nodiscard]] bool IsBuildOrGreater(std::uint32_t build) noexcept;
// Windows 11 reports major version 10 with build >= 22000.
[[nodiscard]] inline bool IsWindows11OrGreater() noexcept { return IsBuildOrGreater(22000); }

}  // namespace aether::platform
