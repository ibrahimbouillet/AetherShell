// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/PointerInput.h"

#include <utility>

namespace aether::platform {
namespace {
// Documented signature of mouse messages generated from pen or touch input (GetMessageExtraInfo).
constexpr LPARAM kSignatureMask = static_cast<LPARAM>(0xFFFFFF00);
constexpr LPARAM kPromotedSignature = static_cast<LPARAM>(0xFF515700);
constexpr LPARAM kTouchBit = 0x80;

thread_local std::function<std::optional<PointerContact>(WPARAM)> testSource;

double Seconds(UINT64 counter) noexcept {
    static const double frequency = [] {
        LARGE_INTEGER value{};
        ::QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    return frequency > 0.0 ? static_cast<double>(counter) / frequency : 0.0;
}
}  // namespace

bool IsPointerMessage(UINT message) noexcept {
    return message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP ||
           message == WM_POINTERCAPTURECHANGED;
}

void SetPointerSourceForTesting(std::function<std::optional<PointerContact>(WPARAM)> source) {
    testSource = std::move(source);
}

bool IsTouchPointer(WPARAM wParam) noexcept {
    if (testSource) {
        const auto contact = testSource(wParam);
        return contact && contact->touch;
    }
    POINTER_INPUT_TYPE type = PT_POINTER;
    return ::GetPointerType(GET_POINTERID_WPARAM(wParam), &type) && type == PT_TOUCH;
}

std::optional<PointerContact> ReadPointer(WPARAM wParam) noexcept {
    POINTER_INFO info{};
    const UINT32 id = GET_POINTERID_WPARAM(wParam);
    if (testSource) return testSource(wParam);
    if (!::GetPointerInfo(id, &info)) return std::nullopt;
    PointerContact contact;
    contact.id = id;
    contact.screenPx = info.ptPixelLocation;
    contact.touch = info.pointerType == PT_TOUCH;
    contact.primary = (info.pointerFlags & POINTER_FLAG_PRIMARY) != 0;
    contact.inContact = (info.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
    contact.canceled = (info.pointerFlags & POINTER_FLAG_CANCELED) != 0;
    if (info.PerformanceCount) contact.time = Seconds(info.PerformanceCount);
    else {
        LARGE_INTEGER now{};
        ::QueryPerformanceCounter(&now);
        contact.time = Seconds(static_cast<UINT64>(now.QuadPart));
    }
    return contact;
}

bool IsTouchPromotedMouse() noexcept {
    const LPARAM extra = ::GetMessageExtraInfo();
    return (extra & kSignatureMask) == kPromotedSignature && (extra & kTouchBit) != 0;
}

}  // namespace aether::platform
