// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/Mp4Writer.h"

#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <limits>
#include <utility>

namespace aether::showcase {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint64_t kMediaFoundationTicksPerSecond = 10'000'000;

Error PlatformError(const char* message, HRESULT status) {
    return MakeError(ErrorCode::Platform, message, static_cast<std::int64_t>(status));
}

bool ValidFrame(const RgbaFrameView& frame, std::uint32_t expectedWidth,
                std::uint32_t expectedHeight, std::size_t& rowBytes,
                std::size_t& bufferBytes) noexcept {
    constexpr std::size_t kBytesPerPixel = 4;
    if (frame.width != expectedWidth || frame.height != expectedHeight || frame.width == 0 || frame.height == 0 ||
        frame.width > std::numeric_limits<std::size_t>::max() / kBytesPerPixel) {
        return false;
    }
    rowBytes = static_cast<std::size_t>(frame.width) * kBytesPerPixel;
    if (frame.rowStrideBytes < rowBytes ||
        (frame.height > 1 && frame.rowStrideBytes >
             (std::numeric_limits<std::size_t>::max() - rowBytes) / static_cast<std::size_t>(frame.height - 1))) {
        return false;
    }
    if (frame.height > std::numeric_limits<std::size_t>::max() / rowBytes) return false;
    bufferBytes = rowBytes * frame.height;
    const auto required = frame.rowStrideBytes * static_cast<std::size_t>(frame.height - 1) + rowBytes;
    return frame.pixels.size() >= required && bufferBytes <= std::numeric_limits<DWORD>::max();
}

bool ValidAudioOptions(const Mp4WriterOptions& options) noexcept {
    return !options.includeAudio || (options.audioSampleRate >= 8'000 && options.audioSampleRate <= 192'000 &&
        options.audioChannels == 2 && options.audioBitrate >= 32'000 && options.audioBitrate <= 512'000);
}

bool IsMp4Path(const std::wstring& value) noexcept {
    if (value.size() < 4) return false;
    const auto offset = value.size() - 4;
    return std::towlower(value[offset]) == L'.' && std::towlower(value[offset + 1]) == L'm' &&
           std::towlower(value[offset + 2]) == L'p' && std::towlower(value[offset + 3]) == L'4';
}

std::wstring MakeTemporarySiblingPath(const std::wstring& outputPath) {
    GUID identifier{};
    if (FAILED(::CoCreateGuid(&identifier))) return {};
    wchar_t text[40]{};
    if (::StringFromGUID2(identifier, text, static_cast<int>(sizeof(text) / sizeof(text[0]))) == 0) return {};
    const auto separator = outputPath.find_last_of(L"\\/");
    const std::wstring directory = separator == std::wstring::npos ? std::wstring{} : outputPath.substr(0, separator + 1);
    return directory + L".aether-showcase-" + text + L".mp4";
}

}  // namespace

struct Mp4Writer::Impl final {
    Mp4WriterOptions options;
    std::wstring temporaryOutputFile;
    ComPtr<IMFSinkWriter> writer;
    DWORD streamIndex = 0;
    DWORD audioStreamIndex = 0;
    std::uint64_t frameCount = 0;
    std::uint64_t audioSampleCount = 0;
    bool mediaFoundationStarted = false;
    bool finalized = false;
    bool published = false;

    void Cleanup() noexcept {
        if (writer && !finalized) (void)writer->Finalize();
        writer.Reset();
        if (mediaFoundationStarted) {
            (void)MFShutdown();
            mediaFoundationStarted = false;
        }
        if (!published && !temporaryOutputFile.empty()) (void)::DeleteFileW(temporaryOutputFile.c_str());
        finalized = true;
    }

    ~Impl() { Cleanup(); }
};

Mp4Writer::Mp4Writer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Mp4Writer::Mp4Writer() noexcept = default;
Mp4Writer::~Mp4Writer() = default;
Mp4Writer::Mp4Writer(Mp4Writer&& other) noexcept = default;
Mp4Writer& Mp4Writer::operator=(Mp4Writer&& other) noexcept = default;

