// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D62/D63: the arrival's motion and the two materials. The renderer runs on a WARP (software) Direct3D
// device with this test's own pictures: no window, no screen. Set AETHER_LIQUID_PICTURE to a picture file to
// also save frames under %TEMP%\AetherShellTests\ for comparison with tools/liquid-glass/welcome-bubble.html.
#include "AetherTest.h"

#include "graphics/LiquidMotion.h"
#include "graphics/LiquidRenderer.h"
#include "graphics/WinRT.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::graphics;

namespace {

ArrivalLayout Screen1080() {
    ArrivalLayout layout;
    layout.width = 1280.0f;   // 1920 x 1080 at 150 %
    layout.height = 720.0f;
    layout.topBar = {12.0f, 6.0f, 1268.0f, 38.0f, 11.0f};
    layout.dock = {438.0f, 653.0f, 842.0f, 716.0f, 21.0f};
    return layout;
}

winrt::com_ptr<ID3D11Device> SoftwareDevice() {
    winrt::com_ptr<ID3D11Device> device;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    (void)::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION,
                              device.put(), nullptr, nullptr);
    return device;
}

// A picture of this test's own: left half `left`, right half `right`, opaque BGRA.
std::shared_ptr<std::vector<std::uint8_t>> SplitPicture(UINT width, UINT height, std::uint8_t left, std::uint8_t right) {
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(width) * height * 4u, std::uint8_t{255});
    for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
        auto* p = bytes->data() + (static_cast<std::size_t>(y) * width + x) * 4u;
        p[0] = p[1] = p[2] = x < width / 2 ? left : right;
    }
    return bytes;
}

const std::uint8_t* At(const std::vector<std::uint8_t>& bgra, UINT width, UINT x, UINT y) { return bgra.data() + (static_cast<std::size_t>(y) * width + x) * 4u; }

// A picture file, reduced with Fant until it just covers width x height (as the shell decodes the wallpaper).
std::shared_ptr<std::vector<std::uint8_t>> DecodeCovering(const std::wstring& file, UINT width, UINT height, UINT& outWidth, UINT& outHeight) {
    winrt::com_ptr<IWICImagingFactory> factory;
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))) ||
        FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())) ||
        FAILED(decoder->GetFrame(0, frame.put()))) return nullptr;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    const double scale = std::max(static_cast<double>(width) / w, static_cast<double>(height) / h);
    outWidth = static_cast<UINT>(std::lround(w * scale));
    outHeight = static_cast<UINT>(std::lround(h * scale));
    winrt::com_ptr<IWICBitmapScaler> scaler;
    winrt::com_ptr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateBitmapScaler(scaler.put())) || FAILED(scaler->Initialize(frame.get(), outWidth, outHeight, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return nullptr;
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(outWidth) * outHeight * 4u);
    if (FAILED(converter->CopyPixels(nullptr, outWidth * 4u, static_cast<UINT>(bytes->size()), bytes->data()))) return nullptr;
    return bytes;
}

void SavePng(const std::vector<std::uint8_t>& bgra, UINT width, UINT height, const std::filesystem::path& path) {
    winrt::com_ptr<IWICImagingFactory> factory;
    winrt::com_ptr<IWICStream> stream;
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))) ||
        FAILED(factory->CreateStream(stream.put())) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) || FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) || FAILED(frame->Initialize(nullptr))) return;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetSize(width, height);
    frame->SetPixelFormat(&format);
    frame->WritePixels(height, width * 4u, static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data()));
    frame->Commit();
    encoder->Commit();
}

}  // namespace

