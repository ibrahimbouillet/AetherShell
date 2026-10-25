// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Test-only process-lifetime fixture. It has no windows, settings, files, application launch or
// termination privileges. The parent integration test asks it to exit through its private event.
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto* values = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (!values || count != 3) { if (values) ::LocalFree(values); return 2; }
    std::array<HANDLE, 2> events{};
    for (std::size_t i = 0; i < events.size(); ++i) {
        std::string digits;
        for (const auto* value = values[i + 1]; *value; ++value) {
            if (*value < L'0' || *value > L'9') { ::LocalFree(values); return 2; }
            digits.push_back(static_cast<char>(*value));
        }
        std::uintptr_t number = 0;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || !number) { ::LocalFree(values); return 2; }
        events[i] = reinterpret_cast<HANDLE>(number);
    }
    ::LocalFree(values);
    if (!::SetEvent(events[0])) return 3;
    return ::WaitForSingleObject(events[1], 30000) == WAIT_OBJECT_0 ? 0 : 4;
}