Result<Mp4Writer> Mp4Writer::Create(Mp4WriterOptions options) noexcept {
    const auto maxBytes = std::numeric_limits<std::uint64_t>::max();
    const auto pixels = options.height != 0 && static_cast<std::uint64_t>(options.width) <= maxBytes / options.height
                            ? static_cast<std::uint64_t>(options.width) * options.height
                            : maxBytes;
    const auto frameBytes = pixels <= maxBytes / 4ULL ? pixels * 4ULL : maxBytes;
    if (options.outputFile.empty() || !IsMp4Path(options.outputFile) || options.width < 16 || options.height < 16 ||
        options.width > 16'384 || options.height > 16'384 || options.width % 2 != 0 || options.height % 2 != 0 ||
        options.framesPerSecond == 0 || options.framesPerSecond > 240 || options.averageBitrate < 100'000 ||
        options.averageBitrate > 100'000'000 || frameBytes > std::numeric_limits<DWORD>::max() ||
        !ValidAudioOptions(options)) {
        return MakeError(ErrorCode::InvalidArgument, "MP4 writer path or video settings are invalid");
    }
    const DWORD attributes = ::GetFileAttributesW(options.outputFile.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return MakeError(ErrorCode::AlreadyExists, "showcase output already exists; choose a new file name");
    }
    const auto fileError = ::GetLastError();
    if (fileError != ERROR_FILE_NOT_FOUND && fileError != ERROR_PATH_NOT_FOUND) {
        return MakeError(ErrorCode::AccessDenied, "showcase output location is unavailable", fileError);
    }

    try {
        auto impl = std::make_unique<Impl>();
        impl->options = std::move(options);
        impl->temporaryOutputFile = MakeTemporarySiblingPath(impl->options.outputFile);
        if (impl->temporaryOutputFile.empty() ||
            ::GetFileAttributesW(impl->temporaryOutputFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return MakeError(ErrorCode::Unavailable, "MP4 writer could not allocate a private temporary output");
        }
        HRESULT status = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(status)) return PlatformError("Media Foundation could not start", status);
        impl->mediaFoundationStarted = true;

        ComPtr<IMFAttributes> attributesStore;
        status = ::MFCreateAttributes(&attributesStore, 2);
        if (SUCCEEDED(status)) {
            // Hardware transforms may be used when the OS offers them; the caller still measures
            // the actual encode path and must not infer 60-FPS capability from this setting.
            status = attributesStore->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        }
        if (SUCCEEDED(status)) {
            status = ::MFCreateSinkWriterFromURL(impl->temporaryOutputFile.c_str(), nullptr,
                                                 attributesStore.Get(), &impl->writer);
        }
        if (FAILED(status)) return PlatformError("Media Foundation could not create the MP4 writer", status);

        ComPtr<IMFMediaType> outputType;
        status = ::MFCreateMediaType(&outputType);
        if (SUCCEEDED(status)) status = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(status)) status = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (SUCCEEDED(status)) status = outputType->SetUINT32(MF_MT_AVG_BITRATE, impl->options.averageBitrate);
        if (SUCCEEDED(status)) status = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(status)) status = ::MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE,
                                                             impl->options.width, impl->options.height);
        if (SUCCEEDED(status)) status = ::MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE,
                                                              impl->options.framesPerSecond, 1);
        if (SUCCEEDED(status)) status = ::MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(status)) status = outputType->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
        if (SUCCEEDED(status)) status = outputType->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
        if (SUCCEEDED(status)) status = outputType->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
        if (SUCCEEDED(status)) status = outputType->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
        if (SUCCEEDED(status)) status = impl->writer->AddStream(outputType.Get(), &impl->streamIndex);
        if (FAILED(status)) return PlatformError("Media Foundation could not configure H.264 output", status);

        ComPtr<IMFMediaType> inputType;
        status = ::MFCreateMediaType(&inputType);
        if (SUCCEEDED(status)) status = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(status)) status = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(status)) status = inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(status)) status = ::MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE,
                                                             impl->options.width, impl->options.height);
        if (SUCCEEDED(status)) status = ::MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE,
                                                              impl->options.framesPerSecond, 1);
        if (SUCCEEDED(status)) status = ::MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(status)) status = impl->writer->SetInputMediaType(impl->streamIndex, inputType.Get(), nullptr);
        if (FAILED(status)) return PlatformError("Media Foundation could not configure RGBA frame input", status);

        if (impl->options.includeAudio) {
            ComPtr<IMFMediaType> audioOutputType;
            status = ::MFCreateMediaType(&audioOutputType);
            if (SUCCEEDED(status)) status = audioOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(status)) status = audioOutputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            if (SUCCEEDED(status)) status = audioOutputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, impl->options.audioChannels);
            if (SUCCEEDED(status)) status = audioOutputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, impl->options.audioSampleRate);
            if (SUCCEEDED(status)) status = audioOutputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(status)) status = audioOutputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, impl->options.audioBitrate / 8U);
            if (SUCCEEDED(status)) status = audioOutputType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
            if (SUCCEEDED(status)) status = audioOutputType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
            if (SUCCEEDED(status)) status = impl->writer->AddStream(audioOutputType.Get(), &impl->audioStreamIndex);
            if (FAILED(status)) return PlatformError("Media Foundation could not configure AAC output", status);

            ComPtr<IMFMediaType> audioInputType;
            status = ::MFCreateMediaType(&audioInputType);
            if (SUCCEEDED(status)) status = audioInputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(status)) status = audioInputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(status)) status = audioInputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, impl->options.audioChannels);
            if (SUCCEEDED(status)) status = audioInputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, impl->options.audioSampleRate);
            if (SUCCEEDED(status)) status = audioInputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(status)) status = audioInputType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, impl->options.audioChannels * 2U);
            if (SUCCEEDED(status)) status = audioInputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                                                        impl->options.audioSampleRate * impl->options.audioChannels * 2U);
            if (SUCCEEDED(status)) status = audioInputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            if (SUCCEEDED(status)) status = impl->writer->SetInputMediaType(impl->audioStreamIndex, audioInputType.Get(), nullptr);
            if (FAILED(status)) return PlatformError("Media Foundation could not configure stereo PCM input", status);
        }

        status = impl->writer->BeginWriting();
        if (FAILED(status)) return PlatformError("Media Foundation could not begin MP4 writing", status);
        return Mp4Writer(std::move(impl));
    } catch (...) {
        return MakeError(ErrorCode::Internal, "MP4 writer initialization failed without exposing the output path");
    }
}

