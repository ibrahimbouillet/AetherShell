// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: the animated weather illustrations: every documented WMO code has a scene, the still pose
// does not depend on the clock, motion changes pixels, Monochrome stays in its ink and Color paints
// a yellow sun; the sky follows the hour. Set AETHER_WEATHER_ART_SHEET to a .png path to write a
// contact sheet of every scene for a visual review.
#include "AetherTest.h"
#include "D2DCanvas.h"

#include "graphics/WeatherArt.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::graphics;
using aether::test::D2DCanvas;

namespace {

constexpr int kDocumentedCodes[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77,
                                    80, 81, 82, 85, 86, 95, 96, 99};

std::vector<aether::test::CanvasPixel> Draw(D2DCanvas& canvas, UINT size, const WeatherArtSubject& subject,
                                            WeatherArtStyle style, WeatherArtMotion motion,
                                            Color ink = Color::FromHex(0xFFFFFF)) {
    canvas.Begin();
    PaintWeatherArt(canvas.Context(), D2D1::RectF(0, 0, static_cast<float>(size), static_cast<float>(size)), subject, style,
                    ink, motion);
    std::vector<aether::test::CanvasPixel> pixels;
    if (!canvas.End()) return pixels;
    for (UINT y = 0; y < size; ++y)
        for (UINT x = 0; x < size; ++x) pixels.push_back(canvas.At(x, y));
    return pixels;
}

bool Same(const std::vector<aether::test::CanvasPixel>& a, const std::vector<aether::test::CanvasPixel>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b || a[i].a != b[i].a) return false;
    }
    return true;
}

std::size_t Covered(const std::vector<aether::test::CanvasPixel>& pixels) {
    std::size_t count = 0;
    for (const auto& p : pixels) count += p.a > 0 ? 1 : 0;
    return count;
}

// Writes the canvas's last frame region as a PNG through WIC (premultiplied BGRA).
bool SavePng(const std::wstring& path, const std::vector<std::uint8_t>& bgra, UINT width, UINT height) {
    winrt::com_ptr<IWICImagingFactory> wic;
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())))) return false;
    winrt::com_ptr<IWICStream> stream;
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    if (FAILED(wic->CreateStream(stream.put())) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(width, height))) {
        return false;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(frame->SetPixelFormat(&format))) return false;
    return SUCCEEDED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data()))) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

}  // namespace

AETHER_TEST("weather art: every documented WMO code has its own scene and intensity") {
    for (const int code : kDocumentedCodes) {
        const auto subject = WeatherSubjectForCode(code, true);
        CHECK(subject.scene != WeatherScene::Unknown);
        CHECK(subject.intensity >= 1 && subject.intensity <= 3);
        CHECK(WeatherArtMoves(subject));
    }
    CHECK(WeatherSubjectForCode(65, true).scene == WeatherScene::Rain);
    CHECK_EQ(WeatherSubjectForCode(65, true).intensity, 3);
    CHECK_EQ(WeatherSubjectForCode(61, true).intensity, 1);
    CHECK(WeatherSubjectForCode(82, false).scene == WeatherScene::RainShowers);
    CHECK(!WeatherSubjectForCode(82, false).isDay);
    CHECK(WeatherSubjectForCode(99, true).scene == WeatherScene::ThunderstormHail);
    CHECK(WeatherSubjectForCode(48, true).scene == WeatherScene::RimeFog);
    CHECK(WeatherSubjectForCode(4, true).scene == WeatherScene::Unknown);  // valid WMO, undocumented here
    CHECK(WeatherSubjectForCode(std::nullopt, std::nullopt).scene == WeatherScene::Unknown);
    CHECK(WeatherSubjectForCode(0, std::nullopt).isDay);
    CHECK_EQ(ToString(WeatherScene::SnowShowers), std::string_view("snow-showers"));
}

AETHER_TEST("weather art: the still pose ignores the clock and motion moves pixels") {
    D2DCanvas canvas;
    REQUIRE(canvas.Create(48, 48));
    for (const int code : {61, 71, 95, 45, 2}) {
        const auto subject = WeatherSubjectForCode(code, true);
        const auto still = Draw(canvas, 48, subject, WeatherArtStyle::Monochrome, {0.0, 0.0f});
        const auto stillLater = Draw(canvas, 48, subject, WeatherArtStyle::Monochrome, {5.37, 0.0f});
        CHECK(Covered(still) > 40);
        if (code != 2) CHECK(Same(still, stillLater));  // a sun's rays turn with the clock even at rest
        const auto moving = Draw(canvas, 48, subject, WeatherArtStyle::Monochrome, {0.43, 1.0f});
        const auto movingLater = Draw(canvas, 48, subject, WeatherArtStyle::Monochrome, {0.81, 1.0f});
        CHECK(!Same(moving, movingLater));
    }
}

AETHER_TEST("weather art: Monochrome stays in its ink, Color paints a yellow sun and blue rain") {
    D2DCanvas canvas;
    REQUIRE(canvas.Create(64, 64));
    for (const int code : kDocumentedCodes) {
        const auto pixels = Draw(canvas, 64, WeatherSubjectForCode(code, code % 2 == 0), WeatherArtStyle::Monochrome,
                                 {1.7, 1.0f}, Color::FromHex(0xFF0000));
        bool onlyInk = true;
        for (const auto& p : pixels) onlyInk = onlyInk && p.g == 0 && p.b == 0;
        CHECK(onlyInk);
        CHECK(Covered(pixels) > 60);
    }
    const auto sun = Draw(canvas, 64, WeatherSubjectForCode(0, true), WeatherArtStyle::Color, {});
    const auto centre = sun[32 * 64 + 32];
    CHECK(centre.a == 255);
    CHECK(centre.r > 220 && centre.g > 150 && centre.b < 140);

    const auto rain = Draw(canvas, 64, WeatherSubjectForCode(63, true), WeatherArtStyle::Color, {});
    bool blue = false;
    for (UINT y = 46; y < 64; ++y)
        for (UINT x = 0; x < 64; ++x) {
            const auto& p = rain[y * 64 + x];
            blue = blue || (p.a > 200 && p.b > p.r + 60);
        }
    CHECK(blue);
}

