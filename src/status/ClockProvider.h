// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Wall-clock provider for the status bar. A dedicated thread arms a waitable timer for the next
// second or minute boundary (absolute UTC due time), publishes a snapshot when it fires and re-arms:
// there is no polling and no accumulated drift, because every wake-up re-reads the system time.
//
// Time changes: the application calls NotifyTimeChanged() on WM_TIMECHANGE and on WM_SETTINGCHANGE
// "intl" (both are surfaced by platform::SystemEvents). The provider then republishes immediately and
// re-reads the time zone. Sleep/resume and clock jumps are handled without help: the absolute timer
// fires on resume, and a watchdog wait (expected interval + slack, measured on the interrupt clock)
// catches a clock set backwards.
#pragma once

#include "status/ClockTime.h"
#include "status/StatusTypes.h"

#include <memory>

namespace aether::status {

class ClockProvider final : public IStatusProvider<ClockSnapshot> {
public:
    explicit ClockProvider(ClockResolution resolution = ClockResolution::Minutes);
    ~ClockProvider() override;
    ClockProvider(const ClockProvider&) = delete;
    ClockProvider& operator=(const ClockProvider&) = delete;

    // Starts the timer thread; the first snapshot is published right away from that thread.
    Result<void> Start(Listener listener) override;
    void Stop() noexcept override;
    [[nodiscard]] ClockSnapshot Current() const override;

    // Thread-safe. Takes effect immediately: a fresh snapshot is published and the timer is re-armed
    // for the next boundary of the new resolution.
    void SetResolution(ClockResolution resolution) noexcept;
    [[nodiscard]] ClockResolution Resolution() const noexcept;

    // Thread-safe. Call on WM_TIMECHANGE, WM_SETTINGCHANGE("intl") and on resume from sleep. Publishes a
    // fresh snapshot (time zone re-read) without waiting for the next boundary.
    void NotifyTimeChanged() noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace aether::status
