// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/GifWriter.h"
#include "showcase/WicFrameResize.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::showcase;

namespace {
struct ResizeComScope final {
    HRESULT status = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ResizeComScope() { if (SUCCEEDED(status)) ::CoUninitialize(); }
};

struct ResizeGifFile final {
    std::wstring path;
    ResizeGifFile() {
        wchar_t directory[MAX_PATH]{};
        wchar_t reserved[MAX_PATH]{};
        const DWORD length = ::GetTempPathW(MAX_PATH, directory);
        if (length > 0 && length < MAX_PATH && ::GetTempFileNameW(directory, L"asr", 0, reserved)) {
            (void)::DeleteFileW(reserved);
            path = reserved;
            path += L".gif";
        }
    }
    ~ResizeGifFile() { if (!path.empty()) (void)::DeleteFileW(path.c_str()); }
};

std::vector<std::uint8_t> QuadrantPixels() {
    std::vector<std::uint8_t> result(64U * 48U * 4U);
    constexpr std::array<std::array<std::uint8_t, 4>, 4> colors{{
        {240, 20, 10, 255}, {10, 20, 240, 255}, {20, 240, 10, 255}, {240, 200, 20, 255}}};
    for (std::size_t y = 0; y < 48; ++y)
        for (std::size_t x = 0; x < 64; ++x) {
            const auto& color = colors[(y >= 24 ? 2U : 0U) + (x >= 32 ? 1U : 0U)];
            for (std::size_t channel = 0; channel < 4; ++channel)
                result[(y * 64U + x) * 4U + channel] = color[channel];
        }
    return result;
}
}  // namespace

AETHER_TEST("showcase WIC resize: explicit RGBA output preserves all four color quadrants") {
    ResizeComScope com;
    REQUIRE(SUCCEEDED(com.status) || com.status == RPC_E_CHANGED_MODE);
    const auto pixels = QuadrantPixels();
    constexpr std::array<std::array<std::uint32_t, 2>, 3> sizes{{{32, 24}, {48, 16}, {128, 96}}};
    for (const auto size : sizes) {
        const auto width = size[0], height = size[1];
        const std::size_t stride = static_cast<std::size_t>(width) * 4U + 16U;
        std::vector<std::uint8_t> resized(stride * height, 0xCC);
        REQUIRE(ResizeRgbaFrame({64, 48, 256, pixels}, resized, width, height, stride).ok());
        const auto at = [&](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return resized[static_cast<std::size_t>(y) * stride + x * 4U + channel];
        };
        // Interior samples avoid the interpolation kernel's deliberate boundary blending.
        CHECK(at(width / 4, height / 4, 0) > 200);
        CHECK(at(width / 4, height / 4, 2) < 50);
        CHECK(at(3 * width / 4, height / 4, 0) < 50);
        CHECK(at(3 * width / 4, height / 4, 2) > 200);
        CHECK(at(width / 4, 3 * height / 4, 1) > 200);
        CHECK(at(width / 4, 3 * height / 4, 0) < 50);
        CHECK(at(3 * width / 4, 3 * height / 4, 0) > 200);
        CHECK(at(3 * width / 4, 3 * height / 4, 1) > 150);
        CHECK(at(3 * width / 4, 3 * height / 4, 2) < 50);
        CHECK_EQ(at(width / 4, height / 4, 3), 255);
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::size_t x = static_cast<std::size_t>(width) * 4U; x < stride; ++x)
                CHECK_EQ(resized[static_cast<std::size_t>(y) * stride + x], 0xCC);
    }
}

AETHER_TEST("showcase WIC resize: rejects invalid geometry short storage and overlapping buffers") {
    const auto source = QuadrantPixels();
    std::vector<std::uint8_t> destination(32U * 24U * 4U, 0xCC);
    CHECK_FALSE(ResizeRgbaFrame({64, 48, 256, source}, destination, 0, 24, 128).ok());
    CHECK_FALSE(ResizeRgbaFrame({64, 48, 256, source}, destination, 16'385, 24, 128).ok());
    CHECK_FALSE(ResizeRgbaFrame({64, 48, 256, source}, destination, 32, 24, 127).ok());
    CHECK_FALSE(ResizeRgbaFrame({64, 48, 255, source}, destination, 32, 24, 128).ok());
    CHECK_FALSE(ResizeRgbaFrame({64, 48, 256, std::span<const std::uint8_t>(source).first(20)},
        destination, 32, 24, 128).ok());
    CHECK_FALSE(ResizeRgbaFrame({64, 48, 256, source}, std::span<std::uint8_t>(destination).first(20),
        32, 24, 128).ok());
    CHECK_FALSE(ResizeRgbaFrame({64, 48, 256, source}, destination, 32, 24,
        std::numeric_limits<std::size_t>::max()).ok());
    auto overlap = QuadrantPixels();
    CHECK_FALSE(ResizeRgbaFrame({64, 48, 256, overlap}, overlap, 32, 24, 128).ok());
    for (const auto value : destination) CHECK_EQ(value, 0xCC);
}

AETHER_TEST("showcase WIC resize: four quadrants keep their channels through GIF encode and decode") {
    using Microsoft::WRL::ComPtr;
    ResizeComScope com;
    REQUIRE(SUCCEEDED(com.status) || com.status == RPC_E_CHANGED_MODE);
    const auto source = QuadrantPixels();
    std::vector<std::uint8_t> resized(32U * 24U * 4U);
    REQUIRE(ResizeRgbaFrame({64, 48, 256, source}, resized, 32, 24, 128).ok());
    ResizeGifFile file;
    REQUIRE_FALSE(file.path.empty());
    auto gif = GifWriter::Create({file.path, 32, 24, 5});
    REQUIRE(gif.ok());
    REQUIRE(gif->AppendFrame({32, 24, 128, resized}).ok());
    REQUIRE(gif->Finalize().ok());
    ComPtr<IWICImagingFactory> factory;
    REQUIRE(SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))));
    ComPtr<IWICBitmapDecoder> decoder;
    REQUIRE(SUCCEEDED(factory->CreateDecoderFromFilename(file.path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)));
    ComPtr<IWICBitmapFrameDecode> frame;
    REQUIRE(SUCCEEDED(decoder->GetFrame(0, &frame)));
    ComPtr<IWICFormatConverter> rgba;
    REQUIRE(SUCCEEDED(factory->CreateFormatConverter(&rgba)));
    REQUIRE(SUCCEEDED(rgba->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)));
    std::vector<std::uint8_t> decoded(resized.size());
    REQUIRE(SUCCEEDED(rgba->CopyPixels(nullptr, 128, static_cast<UINT>(decoded.size()), decoded.data())));
    const auto at = [&](std::size_t x, std::size_t y, std::size_t channel) {
        return decoded[(y * 32U + x) * 4U + channel];
    };
    CHECK(at(8, 6, 0) > 200); CHECK(at(8, 6, 2) < 50);
    CHECK(at(24, 6, 0) < 50); CHECK(at(24, 6, 2) > 200);
    CHECK(at(8, 18, 1) > 200); CHECK(at(8, 18, 0) < 50);
    CHECK(at(24, 18, 0) > 200); CHECK(at(24, 18, 1) > 150); CHECK(at(24, 18, 2) < 50);
}
