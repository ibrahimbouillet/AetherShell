// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/OsVersion.h"

#include <windows.h>

namespace aether::platform {

OsVersion GetOsVersion() noexcept {
    static const OsVersion cached = [] {
        OsVersion version;
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
            auto fn = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"));
            RTL_OSVERSIONINFOW info{sizeof(info)};
            if (fn && fn(&info) == 0) {
                version.major = info.dwMajorVersion;
                version.minor = info.dwMinorVersion;
                version.build = info.dwBuildNumber;
            }
        }
        return version;
    }();
    return cached;
}

bool IsBuildOrGreater(std::uint32_t build) noexcept {
    const auto version = GetOsVersion();
    if (version.major != 10) return version.major > 10;
    return version.build >= build;
}

}  // namespace aether::platform
