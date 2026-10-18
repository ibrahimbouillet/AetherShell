// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/WinRtSupport.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"

#include <objbase.h>

#include <cstdint>
#include <format>
#include <mutex>
#include <utility>

namespace aether::status {
namespace {

std::mutex apartmentMutex;
std::size_t activeApartments = 0;  // protected by apartmentMutex

class MtaRuntime {
public:
    ~MtaRuntime() { Release(); }
    HRESULT Ensure() noexcept {
        if (finalized_) return RO_E_CLOSED;
        if (cookie_ != nullptr) return S_OK;
        return ::CoIncrementMTAUsage(&cookie_);
    }
    void Release() noexcept {
        finalized_ = true;
        if (cookie_ == nullptr) return;
        winrt::clear_factory_cache();
        ::CoDecrementMTAUsage(std::exchange(cookie_, nullptr));
    }
private:
    CO_MTA_USAGE_COOKIE cookie_ = nullptr;
    bool finalized_ = false;
};

MtaRuntime& Runtime() {
    static MtaRuntime runtime;
    return runtime;
}

}  // namespace

WinRtApartmentScope::WinRtApartmentScope() noexcept {
    std::lock_guard lock(apartmentMutex);
    status_ = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(status_)) return;
    const HRESULT runtime = Runtime().Ensure();
    if (FAILED(runtime)) {
        ::CoUninitialize();
        status_ = runtime;
        return;
    }
    ++activeApartments;
}

WinRtApartmentScope::~WinRtApartmentScope() {
    if (FAILED(status_)) return;
    std::lock_guard lock(apartmentMutex);
    --activeApartments;
    ::CoUninitialize();
}

Result<void> FinalizeStatusRuntime() {
    std::lock_guard lock(apartmentMutex);
    if (activeApartments != 0) return MakeError(ErrorCode::InvalidState, "status WinRT providers are still running");
    Runtime().Release();
    return Ok();
}

std::string DescribeHResult(const winrt::hresult_error& error) {
    return std::format("{} (0x{:08X})", text::Narrow(std::wstring_view(error.message())),
                       static_cast<std::uint32_t>(error.code().value));
}

}  // namespace aether::status
