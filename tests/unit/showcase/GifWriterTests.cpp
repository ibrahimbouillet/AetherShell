// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/GifWriter.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace aether;
using namespace aether::showcase;
using Microsoft::WRL::ComPtr;

namespace {
struct GifComScope final {
    HRESULT status = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~GifComScope() { if (SUCCEEDED(status)) ::CoUninitialize(); }
};

struct GifTestFile final {
    std::wstring path;
    GifTestFile() {
        wchar_t directory[MAX_PATH]{};
        wchar_t reserved[MAX_PATH]{};
        if (::GetTempPathW(MAX_PATH, directory) > 0 && ::GetTempFileNameW(directory, L"ash", 0, reserved)) {
            (void)::DeleteFileW(reserved);
            path = reserved;
            path += L".gif";
        }
    }
    ~GifTestFile() { if (!path.empty()) (void)::DeleteFileW(path.c_str()); }
};

std::vector<std::uint8_t> SyntheticGifPixels() {
    std::vector<std::uint8_t> pixels(32U * 24U * 4U);
    for (std::size_t y = 0; y < 24; ++y) {
        for (std::size_t x = 0; x < 32; ++x) {
            const auto offset = (y * 32 + x) * 4;
            pixels[offset] = static_cast<std::uint8_t>(x * 8);
            pixels[offset + 1] = static_cast<std::uint8_t>(y * 10);
            pixels[offset + 2] = 80;
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}
}  // namespace

AETHER_TEST("showcase GIF: rejects invalid settings and frame geometry") {
    GifComScope com;
    REQUIRE(SUCCEEDED(com.status) || com.status == RPC_E_CHANGED_MODE);
    GifTestFile file;
    REQUIRE(!file.path.empty());
    GifWriterOptions options{file.path, 32, 24, 5};
    auto invalid = options;
    invalid.width = 0;
    CHECK_FALSE(GifWriter::Create(invalid).ok());
    invalid = options;
    invalid.frameDelayCentiseconds = 1;
    CHECK_FALSE(GifWriter::Create(invalid).ok());
    invalid = options;
    invalid.outputFile += L".png";
    CHECK_FALSE(GifWriter::Create(invalid).ok());

    auto created = GifWriter::Create(options);
    REQUIRE(created.ok());
    auto writer = std::move(created).value();
    auto pixels = SyntheticGifPixels();
    CHECK_FALSE(writer.AppendFrame({31, 24, 128, pixels}).ok());
    CHECK_FALSE(writer.AppendFrame({32, 24, 127, pixels}).ok());
    CHECK_FALSE(writer.AppendFrame({32, 24, 128, std::span<const std::uint8_t>(pixels).first(8)}).ok());
    pixels[3] = 0;
    CHECK_FALSE(writer.AppendFrame({32, 24, 128, pixels}).ok());
    CHECK_EQ(writer.FramesWritten(), 0U);
    CHECK_FALSE(writer.Finalize().ok());
    CHECK_EQ(::GetFileAttributesW(file.path.c_str()), INVALID_FILE_ATTRIBUTES);
}

AETHER_TEST("showcase GIF: decodes full frames with stable palette timing and infinite loop") {
    GifComScope com;
    REQUIRE(SUCCEEDED(com.status) || com.status == RPC_E_CHANGED_MODE);
    GifTestFile file;
    REQUIRE(!file.path.empty());
    auto created = GifWriter::Create({file.path, 32, 24, 5});
    REQUIRE(created.ok());
    auto writer = std::move(created).value();
    auto pixels = SyntheticGifPixels();
    REQUIRE(writer.AppendFrame({32, 24, 128, pixels}).ok());
    // A second frame rearranges existing colors, so any palette changes would be unnecessary.
    for (std::size_t y = 0; y < 24; ++y) {
        for (std::size_t x = 0; x < 16; ++x) {
            const auto left = (y * 32 + x) * 4;
            const auto right = (y * 32 + (31 - x)) * 4;
            for (std::size_t channel = 0; channel < 4; ++channel)
                std::swap(pixels[left + channel], pixels[right + channel]);
        }
    }
    REQUIRE(writer.AppendFrame({32, 24, 128, pixels}).ok());
    CHECK_EQ(writer.FramesWritten(), 2U);
    REQUIRE(writer.Finalize().ok());
    CHECK_FALSE(writer.AppendFrame({32, 24, 128, pixels}).ok());
    CHECK_FALSE(writer.Finalize().ok());

    ComPtr<IWICImagingFactory> factory;
    REQUIRE(SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))));
    ComPtr<IWICBitmapDecoder> decoder;
    REQUIRE(SUCCEEDED(factory->CreateDecoderFromFilename(file.path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)));
    UINT count = 0;
    REQUIRE(SUCCEEDED(decoder->GetFrameCount(&count)));
    CHECK_EQ(count, 2U);
    ComPtr<IWICMetadataQueryReader> global;
    REQUIRE(SUCCEEDED(decoder->GetMetadataQueryReader(&global)));
    PROPVARIANT value{};
    REQUIRE(SUCCEEDED(global->GetMetadataByName(L"/appext/application", &value)));
    REQUIRE_EQ(value.vt, static_cast<VARTYPE>(VT_UI1 | VT_VECTOR));
    constexpr std::array<BYTE, 11> application{'N','E','T','S','C','A','P','E','2','.','0'};
    CHECK_EQ(value.caub.cElems, application.size());
    if (value.caub.cElems == application.size())
        for (std::size_t index = 0; index < application.size(); ++index)
            CHECK_EQ(value.caub.pElems[index], application[index]);
    ::PropVariantClear(&value);
    REQUIRE(SUCCEEDED(global->GetMetadataByName(L"/appext/data", &value)));
    REQUIRE_EQ(value.vt, static_cast<VARTYPE>(VT_UI1 | VT_VECTOR));
    REQUIRE(value.caub.cElems >= 4);
    CHECK_EQ(value.caub.pElems[1], 1);
    CHECK_EQ(value.caub.pElems[2], 0);
    CHECK_EQ(value.caub.pElems[3], 0);
    ::PropVariantClear(&value);

    std::array<WICColor, 256> firstColors{};
    UINT firstColorCount = 0;
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IWICBitmapFrameDecode> frame;
        REQUIRE(SUCCEEDED(decoder->GetFrame(index, &frame)));
        UINT width = 0, height = 0;
        REQUIRE(SUCCEEDED(frame->GetSize(&width, &height)));
        CHECK_EQ(width, 32U); CHECK_EQ(height, 24U);
        ComPtr<IWICPalette> palette;
        REQUIRE(SUCCEEDED(factory->CreatePalette(&palette)));
        REQUIRE(SUCCEEDED(frame->CopyPalette(palette.Get())));
        std::array<WICColor, 256> colors{};
        UINT colorCount = 0;
        REQUIRE(SUCCEEDED(palette->GetColors(static_cast<UINT>(colors.size()), colors.data(), &colorCount)));
        if (index == 0) { firstColors = colors; firstColorCount = colorCount; }
        else { CHECK_EQ(colorCount, firstColorCount); CHECK(colors == firstColors); }
        ComPtr<IWICMetadataQueryReader> metadata;
        REQUIRE(SUCCEEDED(frame->GetMetadataQueryReader(&metadata)));
        REQUIRE(SUCCEEDED(metadata->GetMetadataByName(L"/grctlext/Delay", &value)));
        CHECK_EQ(value.vt, VT_UI2); CHECK_EQ(value.uiVal, 5);
        ::PropVariantClear(&value);
        std::vector<BYTE> decoded(32U * 24U);
        CHECK(SUCCEEDED(frame->CopyPixels(nullptr, 32, static_cast<UINT>(decoded.size()), decoded.data())));
    }
}