AETHER_TEST("weather art: the sky is night blue at night, light blue in the morning, grey under rain") {
    const auto clear = WeatherSubjectForCode(0, true);
    const auto night = SkyFor(clear, 2.0, 8.0, 17.0);
    CHECK(night.top.b > night.top.r);
    CHECK(night.top.r + night.top.g + night.top.b < 0.6f);
    CHECK(!night.darkText);
    const auto morning = SkyFor(clear, 9.6, 8.0, 17.0);
    CHECK(morning.top.b > morning.top.r + 0.2f);
    CHECK(morning.bottom.r + morning.bottom.g + morning.bottom.b > 2.2f);
    const auto noon = SkyFor(clear, 12.5, 8.0, 17.0);
    CHECK(noon.top.b > 0.8f);
    const auto rain = SkyFor(WeatherSubjectForCode(65, true), 12.5, 8.0, 17.0);
    CHECK(std::abs(rain.top.b - rain.top.r) < std::abs(noon.top.b - noon.top.r));  // greyed
    const auto fallback = SkyFor(clear, 23.0, -5.0, 40.0);  // nonsense sun times: 6 and 18
    CHECK(!fallback.darkText);
}

AETHER_TEST("weather art: contact sheet for review (only when asked)") {
    wchar_t target[MAX_PATH]{};
    if (::GetEnvironmentVariableW(L"AETHER_WEATHER_ART_SHEET", target, MAX_PATH) == 0) return;
    (void)::CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC; an existing apartment is fine
    const std::vector<int> codes{0, 1, 2, 3, 45, 48, 51, 55, 56, 61, 65, 66, 71, 75, 77, 80, 82, 85, 95, 99};
    constexpr UINT cell = 96, top = 22;
    const UINT columns = static_cast<UINT>(codes.size());
    const UINT rows = 6;  // colour day, colour night, colour moving, mono day, mono night, mono 18 px
    D2DCanvas canvas;
    REQUIRE(canvas.Create(columns * cell, rows * cell + top));
    canvas.Begin();
    auto* dc = canvas.Context();
    winrt::com_ptr<ID2D1SolidColorBrush> back;
    REQUIRE(SUCCEEDED(dc->CreateSolidColorBrush(D2D1::ColorF(0.13f, 0.15f, 0.2f, 1.0f), back.put())));
    dc->FillRectangle(D2D1::RectF(0, 0, static_cast<float>(columns * cell), static_cast<float>(rows * cell + top)), back.get());
    for (UINT c = 0; c < columns; ++c) {
        const float x = static_cast<float>(c * cell);
        for (UINT r = 0; r < rows; ++r) {
            const float y = static_cast<float>(top + r * cell);
            const bool day = r == 0 || r == 2 || r == 3 || r == 5;
            const auto subject = WeatherSubjectForCode(codes[c], day);
            if (r < 3) {
                const auto sky = SkyFor(subject, day ? 10.0 : 22.0);
                const D2D1_GRADIENT_STOP stops[] = {{0, sky.top.ToD2D()}, {1, sky.bottom.ToD2D()}};
                winrt::com_ptr<ID2D1GradientStopCollection> collection;
                winrt::com_ptr<ID2D1LinearGradientBrush> gradient;
                (void)dc->CreateGradientStopCollection(stops, 2, collection.put());
                (void)dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, y), D2D1::Point2F(0, y + cell)),
                                                    collection.get(), gradient.put());
                dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 3, y + 3, x + cell - 3, y + cell - 3), 12, 12), gradient.get());
                PaintWeatherArt(dc, D2D1::RectF(x + 12, y + 12, x + cell - 12, y + cell - 12), subject, WeatherArtStyle::Color, {},
                                r == 2 ? WeatherArtMotion{0.9, 1.0f} : WeatherArtMotion{});
            } else if (r < 5) {
                PaintWeatherArt(dc, D2D1::RectF(x + 24, y + 24, x + cell - 24, y + cell - 24), subject, WeatherArtStyle::Monochrome,
                                Color::FromHex(0xC8CCD4), {}, 1.5f);
            } else {
                PaintWeatherArt(dc, D2D1::RectF(x + 39, y + 39, x + 57, y + 57), subject, WeatherArtStyle::Monochrome,
                                Color::FromHex(0xC8CCD4), {}, 1.5f);
            }
        }
    }
    REQUIRE(canvas.End());
    const UINT width = columns * cell, height = rows * cell + top;
    std::vector<std::uint8_t> bgra;
    bgra.reserve(static_cast<std::size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const auto p = canvas.At(x, y);
            bgra.push_back(static_cast<std::uint8_t>(p.b));
            bgra.push_back(static_cast<std::uint8_t>(p.g));
            bgra.push_back(static_cast<std::uint8_t>(p.r));
            bgra.push_back(static_cast<std::uint8_t>(p.a));
        }
    CHECK(SavePng(target, bgra, width, height));
}