Result<void> Mp4Writer::AppendFrame(const RgbaFrameView& frame) noexcept {
    if (!impl_ || !impl_->writer || impl_->finalized) {
        return MakeError(ErrorCode::InvalidState, "MP4 writer is not accepting frames");
    }
    std::size_t rowBytes = 0;
    std::size_t bufferBytes = 0;
    if (!ValidFrame(frame, impl_->options.width, impl_->options.height, rowBytes, bufferBytes)) {
        return MakeError(ErrorCode::Validation, "MP4 frame dimensions or pixel buffer are invalid");
    }
    if (impl_->frameCount >= (std::numeric_limits<std::uint64_t>::max() / kMediaFoundationTicksPerSecond)) {
        return MakeError(ErrorCode::InvalidState, "MP4 frame timeline exceeds its supported duration");
    }
    const auto scaledIndex = impl_->frameCount * kMediaFoundationTicksPerSecond;
    const auto timestamp = scaledIndex / impl_->options.framesPerSecond;
    const auto nextScaledIndex = (impl_->frameCount + 1) * kMediaFoundationTicksPerSecond;
    const auto nextTimestamp = nextScaledIndex / impl_->options.framesPerSecond;
    if (timestamp > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()) ||
        nextTimestamp > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
        return MakeError(ErrorCode::InvalidState, "MP4 frame timeline exceeds its supported duration");
    }

    ComPtr<IMFMediaBuffer> buffer;
    HRESULT status = ::MFCreateMemoryBuffer(static_cast<DWORD>(bufferBytes), &buffer);
    if (FAILED(status)) return PlatformError("MP4 frame buffer allocation failed", status);
    BYTE* destination = nullptr;
    DWORD maximumLength = 0;
    status = buffer->Lock(&destination, &maximumLength, nullptr);
    if (FAILED(status)) return PlatformError("MP4 frame buffer could not be locked", status);
    if (maximumLength < bufferBytes) {
        (void)buffer->Unlock();
        return MakeError(ErrorCode::Unavailable, "MP4 frame buffer is smaller than the requested image");
    }
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        // Frame views are top-down RGBA; Media Foundation's system-memory RGB32 input is
        // bottom-up BGRX. Reverse physical row order as well as channel order. A compressed
        // H.264 stream has no RGB stride flag with which a player could correct a wrong order.
        const auto sourceY = frame.height - 1U - y;
        const auto* sourceRow = frame.pixels.data() + static_cast<std::size_t>(sourceY) * frame.rowStrideBytes;
        auto* targetRow = destination + static_cast<std::size_t>(y) * rowBytes;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto sourceOffset = static_cast<std::size_t>(x) * 4;
            const auto targetOffset = static_cast<std::size_t>(x) * 4;
            targetRow[targetOffset] = sourceRow[sourceOffset + 2];
            targetRow[targetOffset + 1] = sourceRow[sourceOffset + 1];
            targetRow[targetOffset + 2] = sourceRow[sourceOffset];
            targetRow[targetOffset + 3] = 0;
        }
    }
    status = buffer->SetCurrentLength(static_cast<DWORD>(bufferBytes));
    const HRESULT unlockStatus = buffer->Unlock();
    if (FAILED(status)) return PlatformError("MP4 frame buffer length could not be set", status);
    if (FAILED(unlockStatus)) return PlatformError("MP4 frame buffer could not be released", unlockStatus);

    ComPtr<IMFSample> sample;
    status = ::MFCreateSample(&sample);
    if (SUCCEEDED(status)) status = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(status)) status = sample->SetSampleTime(static_cast<LONGLONG>(timestamp));
    if (SUCCEEDED(status)) status = sample->SetSampleDuration(static_cast<LONGLONG>(nextTimestamp - timestamp));
    if (SUCCEEDED(status)) status = impl_->writer->WriteSample(impl_->streamIndex, sample.Get());
    if (FAILED(status)) return PlatformError("Media Foundation could not encode a frame", status);
    ++impl_->frameCount;
    return Ok();
}