AETHER_TEST("showcase GIF: never replaces a destination created before finalization") {
    GifComScope com;
    REQUIRE(SUCCEEDED(com.status) || com.status == RPC_E_CHANGED_MODE);
    GifTestFile file;
    REQUIRE(!file.path.empty());
    auto created = GifWriter::Create({file.path, 32, 24, 5});
    REQUIRE(created.ok());
    auto writer = std::move(created).value();
    const auto pixels = SyntheticGifPixels();
    REQUIRE(writer.AppendFrame({32, 24, 128, pixels}).ok());
    HANDLE sentinel = ::CreateFileW(file.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY, nullptr);
    REQUIRE(sentinel != INVALID_HANDLE_VALUE);
    constexpr std::array<BYTE, 4> marker{'A', 'S', 'H', 'G'};
    DWORD written = 0;
    const BOOL wrote = ::WriteFile(sentinel, marker.data(), static_cast<DWORD>(marker.size()), &written, nullptr);
    ::CloseHandle(sentinel);
    REQUIRE(wrote != FALSE);
    CHECK_FALSE(GifWriter::Create({file.path, 32, 24, 5}).ok());
    auto result = writer.Finalize();
    CHECK_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::AlreadyExists);
    HANDLE preserved = ::CreateFileW(file.path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(preserved != INVALID_HANDLE_VALUE);
    std::array<BYTE, 4> actual{};
    DWORD read = 0;
    const BOOL readOk = ::ReadFile(preserved, actual.data(), static_cast<DWORD>(actual.size()), &read, nullptr);
    ::CloseHandle(preserved);
    CHECK(readOk != FALSE); CHECK_EQ(read, marker.size()); CHECK(actual == marker);
}

AETHER_TEST("showcase GIF: decoded RGBA preserves red left and blue right quadrants") {
    GifComScope com;
    REQUIRE(SUCCEEDED(com.status) || com.status == RPC_E_CHANGED_MODE);
    GifTestFile file;
    REQUIRE(!file.path.empty());
    auto created = GifWriter::Create({file.path, 32, 24, 5});
    REQUIRE(created.ok());
    auto writer = std::move(created).value();
    std::vector<BYTE> pixels(32U * 24U * 4U);
    for (std::size_t y = 0; y < 24; ++y) {
        for (std::size_t x = 0; x < 32; ++x) {
            const auto offset = (y * 32 + x) * 4U;
            pixels[offset] = x < 16 ? 240 : 10;
            pixels[offset + 1] = 20;
            pixels[offset + 2] = x < 16 ? 10 : 240;
            pixels[offset + 3] = 255;
        }
    }
    REQUIRE(writer.AppendFrame({32, 24, 128, pixels}).ok());
    REQUIRE(writer.Finalize().ok());
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
    std::vector<BYTE> decoded(pixels.size());
    REQUIRE(SUCCEEDED(rgba->CopyPixels(nullptr, 128, static_cast<UINT>(decoded.size()), decoded.data())));
    const std::size_t left = (12U * 32U + 8U) * 4U;
    const std::size_t right = (12U * 32U + 24U) * 4U;
    CHECK(decoded[left] > 200);
    CHECK(decoded[left + 2] < 50);
    CHECK(decoded[right] < 50);
    CHECK(decoded[right + 2] > 200);
}
