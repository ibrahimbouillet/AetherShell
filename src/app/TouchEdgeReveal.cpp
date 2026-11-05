// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/TouchEdgeReveal.h"

#include "platform/windows/MonitorUtilities.h"

#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace aether::app {
namespace {
constexpr USAGE kDigitizerPage = 0x0D, kTouchScreen = 0x04, kTipSwitch = 0x42, kContactId = 0x51;
constexpr USAGE kDesktopPage = 0x01, kX = 0x30, kY = 0x31;
}  // namespace

std::optional<ScreenEdge> EdgeSwipeRecognizer::Contact(std::uint32_t id, bool down, double x, double y, double height, std::uint64_t now) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(height) || height <= 0.0) return std::nullopt;
    if (!down) { contacts_.erase(id); return std::nullopt; }
    const auto found = contacts_.find(id);
    if (found == contacts_.end()) {
        if (contacts_.size() >= 16) contacts_.clear();  // lost lifts cannot accumulate
        if (y <= parameters_.edgeBand) contacts_[id] = Track{ScreenEdge::Top, x, y, now, false};
        else if (y >= height - parameters_.edgeBand) contacts_[id] = Track{ScreenEdge::Bottom, x, y, now, false};
        return std::nullopt;
    }
    auto& track = found->second;
    if (track.spent) return std::nullopt;
    if (now - track.since > parameters_.maximumMs) { track.spent = true; return std::nullopt; }
    const double inward = track.edge == ScreenEdge::Top ? y - track.y : track.y - y;
    const double sideways = std::abs(x - track.x);
    if (inward < parameters_.travel) return std::nullopt;
    track.spent = true;  // one contact reveals once
    if (sideways > inward) return std::nullopt;
    return track.edge;
}

TouchDigitizerWatcher::~TouchDigitizerWatcher() { SetEnabled(nullptr, false); }

void TouchDigitizerWatcher::SetEnabled(HWND host, bool enabled) noexcept {
    if (enabled == (host_ != nullptr)) return;
    RAWINPUTDEVICE device{};
    device.usUsagePage = kDigitizerPage;
    device.usUsage = kTouchScreen;
    if (enabled) {
        // Without a touch screen there is nothing to observe and no reason to register.
        if (!host || !(::GetSystemMetrics(SM_DIGITIZER) & NID_READY) || !(::GetSystemMetrics(SM_DIGITIZER) & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH))) return;
        device.dwFlags = RIDEV_INPUTSINK;  // observes in the background; consumes nothing
        device.hwndTarget = host;
        if (::RegisterRawInputDevices(&device, 1, sizeof(device))) host_ = host;
    } else {
        device.dwFlags = RIDEV_REMOVE;
        (void)::RegisterRawInputDevices(&device, 1, sizeof(device));
        host_ = nullptr;
        recognizer_.Reset();
        contactsDown_.clear();
    }
}

