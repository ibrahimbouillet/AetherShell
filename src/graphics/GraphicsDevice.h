// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Process-wide rendering resources, owned by the UI thread:
//   Direct3D 11 device -> Direct2D device -> Windows.UI.Composition graphics device,
//   plus the DirectWrite and WIC factories and the compositor itself.
//
// Device loss (driver update, GPU reset, TDR) is reported by a failed draw.
// HandleDeviceLost() recreates the devices, rebinds the composition graphics
// device and raises DeviceReplaced so windows can redraw and caches can drop
// device-dependent objects.
#pragma once

#include "core/Result.h"
#include "core/Signal.h"
#include "graphics/WinRT.h"

#include <cstdint>

namespace aether::graphics {

struct GraphicsCapabilities {
    // Blurred desktop backdrop behind our windows (HostBackdropBrush + DWMWA_USE_HOSTBACKDROPBRUSH).
    // Documented for Win32 windows on Windows 11 (build 22000+) only.
    bool hostBackdrop = false;
    // False when Direct3D fell back to the WARP software rasterizer.
    bool hardwareAcceleration = false;
};

class GraphicsDevice {
public:
    GraphicsDevice() = default;
    ~GraphicsDevice();
    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    // UI thread, which must be a COM STA. Creates the thread's DispatcherQueue (needed by the
    // compositor), the compositor and the Direct3D/Direct2D devices.
    [[nodiscard]] Result<void> Initialize();
    void Shutdown() noexcept;

    [[nodiscard]] const wuc::Compositor& Compositor() const noexcept { return compositor_; }
    [[nodiscard]] const wuc::CompositionGraphicsDevice& CompositionDevice() const noexcept { return compositionDevice_; }
    [[nodiscard]] ID2D1Factory1* D2DFactory() const noexcept { return d2dFactory_.get(); }
    [[nodiscard]] IDWriteFactory* DWriteFactory() const noexcept { return dwriteFactory_.get(); }
    [[nodiscard]] IWICImagingFactory* WicFactory() const noexcept { return wicFactory_.get(); }
    [[nodiscard]] const GraphicsCapabilities& Capabilities() const noexcept { return capabilities_; }

    // Recreates the Direct3D/Direct2D devices after a device-lost error and raises DeviceReplaced.
    [[nodiscard]] Result<void> HandleDeviceLost();
    [[nodiscard]] std::uint64_t Generation() const noexcept { return generation_; }

    // Raised on the UI thread after the rendering device changed. Every device-dependent resource
    // (bitmaps, brushes, effects) must be recreated and every surface redrawn.
    Signal<> DeviceReplaced;

    [[nodiscard]] static bool IsDeviceLostError(HRESULT hr) noexcept;

private:
    [[nodiscard]] Result<void> CreateDevices();

    winrt::Windows::System::DispatcherQueueController dispatcherQueueController_{nullptr};
    wuc::Compositor compositor_{nullptr};
    wuc::CompositionGraphicsDevice compositionDevice_{nullptr};
    winrt::com_ptr<ID3D11Device> d3dDevice_;
    winrt::com_ptr<ID2D1Factory1> d2dFactory_;
    winrt::com_ptr<ID2D1Device> d2dDevice_;
    winrt::com_ptr<IDWriteFactory> dwriteFactory_;
    winrt::com_ptr<IWICImagingFactory> wicFactory_;
    GraphicsCapabilities capabilities_;
    std::uint64_t generation_ = 0;
};

}  // namespace aether::graphics
