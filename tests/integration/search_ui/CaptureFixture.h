// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include <filesystem>
#include <vector>
#include <cstdint>
namespace {
struct Capture {
    int width, height;
    std::vector<std::uint8_t> bytes;
};
bool PrivateCrop(HWND backdrop, const RECT& crop) {
    // The host backdrop can sample pixels outside the crop. Keep a full opaque 128px guard band,
    // and refuse to copy anything if either the crop or its blur neighborhood leaves our backdrop.
    RECT own{};
    if (!::GetClientRect(backdrop, &own)) return false;
    POINT origin{};
    if (!::ClientToScreen(backdrop, &origin)) return false;
    ::OffsetRect(&own, origin.x, origin.y);
    RECT guarded = crop;
    ::InflateRect(&guarded, 128, 128);
    if (guarded.left < own.left || guarded.top < own.top || guarded.right > own.right || guarded.bottom > own.bottom)
        return false;
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (window == backdrop) return true;
        if (!::IsWindowVisible(window)) continue;
        DWORD process = 0; ::GetWindowThreadProcessId(window, &process);
        if (process == ::GetCurrentProcessId()) continue;
        RECT bounds{}, overlap{};
        if (::GetWindowRect(window, &bounds) && ::IntersectRect(&overlap, &bounds, &guarded)) return false;
    }
    return false;
}
Capture CaptureOwnBackdrop(HWND backdrop, const RECT& bounds) {
    REQUIRE(PrivateCrop(backdrop, bounds));
    Capture capture{bounds.right - bounds.left, bounds.bottom - bounds.top, {}};
    HDC screen = ::GetDC(nullptr), memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = capture.width; info.bmiHeader.biHeight = -capture.height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    REQUIRE(bitmap != nullptr); REQUIRE(bits != nullptr);
    HGDIOBJ previous = ::SelectObject(memory, bitmap);
    const bool copied = ::BitBlt(memory, 0, 0, capture.width, capture.height, screen,
        bounds.left, bounds.top, SRCCOPY | CAPTUREBLT) != FALSE;
    if (copied) capture.bytes.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) +
        static_cast<std::size_t>(capture.width) * capture.height * 4);
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen);
    REQUIRE(copied); REQUIRE(PrivateCrop(backdrop, bounds)); return capture;
}
void Save(const Capture& capture, const std::filesystem::path& file) {
    auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICStream> stream; winrt::check_hresult(factory->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr)); winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->SetSize(capture.width, capture.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format)); REQUIRE(format == GUID_WICPixelFormat32bppBGRA);
    auto bytes = capture.bytes; for (std::size_t index = 3; index < bytes.size(); index += 4) bytes[index] = 255;
    winrt::check_hresult(frame->WritePixels(capture.height, capture.width * 4, static_cast<UINT>(bytes.size()), bytes.data()));
    winrt::check_hresult(frame->Commit()); winrt::check_hresult(encoder->Commit());
}
}
