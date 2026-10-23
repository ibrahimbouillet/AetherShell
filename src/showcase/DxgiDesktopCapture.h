// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "showcase/FrameCompositor.h"

#include <Windows.h>

#include <cstdint>
#include <memory>

namespace aether::showcase {

// Hardware Desktop Duplication for one complete, unrotated SDR monitor. No GDI capture fallback
// is hidden behind this interface. Creation does not acquire desktop pixels. Desktop-wide use is
// reserved for the user-started clean showcase; automated capture tests must use private backdrops.
class DxgiDesktopCaptureSession final {
public:
    DxgiDesktopCaptureSession() noexcept;
    ~DxgiDesktopCaptureSession();
    DxgiDesktopCaptureSession(const DxgiDesktopCaptureSession&) = delete;
    DxgiDesktopCaptureSession& operator=(const DxgiDesktopCaptureSession&) = delete;
    DxgiDesktopCaptureSession(DxgiDesktopCaptureSession&& other) noexcept;
    DxgiDesktopCaptureSession& operator=(DxgiDesktopCaptureSession&& other) noexcept;

    [[nodiscard]] static Result<DxgiDesktopCaptureSession> Create(const RECT& boundsPixels) noexcept;
    // Waits no more than 16ms for a DWM update; a timeout reuses the latest genuine desktop pixels
    // and refreshes the separate hardware cursor. The returned view is borrowed until the next call.
    [[nodiscard]] Result<RgbaFrameView> Capture(bool includeSystemCursor = false) noexcept;
    [[nodiscard]] RECT Bounds() const noexcept;
    // Pointer-only acquisitions do not count as a new desktop image. This is false after a timeout.
    [[nodiscard]] bool UpdatedLastCapture() const noexcept;
    // QPC timestamp supplied by DXGI for the latest real desktop present; zero means unavailable.
    [[nodiscard]] std::int64_t LastPresentCounter() const noexcept;
    // Successful acquisitions include pointer-only notifications; DesktopFrameCount excludes them.
    [[nodiscard]] std::uint64_t AcquiredFrameCount() const noexcept;
    [[nodiscard]] std::uint64_t DesktopFrameCount() const noexcept;

private:
    struct Impl;
    explicit DxgiDesktopCaptureSession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::showcase
