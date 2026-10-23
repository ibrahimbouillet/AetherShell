// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include "core/Result.h"
#include <cstdint>
#include <functional>
#include <memory>
namespace aether::showcase {
// Single acquisition worker. The callback must contain capture/guard/buffer work only, never UIA,
// disk I/O or app/device actions. Poll is nonblocking while acquisition runs; Finish joins first.
class ConcurrentRecorder final {
public:
    using Acquire = std::function<Result<void>(double elapsedSeconds)>;
    ConcurrentRecorder() noexcept;
    ~ConcurrentRecorder();
    ConcurrentRecorder(ConcurrentRecorder&&) noexcept;
    ConcurrentRecorder& operator=(ConcurrentRecorder&&) noexcept;
    ConcurrentRecorder(const ConcurrentRecorder&)=delete;
    ConcurrentRecorder& operator=(const ConcurrentRecorder&)=delete;
    [[nodiscard]] static Result<ConcurrentRecorder> Start(double durationSeconds,
        std::uint32_t framesPerSecond, Acquire acquire) noexcept;
    [[nodiscard]] Result<bool> Poll() const noexcept;
    [[nodiscard]] double ElapsedSeconds() const noexcept;
    [[nodiscard]] Result<void> Finish() noexcept;
private:
    struct Impl;
    explicit ConcurrentRecorder(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
}
