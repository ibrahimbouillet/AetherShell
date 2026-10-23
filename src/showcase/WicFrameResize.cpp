// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/WicFrameResize.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <limits>

namespace aether::showcase {
namespace {
using Microsoft::WRL::ComPtr;

bool ValidBuffer(std::uint32_t width, std::uint32_t height, std::size_t stride,
                 std::size_t bufferBytes) noexcept {
    return width != 0 && height != 0 && width <= 16'384 && height <= 16'384 &&
        stride >= static_cast<std::size_t>(width) * 4U &&
        stride <= std::numeric_limits<UINT>::max() / height &&
        bufferBytes >= stride * height;
}
}  // namespace

Result<void> ResizeRgbaFrame(const RgbaFrameView& source, std::span<std::uint8_t> destination,
    std::uint32_t outputWidth, std::uint32_t outputHeight, std::size_t outputRowStrideBytes) noexcept {
    if (!ValidBuffer(source.width, source.height, source.rowStrideBytes, source.pixels.size()) ||
        !ValidBuffer(outputWidth, outputHeight, outputRowStrideBytes, destination.size()))
        return MakeError(ErrorCode::InvalidArgument, "RGBA resize dimensions or buffers are invalid");
    const auto inputBegin = reinterpret_cast<std::uintptr_t>(source.pixels.data());
    const auto outputBegin = reinterpret_cast<std::uintptr_t>(destination.data());
    const auto inputBytes = source.rowStrideBytes * source.height;
    const auto outputBytes = outputRowStrideBytes * outputHeight;
    if (inputBegin > std::numeric_limits<std::uintptr_t>::max() - inputBytes ||
        outputBegin > std::numeric_limits<std::uintptr_t>::max() - outputBytes ||
        (inputBegin < outputBegin + outputBytes && outputBegin < inputBegin + inputBytes))
        return MakeError(ErrorCode::InvalidArgument, "RGBA resize buffers must not overlap");
    try {
        ComPtr<IWICImagingFactory> factory;
        HRESULT status = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        ComPtr<IWICBitmap> bitmap;
        if (SUCCEEDED(status)) status = factory->CreateBitmapFromMemory(source.width, source.height,
            GUID_WICPixelFormat32bppRGBA, static_cast<UINT>(source.rowStrideBytes),
            static_cast<UINT>(inputBytes), const_cast<BYTE*>(source.pixels.data()), &bitmap);
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(status)) status = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(status)) status = scaler->Initialize(bitmap.Get(), outputWidth, outputHeight,
            WICBitmapInterpolationModeHighQualityCubic);
        ComPtr<IWICFormatConverter> rgba;
        if (SUCCEEDED(status)) status = factory->CreateFormatConverter(&rgba);
        if (SUCCEEDED(status)) status = rgba->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(status)) status = rgba->CopyPixels(nullptr, static_cast<UINT>(outputRowStrideBytes),
            static_cast<UINT>(outputBytes), destination.data());
        if (FAILED(status)) return MakeError(ErrorCode::Platform, "native RGBA resizing failed", status);
        return {};
    } catch (...) { return MakeError(ErrorCode::Internal, "native RGBA resize allocation failed"); }
}
}  // namespace aether::showcase