AETHER_TEST("liquid motion: a drop forms from a point, divides into two, and lands on the real bars") {
    const auto layout = Screen1080();
    const auto start = ArrivalAt(0.0f, layout, true);
    CHECK_FALSE(start.bodies[0].present);
    CHECK_NEAR(start.veil, 0.5f, 1e-4f);   // it begins on the welcome cover
    const auto floating = ArrivalAt(0.9f, layout, true);
    REQUIRE(floating.bodies[0].present);
    CHECK_NEAR(floating.bodies[0].cy, floating.bodies[1].cy, 1e-3f);   // one drop: both halves coincide
    CHECK(floating.logoAlpha > 0.9f);
    CHECK_EQ(floating.veil, 0.0f);
    const auto settled = ArrivalAt(kArrivalSettledSeconds + 0.6f, layout, true);
    const auto& top = settled.bodies[0];
    const auto& dock = settled.bodies[1];
    CHECK_NEAR(top.cy, (layout.topBar.top + layout.topBar.bottom) / 2.0f, 1.0f);
    CHECK_NEAR(top.hy, (layout.topBar.bottom - layout.topBar.top) / 2.0f, 0.5f);
    CHECK_NEAR(top.hx, (layout.topBar.right - layout.topBar.left) / 2.0f, 4.0f);
    CHECK_NEAR(dock.cy, (layout.dock.top + layout.dock.bottom) / 2.0f, 1.0f);
    CHECK_NEAR(dock.hx, (layout.dock.right - layout.dock.left) / 2.0f, 3.0f);
    CHECK_NEAR(dock.radius, layout.dock.radius, 0.5f);
    CHECK_EQ(settled.lens, 1.0f);
    CHECK_EQ(settled.logoAlpha, 0.0f);
    // D63: only two drops divide; nothing is left between them.
    for (float t = 0.0f; t <= kArrivalSeconds; t += 0.01f) CHECK_FALSE(ArrivalAt(t, layout, true).bodies[2].present);
}

AETHER_TEST("liquid motion: nothing jumps from one frame to the next, from the drop to the bars") {
    const auto layout = Screen1080();
    constexpr float kStep = 1.0f / 240.0f;
    float largest = 0.0f, when = 0.0f;
    auto previous = ArrivalAt(0.02f, layout, false);
    for (float t = 0.02f + kStep; t <= kArrivalSettledSeconds; t += kStep) {
        const auto frame = ArrivalAt(t, layout, false);
        for (std::size_t i = 0; i < 2; ++i) {
            const auto& a = previous.bodies[i];
            const auto& b = frame.bodies[i];
            const float change = std::max({std::abs(a.cx - b.cx), std::abs(a.hx - b.hx), std::abs(a.hy - b.hy)});
            // Vertical speed may be high when the drops fly; their size and the rest may not leap.
            if (change > largest) { largest = change; when = t; }
        }
        previous = frame;
    }
    TEST_NOTE("largest change in size or horizontal place in 1/240 s: " + std::to_string(largest) + " DIPs at " + std::to_string(when) + " s");
    CHECK(largest < 16.0f);   // the Top Bar spreading across 1256 DIPs is the fastest thing on screen
}