Result<void> Mp4Writer::AppendAudio(std::span<const std::int16_t> interleavedPcm) noexcept {
    if (!impl_ || !impl_->writer || impl_->finalized || !impl_->options.includeAudio)
        return MakeError(ErrorCode::InvalidState, "MP4 writer is not accepting audio samples");
    if (interleavedPcm.empty() || interleavedPcm.size() % impl_->options.audioChannels != 0 ||
        interleavedPcm.size_bytes() > std::numeric_limits<DWORD>::max())
        return MakeError(ErrorCode::Validation, "MP4 audio sample buffer is invalid");
    const auto frameCount = interleavedPcm.size() / impl_->options.audioChannels;
    if (frameCount > std::numeric_limits<std::uint64_t>::max() - impl_->audioSampleCount ||
        impl_->audioSampleCount + frameCount > std::numeric_limits<std::uint64_t>::max() / kMediaFoundationTicksPerSecond)
        return MakeError(ErrorCode::InvalidState, "MP4 audio timeline exceeds its supported duration");
    const auto start = impl_->audioSampleCount * kMediaFoundationTicksPerSecond / impl_->options.audioSampleRate;
    const auto endSamples = impl_->audioSampleCount + frameCount;
    const auto end = endSamples * kMediaFoundationTicksPerSecond / impl_->options.audioSampleRate;

    ComPtr<IMFMediaBuffer> buffer;
    HRESULT status = ::MFCreateMemoryBuffer(static_cast<DWORD>(interleavedPcm.size_bytes()), &buffer);
    if (FAILED(status)) return PlatformError("MP4 audio buffer allocation failed", status);
    BYTE* destination = nullptr;
    DWORD maximumLength = 0;
    status = buffer->Lock(&destination, &maximumLength, nullptr);
    if (FAILED(status)) return PlatformError("MP4 audio buffer could not be locked", status);
    if (maximumLength < interleavedPcm.size_bytes()) {
        (void)buffer->Unlock();
        return MakeError(ErrorCode::Unavailable, "MP4 audio buffer is smaller than the requested samples");
    }
    std::memcpy(destination, interleavedPcm.data(), interleavedPcm.size_bytes());
    status = buffer->SetCurrentLength(static_cast<DWORD>(interleavedPcm.size_bytes()));
    const HRESULT unlockStatus = buffer->Unlock();
    if (FAILED(status)) return PlatformError("MP4 audio sample length could not be set", status);
    if (FAILED(unlockStatus)) return PlatformError("MP4 audio buffer could not be released", unlockStatus);

    ComPtr<IMFSample> sample;
    status = ::MFCreateSample(&sample);
    if (SUCCEEDED(status)) status = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(status)) status = sample->SetSampleTime(static_cast<LONGLONG>(start));
    if (SUCCEEDED(status)) status = sample->SetSampleDuration(static_cast<LONGLONG>(end - start));
    if (SUCCEEDED(status)) status = impl_->writer->WriteSample(impl_->audioStreamIndex, sample.Get());
    if (FAILED(status)) return PlatformError("Media Foundation could not encode audio samples", status);
    impl_->audioSampleCount = endSamples;
    return Ok();
}

