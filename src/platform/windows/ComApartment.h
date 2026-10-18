// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include <windows.h>
#include <objbase.h>

namespace aether::platform {

// Initializes COM for the current thread for the lifetime of the object.
// RPC_E_CHANGED_MODE (already initialized differently) is tolerated: COM is usable, but this object
// will not uninitialize it.
class ComApartment {
public:
    explicit ComApartment(DWORD model = COINIT_APARTMENTTHREADED) noexcept
        : result_(::CoInitializeEx(nullptr, model | COINIT_DISABLE_OLE1DDE)) {}
    ~ComApartment() {
        if (SUCCEEDED(result_)) ::CoUninitialize();
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    [[nodiscard]] HRESULT Status() const noexcept { return result_; }
    [[nodiscard]] bool IsUsable() const noexcept { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT result_;
};

}  // namespace aether::platform