AETHER_TEST("liquid material: water shows the picture itself, with no colour of its own, and casts a shadow") {
    const auto device = SoftwareDevice();
    REQUIRE(device != nullptr);
    LiquidRenderer renderer;
    const auto ready = renderer.Initialize(device.get());
    if (!ready) TEST_NOTE(ready.error().message);
    REQUIRE(ready.ok());
    constexpr UINT kWidth = 320, kHeight = 160;
    renderer.SetPicture(SplitPicture(kWidth, kHeight, 200, 60), kWidth, kHeight);
    MaterialScene scene;
    scene.style = MaterialStyle::Liquid;
    scene.bodies[0] = {true, 160.0f, 70.0f, 120.0f, 24.0f, 12.0f, 0.0f};
    scene.lens = 1.0f;
    scene.picture = D2D1::RectF(0.0f, 0.0f, kWidth, kHeight);
    std::vector<std::uint8_t> pixels;
    REQUIRE(renderer.RenderToMemory(scene, D2D1::SizeU(kWidth, kHeight), pixels).ok());
    REQUIRE_EQ(pixels.size(), static_cast<std::size_t>(kWidth) * kHeight * 4u);
    CHECK_EQ(At(pixels, kWidth, 5, 5)[3], 0);                  // far from the bar: nothing
    CHECK(At(pixels, kWidth, 160, 100)[3] > 8);                // below it: its shadow
    CHECK(At(pixels, kWidth, 160, 100)[3] < 120);
    CHECK_EQ(At(pixels, kWidth, 100, 70)[3], 255);             // inside: opaque water
    // Where the water is flat it shows the picture itself: no tint, no blur, no grain (D63).
    CHECK(std::abs(At(pixels, kWidth, 100, 70)[1] - 200) <= 2);
    CHECK(std::abs(At(pixels, kWidth, 220, 70)[1] - 60) <= 2);
    // No colour of its own, never blue: over a grey picture every pixel stays grey.
    int largestCast = 0;
    for (UINT y = 40; y < 100; ++y) for (UINT x = 30; x < 290; ++x) {
        const auto* p = At(pixels, kWidth, x, y);
        largestCast = std::max(largestCast, static_cast<int>(p[0]) - std::max<int>(p[1], p[2]));
    }
    CHECK(largestCast <= 1);
    // Grain: frosted, darker than the picture, with its grain.
    scene.style = MaterialStyle::Grain;
    std::vector<std::uint8_t> grain;
    REQUIRE(renderer.RenderToMemory(scene, D2D1::SizeU(kWidth, kHeight), grain).ok());
    CHECK(At(grain, kWidth, 100, 70)[1] < 190);
    int differences = 0;
    for (UINT x = 90; x < 110; ++x) differences += At(grain, kWidth, x, 70)[1] != At(grain, kWidth, x + 1, 70)[1];
    CHECK(differences > 5);
}

AETHER_TEST("liquid material: the bars' transparent water leaves the page behind visible and adds only its light and shade") {
    const auto device = SoftwareDevice();
    REQUIRE(device != nullptr);
    LiquidRenderer renderer;
    REQUIRE(renderer.Initialize(device.get()).ok());   // no picture at all: transparent water needs none (D65)
    constexpr UINT kWidth = 320, kHeight = 160;
    MaterialScene scene;
    scene.style = MaterialStyle::Liquid;
    scene.transparent = true;
    scene.edgeBend = 0.0f;
    scene.bodies[0] = {true, 160.0f, 70.0f, 120.0f, 24.0f, 12.0f, 0.0f};
    scene.lens = 1.0f;
    std::vector<std::uint8_t> pixels;
    REQUIRE(renderer.RenderToMemory(scene, D2D1::SizeU(kWidth, kHeight), pixels).ok());
    CHECK(At(pixels, kWidth, 160, 70)[3] <= 2);                // the middle of the bar: the page itself, untouched
    CHECK(At(pixels, kWidth, 160, 47)[3] > 40);                // its top edge catches the light...
    CHECK(At(pixels, kWidth, 160, 47)[1] > 30);                // ...white light (with some of the edge's shade)
    CHECK(At(pixels, kWidth, 160, 100)[3] > 8);                // and it casts its shadow below
    CHECK_EQ(At(pixels, kWidth, 5, 5)[3], 0);
    int largestCast = 0;
    for (UINT y = 40; y < 100; ++y) for (UINT x = 30; x < 290; ++x) {
        const auto* p = At(pixels, kWidth, x, y);
        largestCast = std::max(largestCast, std::abs(static_cast<int>(p[0]) - static_cast<int>(p[1])) + std::abs(static_cast<int>(p[2]) - static_cast<int>(p[1])));
    }
    CHECK(largestCast <= 2);   // white light and black shade only: no colour of its own
}

