// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Injectable clock so time-dependent logic (update scheduling, cache expiry)
// can be unit tested deterministically.
#pragma once

#include <chrono>
#include <mutex>

namespace aether {

class ITimeSource {
public:
    virtual ~ITimeSource() = default;
    [[nodiscard]] virtual std::chrono::system_clock::time_point SystemNow() const = 0;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point SteadyNow() const = 0;
};

class SystemTimeSource final : public ITimeSource {
public:
    [[nodiscard]] std::chrono::system_clock::time_point SystemNow() const override {
        return std::chrono::system_clock::now();
    }
    [[nodiscard]] std::chrono::steady_clock::time_point SteadyNow() const override {
        return std::chrono::steady_clock::now();
    }
};

// Manually advanced clock for tests.
class FakeTimeSource final : public ITimeSource {
public:
    explicit FakeTimeSource(std::chrono::system_clock::time_point start = std::chrono::system_clock::time_point{
                                std::chrono::seconds{1'790'000'000}})
        : system_(start) {}

    [[nodiscard]] std::chrono::system_clock::time_point SystemNow() const override {
        std::lock_guard lock(mutex_);
        return system_;
    }
    [[nodiscard]] std::chrono::steady_clock::time_point SteadyNow() const override {
        std::lock_guard lock(mutex_);
        return steady_;
    }
    void Advance(std::chrono::nanoseconds delta) {
        std::lock_guard lock(mutex_);
        system_ += std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
        steady_ += std::chrono::duration_cast<std::chrono::steady_clock::duration>(delta);
    }

private:
    mutable std::mutex mutex_;
    std::chrono::system_clock::time_point system_;
    std::chrono::steady_clock::time_point steady_{};
};

}  // namespace aether