std::optional<ScreenEdge> TouchDigitizerWatcher::OnRawInput(LPARAM input, bool* contactLanded) noexcept {
    try {
        if (!host_) return std::nullopt;
        UINT size = 0;
        const auto handle = reinterpret_cast<HRAWINPUT>(input);
        if (::GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size < sizeof(RAWINPUTHEADER) || size > 64 * 1024) return std::nullopt;
        std::vector<std::byte> buffer(size);
        if (::GetRawInputData(handle, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) return std::nullopt;
        const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
        if (raw->header.dwType != RIM_TYPEHID || raw->data.hid.dwSizeHid == 0) return std::nullopt;
        UINT preparsedSize = 0;
        if (::GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, nullptr, &preparsedSize) != 0 || preparsedSize == 0 || preparsedSize > 256 * 1024) return std::nullopt;
        std::vector<std::byte> preparsedBytes(preparsedSize);
        if (::GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, preparsedBytes.data(), &preparsedSize) == static_cast<UINT>(-1)) return std::nullopt;
        const auto preparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsedBytes.data());
        HIDP_CAPS caps{};
        if (::HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS || caps.NumberInputValueCaps == 0) return std::nullopt;
        USHORT valueCount = caps.NumberInputValueCaps;
        std::vector<HIDP_VALUE_CAPS> values(valueCount);
        if (::HidP_GetValueCaps(HidP_Input, values.data(), &valueCount, preparsed) != HIDP_STATUS_SUCCESS) return std::nullopt;
        const auto monitor = platform::PrimaryMonitor();
        const double scale = 96.0 / std::max<UINT>(monitor.dpi, 1);
        double width = static_cast<double>(platform::Width(monitor.bounds)) * scale, height = static_cast<double>(platform::Height(monitor.bounds)) * scale;
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        const DWORD orientation = ::EnumDisplaySettingsExW(nullptr, ENUM_CURRENT_SETTINGS, &mode, 0) ? mode.dmDisplayOrientation : DMDO_DEFAULT;
        const auto now = ::GetTickCount64();
        std::optional<ScreenEdge> revealed;
        auto* report = const_cast<PCHAR>(reinterpret_cast<const char*>(raw->data.hid.bRawData));
        const ULONG reportSize = raw->data.hid.dwSizeHid;
        for (DWORD index = 0; index < raw->data.hid.dwCount; ++index, report += reportSize) {
            for (USHORT i = 0; i < valueCount; ++i) {
                const auto& xCaps = values[i];
                if (xCaps.UsagePage != kDesktopPage || xCaps.IsRange || xCaps.NotRange.Usage != kX) continue;
                const USHORT link = xCaps.LinkCollection;
                const auto yCaps = std::find_if(values.begin(), values.begin() + valueCount, [&](const HIDP_VALUE_CAPS& candidate) {
                    return candidate.UsagePage == kDesktopPage && !candidate.IsRange && candidate.NotRange.Usage == kY && candidate.LinkCollection == link;
                });
                if (yCaps == values.begin() + valueCount) continue;
                ULONG rawX = 0, rawY = 0, contactId = 0;
                if (::HidP_GetUsageValue(HidP_Input, kDesktopPage, link, kX, &rawX, preparsed, report, reportSize) != HIDP_STATUS_SUCCESS ||
                    ::HidP_GetUsageValue(HidP_Input, kDesktopPage, link, kY, &rawY, preparsed, report, reportSize) != HIDP_STATUS_SUCCESS ||
                    ::HidP_GetUsageValue(HidP_Input, kDigitizerPage, link, kContactId, &contactId, preparsed, report, reportSize) != HIDP_STATUS_SUCCESS) continue;
                USAGE pressed[16]{};
                ULONG pressedCount = 16;
                bool down = false;
                if (::HidP_GetUsages(HidP_Input, kDigitizerPage, link, pressed, &pressedCount, preparsed, report, reportSize) == HIDP_STATUS_SUCCESS)
                    down = std::find(pressed, pressed + pressedCount, kTipSwitch) != pressed + pressedCount;
                if (!down) contactsDown_.erase(contactId);
                else {
                    if (contactsDown_.size() >= 16) contactsDown_.clear();  // lost lifts cannot accumulate
                    if (contactsDown_.insert(contactId).second && contactLanded) *contactLanded = true;
                }
                const auto ratio = [](ULONG value, const HIDP_VALUE_CAPS& range) {
                    const double span = static_cast<double>(range.LogicalMax) - range.LogicalMin;
                    return span > 0.0 ? std::clamp((static_cast<double>(value) - range.LogicalMin) / span, 0.0, 1.0) : 0.0;
                };
                double nx = ratio(rawX, xCaps), ny = ratio(rawY, *yCaps);
                // The digitizer reports in its own axes; the top and bottom edges follow the display's rotation.
                if (orientation == DMDO_180) { nx = 1.0 - nx; ny = 1.0 - ny; }
                else if (orientation == DMDO_90) { const double t = nx; nx = 1.0 - ny; ny = t; }
                else if (orientation == DMDO_270) { const double t = nx; nx = ny; ny = 1.0 - t; }
                if (const auto edge = recognizer_.Contact(contactId, down, nx * width, ny * height, height, now)) revealed = edge;
            }
        }
        return revealed;
    } catch (...) { return std::nullopt; }
}

}  // namespace aether::app