AETHER_TEST("liquid material: the arrival renders on its own picture, every phase, and saves frames to compare") {
    const auto device = SoftwareDevice();
    REQUIRE(device != nullptr);
    LiquidRenderer renderer;
    REQUIRE(renderer.Initialize(device.get()).ok());
    const auto layout = Screen1080();
    constexpr UINT kWidth = 1280, kHeight = 720;
    renderer.SetPicture(SplitPicture(kWidth, kHeight, 180, 90), kWidth, kHeight);
    const auto folder = std::filesystem::temp_directory_path() / L"AetherShellTests";
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    for (const float t : {0.3f, 0.9f, 1.8f, 1.97f, 2.4f, 3.0f}) {
        for (const auto style : {MaterialStyle::Liquid, MaterialStyle::Grain}) {
            auto scene = SceneFromArrival(ArrivalAt(t, layout, true), style, D2D1::Point2F(0.0f, 0.0f), 1.0f);
            scene.background = true;
            scene.picture = D2D1::RectF(0.0f, 0.0f, kWidth, kHeight);
            std::vector<std::uint8_t> pixels;
            REQUIRE(renderer.RenderToMemory(scene, D2D1::SizeU(kWidth, kHeight), pixels).ok());
            CHECK_EQ(At(pixels, kWidth, 3, 3)[3], 255);   // the picture fills the screen behind
        }
    }
    TEST_NOTE("rendered six moments in both styles");
}

// Only when AETHER_LIQUID_PICTURE names a picture: the arrival at the prototype's moments, 1280 x 720 at 1x, saved
// as liquid-<style>-<seconds>.png for a pixel comparison with welcome-bubble.html?t=<seconds>&picture=<same file>.
AETHER_TEST("liquid material: frames to compare with the validated prototype") {
    wchar_t file[1024]{};
    if (!::GetEnvironmentVariableW(L"AETHER_LIQUID_PICTURE", file, 1024)) { TEST_NOTE("AETHER_LIQUID_PICTURE not set: nothing to compare"); return; }
    struct Com { HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); ~Com() { if (SUCCEEDED(hr)) ::CoUninitialize(); } } com;   // WIC
    const auto device = SoftwareDevice();
    REQUIRE(device != nullptr);
    LiquidRenderer renderer;
    REQUIRE(renderer.Initialize(device.get()).ok());
    constexpr UINT kWidth = 1280, kHeight = 720;
    UINT pw = 0, ph = 0;
    auto picture = DecodeCovering(file, kWidth, kHeight, pw, ph);
    REQUIRE(picture != nullptr);
    renderer.SetPicture(picture, pw, ph);
    // The prototype's own bars: 32 DIPs high, 6 from the top, 12 from the sides; eight 40-DIP icons, 4 from the bottom.
    ArrivalLayout layout;
    layout.width = kWidth;
    layout.height = kHeight;
    layout.topBar = {12.0f, 6.0f, kWidth - 12.0f, 38.0f, 11.0f};
    const float dockWidth = 8 * 40 + 7 * 8 + 28.0f, dockHeight = 63.0f;
    layout.dock = {(kWidth - dockWidth) / 2.0f, kHeight - 4.0f - dockHeight, (kWidth + dockWidth) / 2.0f, kHeight - 4.0f, 21.0f};
    const float left = (kWidth - static_cast<float>(pw)) / 2.0f, top = (kHeight - static_cast<float>(ph)) / 2.0f;
    const auto folder = std::filesystem::temp_directory_path() / L"AetherShellTests";
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    for (const char* seconds : {"0.6", "0.9", "1.8", "1.97", "2.25", "2.6", "3.5"}) {
        for (const auto style : {MaterialStyle::Liquid, MaterialStyle::Grain}) {
            auto scene = SceneFromArrival(ArrivalAt(std::stof(seconds), layout, true), style, D2D1::Point2F(0.0f, 0.0f), 1.0f);
            scene.background = true;
            scene.picture = D2D1::RectF(left, top, left + static_cast<float>(pw), top + static_cast<float>(ph));
            std::vector<std::uint8_t> pixels;
            REQUIRE(renderer.RenderToMemory(scene, D2D1::SizeU(kWidth, kHeight), pixels).ok());
            SavePng(pixels, kWidth, kHeight, folder / (std::string("liquid-") + (style == MaterialStyle::Liquid ? "liquid-" : "grain-") + seconds + ".png"));
        }
    }
    TEST_NOTE("saved 14 frames under " + folder.string());
}
