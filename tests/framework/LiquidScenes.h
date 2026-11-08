// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D66, D67: the scenes the Liquid glass is judged over, from the owner's Liquid Glass skill (G01 to G06) and the owner's
// own screenshots: white, black, a black and white split, vivid hues, a light page of text and pictures, a dark window
// full of text, a light application, and a photograph. Painted by this test's own private window; only window classes,
// sizes and this process's own pixels are used. Shared by the Top Bar and Dock suites.
#pragma once

#include <windows.h>
#include <wincodec.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

namespace aether::test::liquid {

inline constexpr std::array<const char*, 8> kSceneNames{"white", "black", "split", "vivid", "light-page", "dark-text", "light-app", "photo"};

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
};

inline Image& Photo() { static Image photo; return photo; }
inline int& SceneIndex() { static int scene = 0; return scene; }

// The README's wallpaper, when the sources are found above the test program; empty otherwise.
inline Image LoadPhoto() {
    Image picture;
    wchar_t module[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, module, MAX_PATH);
    for (auto dir = std::filesystem::path(module).parent_path(); dir.has_relative_path(); dir = dir.parent_path()) {
        const auto file = dir / L"resources" / L"preview" / L"setup_preview-3.webp";
        if (!std::filesystem::exists(file)) continue;
        try {
            auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
            winrt::com_ptr<IWICBitmapDecoder> decoder;
            winrt::check_hresult(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put()));
            winrt::com_ptr<IWICBitmapFrameDecode> frame;
            winrt::check_hresult(decoder->GetFrame(0, frame.put()));
            winrt::com_ptr<IWICFormatConverter> converter;
            winrt::check_hresult(factory->CreateFormatConverter(converter.put()));
            winrt::check_hresult(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
            UINT w = 0, h = 0;
            winrt::check_hresult(converter->GetSize(&w, &h));
            picture.bgra.resize(static_cast<std::size_t>(w) * h * 4);
            winrt::check_hresult(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(picture.bgra.size()), picture.bgra.data()));
            picture.width = static_cast<int>(w);
            picture.height = static_cast<int>(h);
        } catch (...) {
            picture = {};
        }
        break;
    }
    return picture;
}

// Lines of real text, as a page or a chat shows them.
inline void PaintText(HDC dc, const RECT& client, COLORREF ink, COLORREF accent, int size) {
    HFONT font = ::CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old = ::SelectObject(dc, font);
    ::SetBkMode(dc, TRANSPARENT);
    static constexpr const wchar_t* kLines[] = {
        L"The bars float over whatever page is open: a document, a chat, code, a photograph.",
        L"System tools (deferred)   MCP tools   Check the build   Broaden the measurement   21.6k",
        L"function render(scene) { return glass.adapt(scene.backdrop).withLight(rim, edge); }",
        L"Detect language   French   English   Arabic      Italian   English   French   Translation",
        L"Each layer has one job: behind, the blur, the liquid's light, then the icons and the text."};
    int line = 0;
    for (int y = 4; y < client.bottom; y += size + 7, ++line) {
        ::SetTextColor(dc, line % 5 == 2 ? accent : ink);
        const auto* text = kLines[line % 5];
        ::TextOutW(dc, 12 + (line % 3) * 9, y, text, static_cast<int>(std::wcslen(text)));
        ::TextOutW(dc, 12 + (line % 3) * 9 + 760, y, text, static_cast<int>(std::wcslen(text)));
    }
    ::SelectObject(dc, old);
    ::DeleteObject(font);
}

