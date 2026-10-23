// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "showcase/FrameCompositor.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace aether::showcase {

// Synchronous capture of a caller-selected screen rectangle. Desktop-wide capture is reserved for
// the owner-authorized clean showcase runner; tests must pass only their own isolated test windows.
class GdiDesktopCapture final {
public:
    [[nodiscard]] static Result<GdiDesktopCapture> Capture(const RECT& boundsPixels) noexcept;
    [[nodiscard]] static Result<GdiDesktopCapture> CaptureVirtualDesktop() noexcept;

    [[nodiscard]] RgbaFrameView Frame() const noexcept;

private:
    GdiDesktopCapture(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> pixels) noexcept;

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

// Reuses the screen DC, memory DC and DIB for repeated capture so a real-time capture loop does
// not allocate and destroy GDI surfaces for every video frame.
class GdiDesktopCaptureSession final {
public:
    GdiDesktopCaptureSession() noexcept;
    ~GdiDesktopCaptureSession();

    GdiDesktopCaptureSession(const GdiDesktopCaptureSession&) = delete;
    GdiDesktopCaptureSession& operator=(const GdiDesktopCaptureSession&) = delete;
    GdiDesktopCaptureSession(GdiDesktopCaptureSession&& other) noexcept;
    GdiDesktopCaptureSession& operator=(GdiDesktopCaptureSession&& other) noexcept;

    [[nodiscard]] static Result<GdiDesktopCaptureSession> Create(const RECT& boundsPixels) noexcept;
    [[nodiscard]] Result<RgbaFrameView> Capture(bool includeSystemCursor = false) noexcept;
    [[nodiscard]] RECT Bounds() const noexcept;

private:
    struct Impl;
    explicit GdiDesktopCaptureSession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::showcase
