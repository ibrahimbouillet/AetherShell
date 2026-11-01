// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Synthetic touch contacts for own-window suites: WM_POINTER messages sent to this test's own windows
// (Windows does not deliver posted pointer messages), read back through platform::SetPointerSourceForTesting. They exercise the real routing
// and arbitration only. No digitizer, Windows gesture or real-input timing is involved, so this is
// never evidence of physical-touch behavior.
#pragma once

#include "graphics/FrameScheduler.h"
#include "platform/windows/PointerInput.h"

#include <cstdint>
#include <map>
#include <optional>

namespace aether::test {

class FakeTouch {
public:
    FakeTouch() {
        platform::SetPointerSourceForTesting([this](WPARAM wParam) -> std::optional<platform::PointerContact> {
            const auto found = contacts_.find(LOWORD(wParam));
            if (found == contacts_.end()) return std::nullopt;
            return found->second;
        });
    }
    ~FakeTouch() { platform::SetPointerSourceForTesting({}); }
    FakeTouch(const FakeTouch&) = delete;
    FakeTouch& operator=(const FakeTouch&) = delete;

    // Contact timestamps are the frame clock plus this offset, so a suite can script exact speeds.
    void Advance(double seconds) noexcept { skew_ += seconds; }
    [[nodiscard]] double Time() const noexcept { return graphics::FrameScheduler::Now() + skew_; }

    WPARAM Set(std::uint32_t id, POINT screen, bool canceled = false, bool touch = true, bool inContact = true) {
        platform::PointerContact contact;
        contact.id = id; contact.screenPx = screen; contact.time = Time();
        contact.touch = touch; contact.primary = true; contact.inContact = inContact; contact.canceled = canceled;
        contacts_[id] = contact;
        return MAKEWPARAM(id, 0);
    }
    void Lift(std::uint32_t id) { contacts_.erase(id); }  // the system no longer knows this contact

    LRESULT Send(HWND target, UINT message, std::uint32_t id, POINT screen, bool canceled = false) {
        return ::SendMessageW(target, message, Set(id, screen, canceled), 0);
    }

private:
    std::map<std::uint32_t, platform::PointerContact> contacts_;
    double skew_ = 0.0;
};

}  // namespace aether::test