Result<void> Mp4Writer::Finalize() noexcept {
    if (!impl_) return MakeError(ErrorCode::InvalidState, "MP4 writer is not initialized");
    if (impl_->finalized) return Ok();
    if (impl_->options.includeAudio && impl_->audioSampleCount == 0)
        return MakeError(ErrorCode::Validation, "MP4 output is missing its required audio samples");
    HRESULT status = S_OK;
    if (impl_->writer) status = impl_->writer->Finalize();
    impl_->writer.Reset();
    if (impl_->mediaFoundationStarted) {
        const HRESULT shutdownStatus = ::MFShutdown();
        impl_->mediaFoundationStarted = false;
        if (SUCCEEDED(status) && FAILED(shutdownStatus)) status = shutdownStatus;
    }
    impl_->finalized = true;
    if (FAILED(status)) {
        if (!impl_->temporaryOutputFile.empty()) (void)::DeleteFileW(impl_->temporaryOutputFile.c_str());
        return PlatformError("Media Foundation could not finalize the MP4 file", status);
    }
    if (impl_->frameCount == 0) {
        if (!impl_->temporaryOutputFile.empty()) (void)::DeleteFileW(impl_->temporaryOutputFile.c_str());
        return MakeError(ErrorCode::Validation, "MP4 output contains no video frames");
    }
    if (!::MoveFileExW(impl_->temporaryOutputFile.c_str(), impl_->options.outputFile.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const HRESULT moveStatus = HRESULT_FROM_WIN32(::GetLastError());
        (void)::DeleteFileW(impl_->temporaryOutputFile.c_str());
        return PlatformError("MP4 output could not be published without replacing an existing file", moveStatus);
    }
    impl_->published = true;
    return Ok();
}

std::uint64_t Mp4Writer::FramesWritten() const noexcept { return impl_ ? impl_->frameCount : 0; }

}  // namespace aether::showcase
