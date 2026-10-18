// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Helpers shared by the providers that call Windows Runtime APIs through C++/WinRT (battery and
// network). Include only from .cpp files: it pulls in <winrt/base.h>.
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <winrt/base.h>

#include <optional>
#include <string>

namespace aether::status {

// Owns one balanced MTA initialization on this thread. A separately owned runtime lease keeps the
// shared MTA alive between provider sessions; unlike the old implementation, its cookie is stored
// and balanced at explicit final shutdown (or process-static RAII cleanup).
class WinRtApartmentScope {
public:
    WinRtApartmentScope() noexcept;
    ~WinRtApartmentScope();
    WinRtApartmentScope(const WinRtApartmentScope&) = delete;
    WinRtApartmentScope& operator=(const WinRtApartmentScope&) = delete;

    [[nodiscard]] bool IsUsable() const noexcept { return SUCCEEDED(status_); }
    [[nodiscard]] HRESULT Status() const noexcept { return status_; }

private:
    HRESULT status_ = E_PENDING;
};

// Final application shutdown only, after every status provider has stopped. Refuses active scopes;
// clears cached WinRT factories and releases the runtime cookie exactly once. Terminal: later
// WinRT scopes fail rather than recreating torn-down factories; battery can still use its Win32 fallback.
// Ordinary provider Stop/restart must not call this function.
[[nodiscard]] Result<void> FinalizeStatusRuntime();

// "Message text (0x80070005)" in UTF-8, for snapshot details and logs.
[[nodiscard]] std::string DescribeHResult(const winrt::hresult_error& error);

// Runs `reader` and returns its value. A WinRT failure leaves the value unknown (std::nullopt) and records
// the first error met in `firstError`, so one unreadable property never hides the others.
template <class T, class Reader>
[[nodiscard]] std::optional<T> TryRead(Reader&& reader, std::string& firstError) {
    try {
        return static_cast<T>(reader());
    } catch (const winrt::hresult_error& error) {
        if (firstError.empty()) firstError = DescribeHResult(error);
    }
    return std::nullopt;
}

}  // namespace aether::status
