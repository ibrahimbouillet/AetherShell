// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/GraphicsDevice.h"

#include "diagnostics/Logger.h"
#include "platform/windows/OsVersion.h"
#include "platform/windows/Win32Error.h"

#include <iterator>

namespace aether::graphics {
namespace {

Error FromWinrt(const winrt::hresult_error& error, std::string_view context) {
    return platform::ErrorFromHResult(error.code(), context);
}

}  // namespace

GraphicsDevice::~GraphicsDevice() { Shutdown(); }

bool GraphicsDevice::IsDeviceLostError(HRESULT hr) noexcept {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR || hr == D2DERR_RECREATE_TARGET;
}

Result<void> GraphicsDevice::Initialize() {
    if (compositor_) return Ok();
    try {
        if (!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
            DispatcherQueueOptions options{sizeof(DispatcherQueueOptions), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
            winrt::check_hresult(::CreateDispatcherQueueController(
                options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
                             winrt::put_abi(dispatcherQueueController_))));
        }
        compositor_ = wuc::Compositor();

        winrt::check_hresult(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                                 nullptr, d2dFactory_.put_void()));
        winrt::check_hresult(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                   reinterpret_cast<IUnknown**>(dwriteFactory_.put())));
        wicFactory_ = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    } catch (const winrt::hresult_error& error) {
        return FromWinrt(error, "graphics initialization");
    }

    if (auto devices = CreateDevices(); !devices) return devices.error();

    capabilities_.hostBackdrop = platform::IsWindows11OrGreater();
    AETHER_LOG_INFO("graphics", "graphics ready (hardware={}, hostBackdrop={})",
                    capabilities_.hardwareAcceleration, capabilities_.hostBackdrop);
    return Ok();
}

Result<void> GraphicsDevice::CreateDevices() {
    try {
        static const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                                    D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
                                                    D3D_FEATURE_LEVEL_9_1};
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        winrt::com_ptr<ID3D11Device> device;
        HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kLevels,
                                         static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, device.put(),
                                         nullptr, nullptr);
        capabilities_.hardwareAcceleration = SUCCEEDED(hr);
        if (FAILED(hr)) {
            AETHER_LOG_WARN("graphics", "hardware Direct3D device unavailable (0x{:08X}); using WARP",
                            static_cast<unsigned>(hr));
            winrt::check_hresult(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, kLevels,
                                                     static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION,
                                                     device.put(), nullptr, nullptr));
        }
        const auto dxgiDevice = device.as<IDXGIDevice>();
        winrt::com_ptr<ID2D1Device> d2dDevice;
        winrt::check_hresult(d2dFactory_->CreateDevice(dxgiDevice.get(), d2dDevice.put()));

        if (!compositionDevice_) {
            const auto interop = compositor_.as<abi_wuc::ICompositorInterop>();
            winrt::check_hresult(interop->CreateGraphicsDevice(
                d2dDevice.get(), reinterpret_cast<abi_wuc::ICompositionGraphicsDevice**>(
                                     winrt::put_abi(compositionDevice_))));
        } else {
            const auto interop = compositionDevice_.as<abi_wuc::ICompositionGraphicsDeviceInterop>();
            winrt::check_hresult(interop->SetRenderingDevice(d2dDevice.get()));
        }
        d3dDevice_ = std::move(device);
        d2dDevice_ = std::move(d2dDevice);
        ++generation_;
        return Ok();
    } catch (const winrt::hresult_error& error) {
        return FromWinrt(error, "Direct3D/Direct2D device creation");
    }
}

Result<void> GraphicsDevice::HandleDeviceLost() {
    if (d3dDevice_) {
        const HRESULT reason = d3dDevice_->GetDeviceRemovedReason();
        AETHER_LOG_WARN("graphics", "rendering device lost (reason 0x{:08X}); recreating",
                        static_cast<unsigned>(reason));
    }
    if (auto result = CreateDevices(); !result) return result.error();
    DeviceReplaced.Emit();
    return Ok();
}

void GraphicsDevice::Shutdown() noexcept {
    compositionDevice_ = nullptr;
    compositor_ = nullptr;
    d2dDevice_ = nullptr;
    d3dDevice_ = nullptr;
    wicFactory_ = nullptr;
    dwriteFactory_ = nullptr;
    d2dFactory_ = nullptr;
    if (dispatcherQueueController_) {
        // Completes asynchronously; the queue is drained by the message loop that is still running or
        // simply released at process exit.
        try {
            dispatcherQueueController_.ShutdownQueueAsync();
        } catch (...) {
        }
        dispatcherQueueController_ = nullptr;
    }
}

}  // namespace aether::graphics
