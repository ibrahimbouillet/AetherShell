// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/PngWriter.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
using namespace aether;
using namespace aether::showcase;
namespace {
struct PngTestScope final {
    HRESULT com=::CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::filesystem::path directory=aether::test::MakeTempDirectory("showcase-png");
    ~PngTestScope(){std::error_code ignored;std::filesystem::remove_all(directory,ignored);if(SUCCEEDED(com))::CoUninitialize();}
};
}
AETHER_TEST("showcase PNG: lossless RGBA round trip dimensions orientation and no replacement") {
    PngTestScope scope;REQUIRE(SUCCEEDED(scope.com)||scope.com==RPC_E_CHANGED_MODE);
    const auto path=(scope.directory/L"frame.png").native();
    const std::array<std::uint8_t,24> pixels{240,10,20,255,10,20,240,255,0,0,0,0,
        20,240,10,255,240,200,20,255,0,0,0,0};
    REQUIRE(WritePngFrame(path,{2,2,12,pixels}));
    CHECK(!WritePngFrame(path,{2,2,12,pixels}));
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    REQUIRE(SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))));
    ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> rgba;
    REQUIRE(SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)));
    REQUIRE(SUCCEEDED(decoder->GetFrame(0,&frame)));
    UINT width=0,height=0;REQUIRE(SUCCEEDED(frame->GetSize(&width,&height)));CHECK_EQ(width,2U);CHECK_EQ(height,2U);
    REQUIRE(SUCCEEDED(factory->CreateFormatConverter(&rgba)));
    REQUIRE(SUCCEEDED(rgba->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)));
    std::array<std::uint8_t,16> decoded{};
    REQUIRE(SUCCEEDED(rgba->CopyPixels(nullptr,8,16,decoded.data())));
    for(std::size_t y=0;y<2;++y)for(std::size_t x=0;x<8;++x)CHECK_EQ(decoded[y*8+x],pixels[y*12+x]);
}
AETHER_TEST("showcase PNG: invalid stride bounds and extension leave no final output") {
    PngTestScope scope;const auto path=(scope.directory/L"invalid.png").native();
    std::array<std::uint8_t,16> pixels{};
    CHECK(!WritePngFrame(path,{2,2,7,pixels}));CHECK(!WritePngFrame(path,{2,3,8,pixels}));
    CHECK(!WritePngFrame((scope.directory/L"invalid.jpg").native(),{2,2,8,pixels}));
    CHECK(!std::filesystem::exists(path));
}
