// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/GdiDesktopCapture.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace aether::showcase {
namespace {

constexpr std::uint32_t kMaximumDimension = 16'384;

struct GdiResources final {
    HDC screen = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;

    ~GdiResources() {
        if (memory && previousBitmap && previousBitmap != HGDI_ERROR) (void)::SelectObject(memory, previousBitmap);
        if (bitmap) (void)::DeleteObject(bitmap);
        if (memory) (void)::DeleteDC(memory);
        if (screen) (void)::ReleaseDC(nullptr, screen);
    }
};

Error CaptureError(const char* message) noexcept {
    return MakeError(ErrorCode::Platform, message, static_cast<std::int64_t>(::GetLastError()));
}

}  // namespace

struct GdiDesktopCaptureSession::Impl final {
    GdiResources resources;
    RECT bounds{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    void* dibPixels = nullptr;
    std::vector<std::uint8_t> rgba;
};

GdiDesktopCapture::GdiDesktopCapture(std::uint32_t width, std::uint32_t height,
                                     std::vector<std::uint8_t> pixels) noexcept
    : width_(width), height_(height), pixels_(std::move(pixels)) {}

Result<GdiDesktopCapture> GdiDesktopCapture::Capture(const RECT& boundsPixels) noexcept {
    auto session = GdiDesktopCaptureSession::Create(boundsPixels);
    if (!session) return session.error();
    auto frame = session->Capture();
    if (!frame) return frame.error();
    try {
        return GdiDesktopCapture(frame->width, frame->height,
                                 std::vector<std::uint8_t>(frame->pixels.begin(), frame->pixels.end()));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase screen capture buffer allocation failed");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase screen capture failed without exposing desktop details");
    }
}

Result<GdiDesktopCapture> GdiDesktopCapture::CaptureVirtualDesktop() noexcept {
    RECT bounds{};
    bounds.left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    bounds.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    bounds.right = bounds.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    bounds.bottom = bounds.top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        return MakeError(ErrorCode::Unavailable, "showcase virtual desktop geometry is unavailable");
    }
    return Capture(bounds);
}

RgbaFrameView GdiDesktopCapture::Frame() const noexcept {
    return {width_, height_, static_cast<std::size_t>(width_) * 4U, pixels_};
}

GdiDesktopCaptureSession::GdiDesktopCaptureSession() noexcept = default;
GdiDesktopCaptureSession::GdiDesktopCaptureSession(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
GdiDesktopCaptureSession::~GdiDesktopCaptureSession() = default;
GdiDesktopCaptureSession::GdiDesktopCaptureSession(GdiDesktopCaptureSession&& other) noexcept = default;
GdiDesktopCaptureSession& GdiDesktopCaptureSession::operator=(GdiDesktopCaptureSession&& other) noexcept = default;

Result<GdiDesktopCaptureSession> GdiDesktopCaptureSession::Create(const RECT& boundsPixels) noexcept {
    const auto width = static_cast<std::int64_t>(boundsPixels.right) - boundsPixels.left;
    const auto height = static_cast<std::int64_t>(boundsPixels.bottom) - boundsPixels.top;
    if (width <= 0 || height <= 0 || width > kMaximumDimension || height > kMaximumDimension) {
        return MakeError(ErrorCode::InvalidArgument, "showcase capture rectangle is invalid or too large");
    }
    const auto pixelCount = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U ||
        pixelCount > std::numeric_limits<DWORD>::max() / 4U) {
        return MakeError(ErrorCode::InvalidArgument, "showcase capture buffer exceeds supported size");
    }
    try {
        auto impl = std::make_unique<Impl>();
        impl->bounds = boundsPixels;
        impl->width = static_cast<std::uint32_t>(width);
        impl->height = static_cast<std::uint32_t>(height);
        impl->rgba.resize(static_cast<std::size_t>(pixelCount) * 4U);
        impl->resources.screen = ::GetDC(nullptr);
        if (!impl->resources.screen) return CaptureError("showcase screen capture could not acquire the desktop");
        impl->resources.memory = ::CreateCompatibleDC(impl->resources.screen);
        if (!impl->resources.memory) return CaptureError("showcase screen capture could create its private buffer");

        BITMAPINFO information{};
        information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        information.bmiHeader.biWidth = static_cast<LONG>(impl->width);
        information.bmiHeader.biHeight = -static_cast<LONG>(impl->height);
        information.bmiHeader.biPlanes = 1;
        information.bmiHeader.biBitCount = 32;
        information.bmiHeader.biCompression = BI_RGB;
        impl->resources.bitmap = ::CreateDIBSection(impl->resources.screen, &information, DIB_RGB_COLORS,
                                                     &impl->dibPixels, nullptr, 0);
        if (!impl->resources.bitmap || !impl->dibPixels)
            return CaptureError("showcase screen capture could not allocate its pixel surface");
        impl->resources.previousBitmap = ::SelectObject(impl->resources.memory, impl->resources.bitmap);
        if (!impl->resources.previousBitmap || impl->resources.previousBitmap == HGDI_ERROR)
            return CaptureError("showcase screen capture could not select its pixel surface");
        return GdiDesktopCaptureSession(std::move(impl));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase screen capture buffer allocation failed");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase screen capture session could not be created");
    }
}

Result<RgbaFrameView> GdiDesktopCaptureSession::Capture(bool includeSystemCursor) noexcept {
    if (!impl_ || !impl_->resources.memory || !impl_->dibPixels)
        return MakeError(ErrorCode::InvalidState, "showcase screen capture session is unavailable");
    if (!::BitBlt(impl_->resources.memory, 0, 0, static_cast<int>(impl_->width), static_cast<int>(impl_->height),
                  impl_->resources.screen, impl_->bounds.left, impl_->bounds.top, SRCCOPY | CAPTUREBLT)) {
        return CaptureError("showcase desktop pixels could not be captured");
    }
    if (includeSystemCursor) {
        CURSORINFO cursor{sizeof(cursor)};
        if (::GetCursorInfo(&cursor) && cursor.flags == CURSOR_SHOWING && cursor.hCursor) {
            ICONINFO information{};
            if (::GetIconInfo(cursor.hCursor, &information)) {
                const int x = cursor.ptScreenPos.x - impl_->bounds.left - static_cast<int>(information.xHotspot);
                const int y = cursor.ptScreenPos.y - impl_->bounds.top - static_cast<int>(information.yHotspot);
                (void)::DrawIconEx(impl_->resources.memory, x, y, cursor.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
                if (information.hbmMask) (void)::DeleteObject(information.hbmMask);
                if (information.hbmColor) (void)::DeleteObject(information.hbmColor);
            }
        }
    }
    (void)::GdiFlush();
    const auto* bgra = static_cast<const std::uint8_t*>(impl_->dibPixels);
    for (std::size_t offset = 0; offset < impl_->rgba.size(); offset += 4) {
        impl_->rgba[offset] = bgra[offset + 2];
        impl_->rgba[offset + 1] = bgra[offset + 1];
        impl_->rgba[offset + 2] = bgra[offset];
        impl_->rgba[offset + 3] = 255;
    }
    return RgbaFrameView{impl_->width, impl_->height, static_cast<std::size_t>(impl_->width) * 4U, impl_->rgba};
}

RECT GdiDesktopCaptureSession::Bounds() const noexcept { return impl_ ? impl_->bounds : RECT{}; }

}  // namespace aether::showcase
