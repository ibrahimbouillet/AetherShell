// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/GifWriter.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <cwctype>
#include <limits>
#include <utility>

namespace aether::showcase {
namespace {
using Microsoft::WRL::ComPtr;

Error GifError(const char* message, HRESULT status) {
    return MakeError(ErrorCode::Platform, message, static_cast<std::int64_t>(status));
}

bool GifPath(const std::wstring& value) noexcept {
    if (value.size() < 4) return false;
    const auto offset = value.size() - 4;
    return value[offset] == L'.' && std::towlower(value[offset + 1]) == L'g' &&
        std::towlower(value[offset + 2]) == L'i' && std::towlower(value[offset + 3]) == L'f';
}

std::wstring TemporaryGifPath(const std::wstring& output) {
    GUID id{};
    if (FAILED(::CoCreateGuid(&id))) return {};
    wchar_t text[40]{};
    if (::StringFromGUID2(id, text, static_cast<int>(std::size(text))) == 0) return {};
    const auto separator = output.find_last_of(L"\\/");
    return (separator == std::wstring::npos ? std::wstring{} : output.substr(0, separator + 1)) +
        L".aether-showcase-" + text + L".gif";
}

HRESULT SetLoop(IWICMetadataQueryWriter* metadata) noexcept {
    std::array<BYTE, 11> application{'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'};
    std::array<BYTE, 5> data{3, 1, 0, 0, 0};
    PROPVARIANT value{};
    value.vt = VT_UI1 | VT_VECTOR;
    value.caub.cElems = static_cast<ULONG>(application.size());
    value.caub.pElems = application.data();
    HRESULT status = metadata->SetMetadataByName(L"/appext/application", &value);
    if (FAILED(status)) return status;
    value.caub.cElems = static_cast<ULONG>(data.size());
    value.caub.pElems = data.data();
    return metadata->SetMetadataByName(L"/appext/data", &value);
}

HRESULT SetTiming(IWICBitmapFrameEncode* frame, std::uint32_t delay) noexcept {
    ComPtr<IWICMetadataQueryWriter> metadata;
    HRESULT status = frame->GetMetadataQueryWriter(&metadata);
    if (FAILED(status)) return status;
    PROPVARIANT value{};
    value.vt = VT_UI2;
    value.uiVal = static_cast<USHORT>(delay);
    status = metadata->SetMetadataByName(L"/grctlext/Delay", &value);
    if (FAILED(status)) return status;
    value.vt = VT_UI1;
    value.bVal = 1;  // Full opaque frames replace the preceding canvas; retain the current frame.
    status = metadata->SetMetadataByName(L"/grctlext/Disposal", &value);
    if (FAILED(status)) return status;
    value.vt = VT_BOOL;
    value.boolVal = VARIANT_FALSE;
    return metadata->SetMetadataByName(L"/grctlext/TransparencyFlag", &value);
}

bool ValidFrame(const RgbaFrameView& frame, const GifWriterOptions& options) noexcept {
    if (frame.width != options.width || frame.height != options.height) return false;
    const auto rowBytes = static_cast<std::size_t>(frame.width) * 4U;
    if (frame.rowStrideBytes < rowBytes || frame.rowStrideBytes > std::numeric_limits<UINT>::max() ||
        frame.rowStrideBytes > std::numeric_limits<UINT>::max() / frame.height) return false;
    const auto bytes = frame.rowStrideBytes * static_cast<std::size_t>(frame.height);
    if (frame.pixels.size() < bytes) return false;
    for (std::uint32_t y = 0; y < frame.height; ++y)
        for (std::uint32_t x = 0; x < frame.width; ++x)
            if (frame.pixels[static_cast<std::size_t>(y) * frame.rowStrideBytes + x * 4U + 3U] != 255)
                return false;
    return true;
}
}  // namespace

struct GifWriter::Impl final {
    GifWriterOptions options;
    std::wstring temporaryPath;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICPalette> palette;
    std::uint64_t count = 0;
    bool closed = false;
    bool published = false;
    bool failed = false;
    ~Impl() {
        encoder.Reset(); stream.Reset(); palette.Reset(); factory.Reset();
        if (!published && !temporaryPath.empty()) (void)::DeleteFileW(temporaryPath.c_str());
    }
};

GifWriter::GifWriter() noexcept = default;
GifWriter::~GifWriter() = default;
GifWriter::GifWriter(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
GifWriter::GifWriter(GifWriter&&) noexcept = default;
GifWriter& GifWriter::operator=(GifWriter&&) noexcept = default;

Result<GifWriter> GifWriter::Create(GifWriterOptions options) noexcept {
    if (!GifPath(options.outputFile) || options.width == 0 || options.height == 0 ||
        options.width > 16'384 || options.height > 16'384 ||
        options.frameDelayCentiseconds < 2 || options.frameDelayCentiseconds > 65'535) {
        return MakeError(ErrorCode::InvalidArgument, "GIF output settings are invalid");
    }
    if (::GetFileAttributesW(options.outputFile.c_str()) != INVALID_FILE_ATTRIBUTES)
        return MakeError(ErrorCode::AlreadyExists, "GIF output already exists; choose a new file name");
    const DWORD fileError = ::GetLastError();
    if (fileError != ERROR_FILE_NOT_FOUND && fileError != ERROR_PATH_NOT_FOUND)
        return MakeError(ErrorCode::AccessDenied, "GIF output location is unavailable", fileError);
    try {
        auto impl = std::make_unique<Impl>();
        impl->options = std::move(options);
        impl->temporaryPath = TemporaryGifPath(impl->options.outputFile);
        if (impl->temporaryPath.empty()) return MakeError(ErrorCode::Unavailable, "GIF temporary output unavailable");
        // CREATE_NEW reserves ownership; subsequent WIC access never selects an existing candidate.
        HANDLE reserved = ::CreateFileW(impl->temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (reserved == INVALID_HANDLE_VALUE) {
            impl->temporaryPath.clear();
            return MakeError(ErrorCode::Unavailable, "GIF temporary output unavailable");
        }
        ::CloseHandle(reserved);
        HRESULT status = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&impl->factory));
        if (FAILED(status)) return GifError("WIC GIF factory unavailable", status);
        status = impl->factory->CreateStream(&impl->stream);
        if (SUCCEEDED(status)) status = impl->stream->InitializeFromFilename(impl->temporaryPath.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(status)) status = impl->factory->CreateEncoder(GUID_ContainerFormatGif, nullptr, &impl->encoder);
        if (SUCCEEDED(status)) status = impl->encoder->Initialize(impl->stream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICMetadataQueryWriter> metadata;
        if (SUCCEEDED(status)) status = impl->encoder->GetMetadataQueryWriter(&metadata);
        if (SUCCEEDED(status)) status = SetLoop(metadata.Get());
        if (FAILED(status)) return GifError("WIC GIF encoder could not initialize", status);
        return GifWriter(std::move(impl));
    } catch (...) { return MakeError(ErrorCode::Internal, "GIF writer allocation failed"); }
}

Result<void> GifWriter::AppendFrame(const RgbaFrameView& frame) noexcept {
    if (!impl_ || impl_->closed || impl_->failed)
        return MakeError(ErrorCode::InvalidState, "GIF writer is not accepting frames");
    if (!ValidFrame(frame, impl_->options))
        return MakeError(ErrorCode::InvalidArgument, "GIF frame must match its geometry and contain opaque RGBA pixels");
    try {
        ComPtr<IWICBitmap> bitmap;
        const auto bufferBytes = static_cast<UINT>(frame.rowStrideBytes * frame.height);
        HRESULT status = impl_->factory->CreateBitmapFromMemory(frame.width, frame.height,
            GUID_WICPixelFormat32bppRGBA, static_cast<UINT>(frame.rowStrideBytes), bufferBytes,
            const_cast<BYTE*>(frame.pixels.data()), &bitmap);
        if (SUCCEEDED(status) && !impl_->palette) {
            status = impl_->factory->CreatePalette(&impl_->palette);
            if (SUCCEEDED(status)) status = impl_->palette->InitializeFromBitmap(bitmap.Get(), 256, FALSE);
            if (SUCCEEDED(status)) status = impl_->encoder->SetPalette(impl_->palette.Get());
        }
        ComPtr<IWICFormatConverter> indexed;
        if (SUCCEEDED(status)) status = impl_->factory->CreateFormatConverter(&indexed);
        if (SUCCEEDED(status)) status = indexed->Initialize(bitmap.Get(), GUID_WICPixelFormat8bppIndexed,
            WICBitmapDitherTypeErrorDiffusion, impl_->palette.Get(), 0.0, WICBitmapPaletteTypeCustom);
        ComPtr<IWICBitmapFrameEncode> encoded;
        if (SUCCEEDED(status)) status = impl_->encoder->CreateNewFrame(&encoded, nullptr);
        if (SUCCEEDED(status)) status = encoded->Initialize(nullptr);
        if (SUCCEEDED(status)) status = encoded->SetSize(frame.width, frame.height);
        WICPixelFormatGUID format = GUID_WICPixelFormat8bppIndexed;
        if (SUCCEEDED(status)) status = encoded->SetPixelFormat(&format);
        if (SUCCEEDED(status) && format != GUID_WICPixelFormat8bppIndexed) status = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        // Opaque indexed source needs no generated transparency extension. Install the control
        // metadata before WriteSource, when WIC serializes the frame headers to the output stream.
        if (SUCCEEDED(status)) status = SetTiming(encoded.Get(), impl_->options.frameDelayCentiseconds);
        if (SUCCEEDED(status)) status = encoded->WriteSource(indexed.Get(), nullptr);
        if (SUCCEEDED(status)) status = encoded->Commit();
        if (FAILED(status)) {
            impl_->failed = true;
            return GifError("WIC GIF frame encoding failed", status);
        }
        ++impl_->count;
        return {};
    } catch (...) {
        impl_->failed = true;
        return MakeError(ErrorCode::Internal, "GIF frame allocation failed");
    }
}

Result<void> GifWriter::Finalize() noexcept {
    if (!impl_ || impl_->closed || impl_->failed || impl_->count == 0)
        return MakeError(ErrorCode::InvalidState, "GIF writer has no complete animation to publish");
    const HRESULT status = impl_->encoder->Commit();
    impl_->closed = true;
    impl_->encoder.Reset();
    impl_->stream.Reset();
    if (FAILED(status)) return GifError("WIC GIF completion failed", status);
    if (!::MoveFileExW(impl_->temporaryPath.c_str(), impl_->options.outputFile.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const auto error = ::GetLastError();
        return MakeError(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? ErrorCode::AlreadyExists : ErrorCode::Io,
            "GIF output could not be published without replacing an existing file", error);
    }
    impl_->published = true;
    return {};
}

std::uint64_t GifWriter::FramesWritten() const noexcept { return impl_ ? impl_->count : 0; }

}  // namespace aether::showcase
