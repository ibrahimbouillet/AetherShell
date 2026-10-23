// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/DxgiDesktopCapture.h"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace aether::showcase {
namespace {

using Microsoft::WRL::ComPtr;
constexpr std::uint32_t kMaximumDimension = 16'384;
// The caller already schedules captures. Waiting another refresh interval here halves cadence
// on unchanged desktop images; reuse authentic cached pixels and compose the current cursor.
constexpr UINT kAcquireTimeoutMilliseconds = 0;

Error PlatformError(const char* message, HRESULT status) noexcept {
    return MakeError(ErrorCode::Platform, message, static_cast<std::int64_t>(status));
}

bool SameBounds(const RECT& first, const RECT& second) noexcept {
    return first.left == second.left && first.top == second.top && first.right == second.right &&
           first.bottom == second.bottom;
}

struct CursorSurface final {
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    void* pixels = nullptr;
    ~CursorSurface() {
        if (memory && previousBitmap && previousBitmap != HGDI_ERROR)
            (void)::SelectObject(memory, previousBitmap);
        if (bitmap) (void)::DeleteObject(bitmap);
        if (memory) (void)::DeleteDC(memory);
    }
};

struct FrameLease final {
    IDXGIOutputDuplication* duplication = nullptr;
    ~FrameLease() { if (duplication) (void)duplication->ReleaseFrame(); }
    [[nodiscard]] HRESULT Release() noexcept {
        auto* value = std::exchange(duplication, nullptr);
        return value ? value->ReleaseFrame() : S_OK;
    }
};

Result<void> DrawCurrentCursor(CursorSurface& surface, const RECT& bounds) noexcept {
    CURSORINFO cursor{sizeof(cursor)};
    if (!::GetCursorInfo(&cursor))
        return MakeError(ErrorCode::Platform, "showcase cursor visibility is unavailable", ::GetLastError());
    if ((cursor.flags & CURSOR_SHOWING) == 0 || !cursor.hCursor) return Ok();
    ICONINFO information{};
    if (!::GetIconInfo(cursor.hCursor, &information))
        return MakeError(ErrorCode::Platform, "showcase cursor shape is unavailable", ::GetLastError());
    const int x = cursor.ptScreenPos.x - bounds.left - static_cast<int>(information.xHotspot);
    const int y = cursor.ptScreenPos.y - bounds.top - static_cast<int>(information.yHotspot);
    const BOOL drawn = ::DrawIconEx(surface.memory, x, y, cursor.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    const DWORD drawError = drawn ? ERROR_SUCCESS : ::GetLastError();
    if (information.hbmMask) (void)::DeleteObject(information.hbmMask);
    if (information.hbmColor) (void)::DeleteObject(information.hbmColor);
    if (!drawn) return MakeError(ErrorCode::Platform, "showcase cursor could not be composed", drawError);
    if (!::GdiFlush())
        return MakeError(ErrorCode::Platform, "showcase cursor composition did not complete", ::GetLastError());
    return Ok();
}

}  // namespace

struct DxgiDesktopCaptureSession::Impl final {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging;
    CursorSurface cursor;
    RECT bounds{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> desktopBgra;
    std::vector<std::uint8_t> rgba;
    bool hasDesktop = false;
    bool updatedLastCapture = false;
    bool separateCursorVisible = false;
    std::int64_t lastPresentCounter = 0;
    std::uint64_t acquiredFrames = 0;
    std::uint64_t desktopFrames = 0;
};

DxgiDesktopCaptureSession::DxgiDesktopCaptureSession() noexcept = default;
DxgiDesktopCaptureSession::DxgiDesktopCaptureSession(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
DxgiDesktopCaptureSession::~DxgiDesktopCaptureSession() = default;
DxgiDesktopCaptureSession::DxgiDesktopCaptureSession(DxgiDesktopCaptureSession&& other) noexcept = default;
DxgiDesktopCaptureSession& DxgiDesktopCaptureSession::operator=(DxgiDesktopCaptureSession&& other) noexcept = default;

Result<DxgiDesktopCaptureSession> DxgiDesktopCaptureSession::Create(const RECT& boundsPixels) noexcept {
    const auto width = static_cast<std::int64_t>(boundsPixels.right) - boundsPixels.left;
    const auto height = static_cast<std::int64_t>(boundsPixels.bottom) - boundsPixels.top;
    if (width <= 0 || height <= 0 || width > kMaximumDimension || height > kMaximumDimension)
        return MakeError(ErrorCode::InvalidArgument, "showcase capture rectangle is invalid or too large");
    const auto pixelCount = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U)
        return MakeError(ErrorCode::InvalidArgument, "showcase capture buffer exceeds supported size");
    try {
        auto impl = std::make_unique<Impl>();
        impl->bounds = boundsPixels;
        impl->width = static_cast<std::uint32_t>(width);
        impl->height = static_cast<std::uint32_t>(height);
        ComPtr<IDXGIFactory1> factory;
        HRESULT status = ::CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(status)) return PlatformError("showcase display enumeration is unavailable", status);

        ComPtr<IDXGIAdapter1> selectedAdapter;
        ComPtr<IDXGIOutput> selectedOutput;
        for (UINT adapterIndex = 0; !selectedOutput; ++adapterIndex) {
            ComPtr<IDXGIAdapter1> adapter;
            status = factory->EnumAdapters1(adapterIndex, &adapter);
            if (status == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(status)) return PlatformError("showcase graphics adapter enumeration failed", status);
            DXGI_ADAPTER_DESC1 adapterDescription{};
            status = adapter->GetDesc1(&adapterDescription);
            if (FAILED(status)) return PlatformError("showcase graphics adapter capabilities are unavailable", status);
            if ((adapterDescription.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
            for (UINT outputIndex = 0; ; ++outputIndex) {
                ComPtr<IDXGIOutput> output;
                status = adapter->EnumOutputs(outputIndex, &output);
                if (status == DXGI_ERROR_NOT_FOUND) break;
                if (FAILED(status)) return PlatformError("showcase display enumeration failed", status);
                DXGI_OUTPUT_DESC description{};
                status = output->GetDesc(&description);
                if (FAILED(status)) return PlatformError("showcase display geometry is unavailable", status);
                if (!description.AttachedToDesktop || !SameBounds(description.DesktopCoordinates, boundsPixels)) continue;
                if (description.Rotation != DXGI_MODE_ROTATION_IDENTITY)
                    return MakeError(ErrorCode::Unsupported, "showcase accelerated capture requires an unrotated display");
                selectedAdapter = adapter;
                selectedOutput = output;
                break;
            }
        }
        if (!selectedOutput)
            return MakeError(ErrorCode::Unsupported, "showcase accelerated capture requires one complete hardware display");

        ComPtr<IDXGIOutput6> output6;
        status = selectedOutput.As(&output6);
        if (FAILED(status)) return MakeError(ErrorCode::Unsupported, "showcase could not verify the display SDR color mode");
        DXGI_OUTPUT_DESC1 colorDescription{};
        status = output6->GetDesc1(&colorDescription);
        if (FAILED(status)) return PlatformError("showcase display color information is unavailable", status);
        if (colorDescription.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 &&
            colorDescription.ColorSpace != DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709)
            return MakeError(ErrorCode::Unsupported, "showcase accelerated capture supports SDR BT.709 displays only");

        constexpr std::array<D3D_FEATURE_LEVEL, 4> featureLevels{
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL actualFeatureLevel{};
        status = ::D3D11CreateDevice(selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels.data(),
                                     static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                                     &impl->device, &actualFeatureLevel, &impl->context);
        if (FAILED(status)) return PlatformError("showcase hardware capture device could not be created", status);
        ComPtr<IDXGIOutput1> output1;
        status = selectedOutput.As(&output1);
        if (SUCCEEDED(status)) status = output1->DuplicateOutput(impl->device.Get(), &impl->duplication);
        if (FAILED(status)) return PlatformError("showcase hardware desktop duplication is unavailable", status);
        DXGI_OUTDUPL_DESC duplicationDescription{};
        impl->duplication->GetDesc(&duplicationDescription);
        if (duplicationDescription.ModeDesc.Width != impl->width ||
            duplicationDescription.ModeDesc.Height != impl->height ||
            duplicationDescription.ModeDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
            duplicationDescription.Rotation != DXGI_MODE_ROTATION_IDENTITY)
            return MakeError(ErrorCode::Unsupported, "showcase duplicated display format is unsupported");

        D3D11_TEXTURE2D_DESC stagingDescription{};
        stagingDescription.Width = impl->width;
        stagingDescription.Height = impl->height;
        stagingDescription.MipLevels = 1;
        stagingDescription.ArraySize = 1;
        stagingDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDescription.SampleDesc.Count = 1;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        status = impl->device->CreateTexture2D(&stagingDescription, nullptr, &impl->staging);
        if (FAILED(status)) return PlatformError("showcase capture readback buffer could not be created", status);

        impl->desktopBgra.resize(static_cast<std::size_t>(pixelCount) * 4U);
        impl->rgba.resize(static_cast<std::size_t>(pixelCount) * 4U);
        impl->cursor.memory = ::CreateCompatibleDC(nullptr);
        if (!impl->cursor.memory)
            return MakeError(ErrorCode::Platform, "showcase cursor surface could not be created", ::GetLastError());
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = static_cast<LONG>(impl->width);
        bitmap.bmiHeader.biHeight = -static_cast<LONG>(impl->height);
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        impl->cursor.bitmap = ::CreateDIBSection(impl->cursor.memory, &bitmap, DIB_RGB_COLORS,
                                                &impl->cursor.pixels, nullptr, 0);
        if (!impl->cursor.bitmap || !impl->cursor.pixels)
            return MakeError(ErrorCode::Platform, "showcase cursor buffer could not be allocated", ::GetLastError());
        impl->cursor.previousBitmap = ::SelectObject(impl->cursor.memory, impl->cursor.bitmap);
        if (!impl->cursor.previousBitmap || impl->cursor.previousBitmap == HGDI_ERROR)
            return MakeError(ErrorCode::Platform, "showcase cursor buffer could not be selected", ::GetLastError());
        return DxgiDesktopCaptureSession(std::move(impl));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase accelerated capture allocation failed");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase accelerated capture could not be initialized");
    }
}

Result<RgbaFrameView> DxgiDesktopCaptureSession::Capture(bool includeSystemCursor) noexcept {
    if (!impl_ || !impl_->duplication)
        return MakeError(ErrorCode::InvalidState, "showcase accelerated capture session is unavailable");
    impl_->updatedLastCapture = false;
    DXGI_OUTDUPL_FRAME_INFO information{};
    ComPtr<IDXGIResource> resource;
    const HRESULT acquired = impl_->duplication->AcquireNextFrame(kAcquireTimeoutMilliseconds, &information, &resource);
    if (acquired == DXGI_ERROR_WAIT_TIMEOUT) {
        if (!impl_->hasDesktop)
            return MakeError(ErrorCode::Timeout, "showcase accelerated capture has not received an initial desktop frame");
    } else {
        if (acquired == DXGI_ERROR_ACCESS_LOST)
            return MakeError(ErrorCode::Unavailable, "showcase accelerated capture lost the display; stop and restore the session");
        if (FAILED(acquired)) return PlatformError("showcase accelerated desktop acquisition failed", acquired);
        FrameLease lease{impl_->duplication.Get()};
        ++impl_->acquiredFrames;
        if (information.ProtectedContentMaskedOut)
            return MakeError(ErrorCode::Unsupported, "showcase capture cannot include masked protected content");
        if (information.LastMouseUpdateTime.QuadPart != 0)
            impl_->separateCursorVisible = information.PointerPosition.Visible != FALSE;
        if (information.LastPresentTime.QuadPart != 0 || !impl_->hasDesktop) {
            ComPtr<ID3D11Texture2D> desktop;
            HRESULT status = resource.As(&desktop);
            if (FAILED(status)) return PlatformError("showcase duplicated desktop surface is unavailable", status);
            D3D11_TEXTURE2D_DESC description{};
            desktop->GetDesc(&description);
            if (description.Width != impl_->width || description.Height != impl_->height ||
                description.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
                return MakeError(ErrorCode::Unavailable, "showcase display geometry changed during capture");
            impl_->context->CopyResource(impl_->staging.Get(), desktop.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            status = impl_->context->Map(impl_->staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(status)) return PlatformError("showcase desktop frame could not be read back", status);
            const auto rowBytes = static_cast<std::size_t>(impl_->width) * 4U;
            const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
            for (std::uint32_t row = 0; row < impl_->height; ++row) {
                std::memcpy(impl_->desktopBgra.data() + static_cast<std::size_t>(row) * rowBytes,
                            source + static_cast<std::size_t>(row) * mapped.RowPitch, rowBytes);
            }
            impl_->context->Unmap(impl_->staging.Get(), 0);
            impl_->hasDesktop = true;
            impl_->updatedLastCapture = true;
            impl_->lastPresentCounter = information.LastPresentTime.QuadPart;
            ++impl_->desktopFrames;
        }
        const HRESULT released = lease.Release();
        if (FAILED(released)) return PlatformError("showcase desktop frame could not be released", released);
    }

    const auto* bgra = impl_->desktopBgra.data();
    // A real take on this display omitted the pointer despite CURSOR_SHOWING. Compose the actual
    // current Windows cursor explicitly (shape, hotspot and position from GetCursorInfo), rather
    // than treating a missing DXGI pointer notification as proof that its pixels are baked in.
    // This path is reviewed for a single visible cursor in the complete exported take.
    if (includeSystemCursor) {
        std::memcpy(impl_->cursor.pixels, bgra, impl_->desktopBgra.size());
        if (auto cursor = DrawCurrentCursor(impl_->cursor, impl_->bounds); !cursor) return cursor.error();
        bgra = static_cast<const std::uint8_t*>(impl_->cursor.pixels);
    }
    for (std::size_t offset = 0; offset < impl_->rgba.size(); offset += 4U) {
        impl_->rgba[offset] = bgra[offset + 2U];
        impl_->rgba[offset + 1U] = bgra[offset + 1U];
        impl_->rgba[offset + 2U] = bgra[offset];
        impl_->rgba[offset + 3U] = 255;
    }
    return RgbaFrameView{impl_->width, impl_->height, static_cast<std::size_t>(impl_->width) * 4U, impl_->rgba};
}

RECT DxgiDesktopCaptureSession::Bounds() const noexcept { return impl_ ? impl_->bounds : RECT{}; }
bool DxgiDesktopCaptureSession::UpdatedLastCapture() const noexcept { return impl_ && impl_->updatedLastCapture; }
std::int64_t DxgiDesktopCaptureSession::LastPresentCounter() const noexcept { return impl_ ? impl_->lastPresentCounter : 0; }
std::uint64_t DxgiDesktopCaptureSession::AcquiredFrameCount() const noexcept { return impl_ ? impl_->acquiredFrames : 0; }
std::uint64_t DxgiDesktopCaptureSession::DesktopFrameCount() const noexcept { return impl_ ? impl_->desktopFrames : 0; }

}  // namespace aether::showcase