inline void PaintScene(HDC dc, const RECT& client) {
    const auto fill = [&](const RECT& r, COLORREF color) { HBRUSH b = ::CreateSolidBrush(color); ::FillRect(dc, &r, b); ::DeleteObject(b); };
    const int w = client.right, h = client.bottom;
    switch (SceneIndex()) {
        case 0: fill(client, RGB(255, 255, 255)); break;
        case 1: fill(client, RGB(0, 0, 0)); break;
        case 2: fill({0, 0, w / 2, h}, RGB(0, 0, 0)); fill({w / 2, 0, w, h}, RGB(255, 255, 255)); break;
        case 3:
            for (int x = 0; x < w; x += 2) {   // the hues across, as vivid as sRGB allows
                const float hue = 6.0f * static_cast<float>(x) / static_cast<float>(w);
                const int i = static_cast<int>(hue) % 6;
                const int up = static_cast<int>(255.0f * (hue - std::floor(hue))), down = 255 - up;
                const COLORREF c = i == 0 ? RGB(255, up, 0) : i == 1 ? RGB(down, 255, 0) : i == 2 ? RGB(0, 255, up)
                                 : i == 3 ? RGB(0, down, 255) : i == 4 ? RGB(up, 0, 255) : RGB(255, 0, down);
                fill({x, 0, x + 2, h}, c);
            }
            break;
        case 4:   // a light page scrolled under the bar: text and two pictures on white
            fill(client, RGB(255, 255, 255));
            fill({w / 3, 120, w / 3 + 160, 200}, RGB(220, 70, 40));
            fill({2 * w / 3, 110, 2 * w / 3 + 180, 230}, RGB(30, 110, 200));
            PaintText(dc, client, RGB(32, 33, 36), RGB(26, 115, 232), 15);
            break;
        case 5:   // a dark window full of text (a chat, an editor)
            fill(client, RGB(31, 31, 31));
            fill({0, 0, w / 4, h}, RGB(24, 24, 24));
            PaintText(dc, client, RGB(214, 214, 214), RGB(86, 156, 214), 15);
            break;
        case 6: {   // a light application: its toolbar, tabs and grey text
            fill(client, RGB(255, 255, 255));
            fill({0, 0, w, 44}, RGB(223, 227, 235));
            for (int x = 8; x < w - 200; x += 196) fill({x, 8, x + 188, 44}, x == 8 ? RGB(255, 255, 255) : RGB(232, 236, 242));
            fill({0, 44, w, 92}, RGB(241, 243, 244));
            fill({60, 54, w - 60, 82}, RGB(255, 255, 255));
            PaintText(dc, {0, 100, w, h}, RGB(95, 99, 104), RGB(26, 115, 232), 16);
            break;
        }
        default: {
            const auto& photo = Photo();
            if (photo.width <= 0) { fill(client, RGB(90, 90, 90)); break; }
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = photo.width;
            info.bmiHeader.biHeight = -photo.height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            // "Fill", as Windows shows a wallpaper: the whole picture, scaled to cover, centred.
            const double cover = std::max(static_cast<double>(w) / photo.width, static_cast<double>(h) / photo.height);
            const int dw = static_cast<int>(std::ceil(photo.width * cover)), dh = static_cast<int>(std::ceil(photo.height * cover));
            ::SetStretchBltMode(dc, HALFTONE);
            ::StretchDIBits(dc, (w - dw) / 2, (h - dh) / 2, dw, dh, 0, 0, photo.width, photo.height, photo.bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        }
    }
}

inline LRESULT CALLBACK SceneProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(hwnd, &paint);
        RECT client{};
        ::GetClientRect(hwnd, &client);
        PaintScene(dc, client);
        ::EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

// The private window behind the bar under test, topmost below it.
struct SceneBackdrop {
    HWND hwnd = nullptr;
    SceneBackdrop() = default;
    SceneBackdrop(const SceneBackdrop&) = delete;
    SceneBackdrop& operator=(const SceneBackdrop&) = delete;
    ~SceneBackdrop() { if (hwnd) ::DestroyWindow(hwnd); }
    bool Create(const RECT& bounds) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = SceneProc;
        cls.hInstance = ::GetModuleHandleW(nullptr);
        cls.lpszClassName = L"Aether.Tests.LiquidScenes";
        if (!::RegisterClassW(&cls) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"", WS_POPUP, bounds.left, bounds.top,
                                 bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, nullptr);
        if (!hwnd) return false;
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        ::UpdateWindow(hwnd);
        return true;
    }
    void Show(int scene) const {
        SceneIndex() = scene;
        ::InvalidateRect(hwnd, nullptr, FALSE);
        ::UpdateWindow(hwnd);
    }
};

// WCAG 2.2 relative luminance and contrast, from 8-bit sRGB.
inline double Linear(double v) { const double s = v / 255.0; return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); }
inline double Luminance(const std::uint8_t* bgra) { return 0.2126 * Linear(bgra[2]) + 0.7152 * Linear(bgra[1]) + 0.0722 * Linear(bgra[0]); }
inline double LuminanceOf(float r, float g, float b) { return 0.2126 * Linear(r * 255.0) + 0.7152 * Linear(g * 255.0) + 0.0722 * Linear(b * 255.0); }
inline double Contrast(double a, double b) { return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05); }

// Several captures side by side, rows under one another, on grey, for a person to compare.
inline Image Sheet(const std::vector<std::vector<Image>>& rows) {
    Image sheet;
    for (const auto& row : rows) {
        int width = 0, height = 0;
        for (const auto& c : row) { width += c.width + 8; height = std::max(height, c.height); }
        sheet.width = std::max(sheet.width, width);
        sheet.height += height + 8;
    }
    sheet.bgra.assign(static_cast<std::size_t>(sheet.width) * sheet.height * 4, 128);
    int top = 0;
    for (const auto& row : rows) {
        int left = 0, height = 0;
        for (const auto& c : row) {
            for (int y = 0; y < c.height; ++y)
                std::copy_n(c.bgra.data() + static_cast<std::size_t>(y) * c.width * 4, static_cast<std::size_t>(c.width) * 4,
                            sheet.bgra.data() + (static_cast<std::size_t>(top + y) * sheet.width + left) * 4);
            left += c.width + 8;
            height = std::max(height, c.height);
        }
        top += height + 8;
    }
    return sheet;
}

inline void SavePng(const Image& image, const std::filesystem::path& file) {
    auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICStream> stream;
    winrt::check_hresult(factory->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height)));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format));
    auto bytes = image.bgra;
    for (std::size_t index = 3; index < bytes.size(); index += 4) bytes[index] = 255;
    winrt::check_hresult(frame->WritePixels(static_cast<UINT>(image.height), static_cast<UINT>(image.width) * 4, static_cast<UINT>(bytes.size()), bytes.data()));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
}

}  // namespace aether::test::liquid
