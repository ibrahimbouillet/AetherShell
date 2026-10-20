// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Headless Direct2D canvas for painter tests: a WARP (software) Direct3D device, a Direct2D device
// context drawing into an offscreen bitmap, and a CPU readback. No window, no swap chain, no screen.
#pragma once

#include "graphics/WinRT.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace aether::test {

struct CanvasPixel {
    int b = 0;
    int g = 0;
    int r = 0;
    int a = 0;  // premultiplied BGRA
};

class D2DCanvas {
public:
    [[nodiscard]] bool Create(UINT width, UINT height, float dpi = 96.0f) {
        width_ = width;
        height_ = height;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        if (FAILED(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                       static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, d3d_.put(), nullptr,
                                       nullptr))) {
            return false;
        }
        const auto dxgi = d3d_.try_as<IDXGIDevice>();
        if (!dxgi) return false;
        D2D1_FACTORY_OPTIONS options{};
        if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                                       factory_.put_void())) ||
            FAILED(factory_->CreateDevice(dxgi.get(), device_.put())) ||
            FAILED(device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context_.put()))) {
            return false;
        }
        const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        if (FAILED(context_->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                                          D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, dpi, dpi),
                                          target_.put())) ||
            FAILED(context_->CreateBitmap(
                D2D1::SizeU(width, height), nullptr, 0,
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format, dpi, dpi),
                readback_.put()))) {
            return false;
        }
        context_->SetTarget(target_.get());
        context_->SetDpi(dpi, dpi);
        return true;
    }

    [[nodiscard]] ID2D1DeviceContext* Context() const noexcept { return context_.get(); }

    void Begin() {
        context_->BeginDraw();
        context_->SetTransform(D2D1::Matrix3x2F::Identity());
        context_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    }

    // Ends drawing and copies the target into `pixels_`.
    [[nodiscard]] bool End() {
        if (FAILED(context_->EndDraw())) return false;
        const D2D1_POINT_2U origin{0, 0};
        const D2D1_RECT_U all{0, 0, width_, height_};
        if (FAILED(readback_->CopyFromBitmap(&origin, target_.get(), &all))) return false;
        D2D1_MAPPED_RECT mapped{};
        if (FAILED(readback_->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;
        pixels_.assign(static_cast<std::size_t>(width_) * height_ * 4u, 0);
        for (UINT row = 0; row < height_; ++row) {
            std::memcpy(pixels_.data() + static_cast<std::size_t>(row) * width_ * 4u, mapped.bits + static_cast<std::size_t>(row) * mapped.pitch,
                        static_cast<std::size_t>(width_) * 4u);
        }
        readback_->Unmap();
        return true;
    }

    [[nodiscard]] CanvasPixel At(UINT x, UINT y) const {
        if (x >= width_ || y >= height_ || pixels_.empty()) return {};
        const auto* p = pixels_.data() + (static_cast<std::size_t>(y) * width_ + x) * 4u;
        return CanvasPixel{p[0], p[1], p[2], p[3]};
    }

private:
    UINT width_ = 0;
    UINT height_ = 0;
    winrt::com_ptr<ID3D11Device> d3d_;
    winrt::com_ptr<ID2D1Factory1> factory_;
    winrt::com_ptr<ID2D1Device> device_;
    winrt::com_ptr<ID2D1DeviceContext> context_;
    winrt::com_ptr<ID2D1Bitmap1> target_;
    winrt::com_ptr<ID2D1Bitmap1> readback_;
    std::vector<std::uint8_t> pixels_;
};

inline bool Near(const CanvasPixel& actual, int r, int g, int b, int a, int tolerance) {
    const auto close = [tolerance](int x, int y) { return x - y <= tolerance && y - x <= tolerance; };
    return close(actual.r, r) && close(actual.g, g) && close(actual.b, b) && close(actual.a, a);
}

}  // namespace aether::test
