// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FR-040 touch access to auto-hidden bars. A window at the screen edge would intercept input belonging
// to other applications (FR-039 forbids it), so the swipe is observed instead: raw digitizer reports
// are read passively while Auto-Hide is on. Nothing is consumed, so applications and Windows' own edge
// gestures receive every touch as usual. Positions are used only for this decision, never stored or
// logged. Registration is best effort: without it the bars remain reachable by keyboard and pointer.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>

namespace aether::app {

enum class ScreenEdge { Top, Bottom };

// Pure decision (unit tested): a contact that lands on a screen edge and moves inward. Units are the
// caller's (DIPs); time is milliseconds. A contact that starts elsewhere, drifts sideways or takes too
// long is an ordinary touch.
struct EdgeSwipeParameters {
    double edgeBand = 18.0;
    double travel = 44.0;
    std::uint64_t maximumMs = 900;
};
class EdgeSwipeRecognizer {
public:
    explicit EdgeSwipeRecognizer(EdgeSwipeParameters parameters = {}) noexcept : parameters_(parameters) {}
    // One report of a contact on a surface `height` long. Returns the edge when this report completes a swipe.
    [[nodiscard]] std::optional<ScreenEdge> Contact(std::uint32_t id, bool down, double x, double y, double height, std::uint64_t now);
    void Reset() noexcept { contacts_.clear(); }

private:
    struct Track { ScreenEdge edge; double x, y; std::uint64_t since; bool spent; };
    EdgeSwipeParameters parameters_;
    std::map<std::uint32_t, Track> contacts_;
};

// Parses HID touch reports for the host window, which forwards WM_INPUT.
class TouchDigitizerWatcher {
public:
    TouchDigitizerWatcher() = default;
    ~TouchDigitizerWatcher();
    TouchDigitizerWatcher(const TouchDigitizerWatcher&) = delete;
    TouchDigitizerWatcher& operator=(const TouchDigitizerWatcher&) = delete;
    void SetEnabled(HWND host, bool enabled) noexcept;
    [[nodiscard]] bool Enabled() const noexcept { return host_ != nullptr; }
    // `contactLanded` is set when this report starts a new contact, anywhere (D60: open panels close).
    [[nodiscard]] std::optional<ScreenEdge> OnRawInput(LPARAM input, bool* contactLanded = nullptr) noexcept;

private:
    HWND host_ = nullptr;
    EdgeSwipeRecognizer recognizer_;
    std::set<std::uint32_t> contactsDown_;
};

}  // namespace aether::app
