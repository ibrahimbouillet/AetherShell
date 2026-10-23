// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/PngWriter.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cwctype>
#include <limits>

namespace aether::showcase {
Result<void> WritePngFrame(const std::wstring& output, const RgbaFrameView& frame) noexcept {
    if (output.size() < 4 || output[output.size()-4] != L'.' ||
        std::towlower(output[output.size()-3]) != L'p' || std::towlower(output[output.size()-2]) != L'n' ||
        std::towlower(output.back()) != L'g' || frame.width == 0 || frame.height == 0 ||
        frame.width > 16'384 || frame.height > 16'384 || frame.rowStrideBytes < frame.width * 4ULL ||
        frame.rowStrideBytes > std::numeric_limits<UINT>::max() / frame.height ||
        frame.pixels.size() < frame.rowStrideBytes * frame.height)
        return MakeError(ErrorCode::InvalidArgument, "PNG output geometry or file type is invalid");
    if (::GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
        return MakeError(ErrorCode::AlreadyExists, "PNG output already exists");
    struct Temporary final {
        std::wstring path;
        ~Temporary() { if (!path.empty()) (void)::DeleteFileW(path.c_str()); }
    } temporary;
    try {
        GUID id{};
        wchar_t text[40]{};
        if (FAILED(::CoCreateGuid(&id)) || !::StringFromGUID2(id, text, 40))
            return MakeError(ErrorCode::Unavailable, "PNG temporary identifier is unavailable");
        temporary.path = output + text + L".tmp";
        HANDLE reserved = ::CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                       FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (reserved == INVALID_HANDLE_VALUE) { temporary.path.clear(); return MakeError(ErrorCode::Io, "PNG temporary file is unavailable"); }
        (void)::CloseHandle(reserved);
        using Microsoft::WRL::ComPtr;
        ComPtr<IWICImagingFactory> factory;
        HRESULT status = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> encoded;
        ComPtr<IWICBitmap> bitmap;
        ComPtr<IWICFormatConverter> converted;
        if (SUCCEEDED(status)) status = factory->CreateStream(&stream);
        if (SUCCEEDED(status)) status = stream->InitializeFromFilename(temporary.path.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(status)) status = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (SUCCEEDED(status)) status = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (SUCCEEDED(status)) status = encoder->CreateNewFrame(&encoded, nullptr);
        if (SUCCEEDED(status)) status = encoded->Initialize(nullptr);
        if (SUCCEEDED(status)) status = encoded->SetSize(frame.width, frame.height);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(status)) status = encoded->SetPixelFormat(&format);
        if (SUCCEEDED(status) && format != GUID_WICPixelFormat32bppBGRA) status = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        if (SUCCEEDED(status)) status = factory->CreateBitmapFromMemory(frame.width, frame.height,
            GUID_WICPixelFormat32bppRGBA, static_cast<UINT>(frame.rowStrideBytes),
            static_cast<UINT>(frame.rowStrideBytes * frame.height), const_cast<BYTE*>(frame.pixels.data()), &bitmap);
        if (SUCCEEDED(status)) status = factory->CreateFormatConverter(&converted);
        if (SUCCEEDED(status)) status = converted->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(status)) status = encoded->WriteSource(converted.Get(), nullptr);
        if (SUCCEEDED(status)) status = encoded->Commit();
        if (SUCCEEDED(status)) status = encoder->Commit();
        encoded.Reset(); encoder.Reset(); stream.Reset();
        if (FAILED(status)) return MakeError(ErrorCode::Platform, "WIC PNG encoding failed", status);
        if (!::MoveFileExW(temporary.path.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH))
            return MakeError(ErrorCode::Io, "PNG output could not be finalized without replacement", ::GetLastError());
        temporary.path.clear();
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "PNG encoding allocation failed"); }
}
}
