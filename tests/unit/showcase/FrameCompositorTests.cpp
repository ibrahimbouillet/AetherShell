// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/FrameCompositor.h"
#include "showcase/Mp4Writer.h"
#include "showcase/Soundtrack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>
#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#include <wrl/client.h>

using namespace aether;
using namespace aether::showcase;

namespace {

SequenceConfig SmallConfig(std::uint32_t width = 2, std::uint32_t height = 2,
                           std::uint32_t outputWidth = 2, std::uint32_t outputHeight = 2) {
    SequenceConfig config;
    config.sourcePixels = {static_cast<double>(width), static_cast<double>(height)};
    config.outputPixels = {static_cast<double>(outputWidth), static_cast<double>(outputHeight)};
    config.dockBoundsPixels = {0.0, static_cast<double>(height - 1), static_cast<double>(width),
                               static_cast<double>(height)};
    config.visualStudioCodeIconPixels = {static_cast<double>(width) / 2.0, static_cast<double>(height) / 2.0};
    config.cursorStartPixels = {0.5, 0.5};
    config.timing.topMarginPixels = 0.0;
    return config;
}

class FakeFrameSource final : public ITemporalFrameSource {
public:
    std::uint32_t width = 2;
    std::uint32_t height = 2;
    std::size_t rowStride = 8;
    std::vector<std::uint8_t> bytes = {10, 20, 30, 255, 40, 50, 60, 255,
                                       70, 80, 90, 255, 100, 110, 120, 255};
    std::vector<double> timestamps;
    bool encodeCallNumber = false;
    bool failRead = false;

    Result<RgbaFrameView> ReadFrameAt(double timestampSeconds) noexcept override {
        if (failRead) return MakeError(ErrorCode::Unavailable, "fake capture frame unavailable");
        timestamps.push_back(timestampSeconds);
        if (encodeCallNumber) {
            const auto level = static_cast<std::uint8_t>(timestamps.size() * 10);
            std::fill(bytes.begin(), bytes.end(), level);
            for (std::size_t index = 3; index < bytes.size(); index += 4) bytes[index] = 255;
        }
        return RgbaFrameView{width, height, rowStride, bytes};
    }
};

class ComApartmentScope final {
public:
    ComApartmentScope() noexcept {
        const HRESULT result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
        valid_ = SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
        owns_ = SUCCEEDED(result);
    }
    ~ComApartmentScope() { if (owns_) ::CoUninitialize(); }
    [[nodiscard]] bool IsValid() const noexcept { return valid_; }

private:
    bool valid_ = false;
    bool owns_ = false;
};

class TemporaryFileCleanup final {
public:
    explicit TemporaryFileCleanup(std::wstring path) : path_(std::move(path)) {}
    ~TemporaryFileCleanup() { if (!path_.empty()) (void)::DeleteFileW(path_.c_str()); }

private:
    std::wstring path_;
};

class MediaFoundationScope final {
public:
    MediaFoundationScope() noexcept : status_(::MFStartup(MF_VERSION, MFSTARTUP_FULL)) {}
    ~MediaFoundationScope() { if (SUCCEEDED(status_)) (void)::MFShutdown(); }
    [[nodiscard]] bool IsValid() const noexcept { return SUCCEEDED(status_); }
private:
    HRESULT status_ = E_FAIL;
};

}  // namespace

AETHER_TEST("showcase compositor: identity resampling preserves captured pixels and row padding") {
    auto sequence = CinematicSequence::Create(SmallConfig());
    CHECK(sequence.ok());
    FakeFrameSource source;
    std::array<std::uint8_t, 20> output{};
    output.fill(0xCC);
    CompositeSettings settings;
    settings.motionBlurEnabled = false;

    auto result = FrameCompositor::Render(*sequence, source, 0.0, {}, output, 2, 2, 12, settings);
    CHECK(result.ok());
    CHECK_EQ(result->sourceFrameReads, 1U);
    CHECK_EQ(source.timestamps.size(), 1U);
    CHECK(std::equal(source.bytes.begin(), source.bytes.begin() + 8, output.begin()));
    CHECK(std::equal(source.bytes.begin() + 8, source.bytes.end(), output.begin() + 12));
    CHECK_EQ(output[8], 0xCC);
    CHECK_EQ(output[9], 0xCC);
    CHECK_EQ(output[10], 0xCC);
    CHECK_EQ(output[11], 0xCC);
}

AETHER_TEST("showcase compositor: motion blur averages the planned captured subframes") {
    auto sequence = CinematicSequence::Create(SmallConfig(1, 1, 1, 1));
    CHECK(sequence.ok());
    FakeFrameSource source;
    source.width = 1;
    source.height = 1;
    source.rowStride = 4;
    source.bytes = {0, 0, 0, 255};
    source.encodeCallNumber = true;
    std::array<std::uint8_t, 4> output{};

    auto result = FrameCompositor::Render(*sequence, source, 0.1, {}, output, 1, 1, 4);
    CHECK(result.ok());
    CHECK_EQ(result->sourceFrameReads, TemporalSampleSet::MaximumSamples);
    CHECK_EQ(source.timestamps.size(), TemporalSampleSet::MaximumSamples);
    CHECK(std::is_sorted(source.timestamps.begin(), source.timestamps.end()));
    CHECK(source.timestamps.front() < 0.1);
    CHECK(source.timestamps.back() > 0.1);
    CHECK_EQ(output[0], 45);
    CHECK_EQ(output[1], 45);
    CHECK_EQ(output[2], 45);
    CHECK_EQ(output[3], 255);
}

AETHER_TEST("showcase compositor: output scaling fits the entire captured desktop") {
    auto sequence = CinematicSequence::Create(SmallConfig(2, 2, 4, 4));
    CHECK(sequence.ok());
    CHECK_EQ(sequence->Evaluate(0.0).camera.zoom, 2.0);
    FakeFrameSource source;
    std::array<std::uint8_t, 64> output{};
    CompositeSettings settings;
    settings.motionBlurEnabled = false;

    auto result = FrameCompositor::Render(*sequence, source, 0.0, {}, output, 4, 4, 16, settings);
    CHECK(result.ok());
    CHECK_EQ(output[3], 255);
    CHECK_EQ(output[15], 255);
    CHECK_EQ(output[60 + 3], 255);
    CHECK_EQ(output[63], 255);
}

AETHER_TEST("showcase compositor: malformed captures and output geometry fail closed") {
    auto sequence = CinematicSequence::Create(SmallConfig());
    CHECK(sequence.ok());
    FakeFrameSource source;
    std::array<std::uint8_t, 16> output{};
    CompositeSettings settings;
    settings.motionBlurEnabled = false;

    source.width = 1;
    auto wrongSource = FrameCompositor::Render(*sequence, source, 0.0, {}, output, 2, 2, 8, settings);
    CHECK_FALSE(wrongSource.ok());
    CHECK_EQ(wrongSource.error().code, ErrorCode::Validation);

    source.width = 2;
    auto wrongOutput = FrameCompositor::Render(*sequence, source, 0.0, {}, output, 1, 2, 4, settings);
    CHECK_FALSE(wrongOutput.ok());
    CHECK_EQ(wrongOutput.error().code, ErrorCode::InvalidArgument);

    settings.sampleCount = TemporalSampleSet::MaximumSamples + 1;
    auto tooManySamples = FrameCompositor::Render(*sequence, source, 0.0, {}, output, 2, 2, 8, settings);
    CHECK_FALSE(tooManySamples.ok());
    CHECK_EQ(tooManySamples.error().code, ErrorCode::InvalidArgument);
}

AETHER_TEST("showcase compositor: provider failure leaves the destination untouched") {
    auto sequence = CinematicSequence::Create(SmallConfig());
    CHECK(sequence.ok());
    FakeFrameSource source;
    source.failRead = true;
    std::array<std::uint8_t, 16> output{};
    output.fill(0x42);
    CompositeSettings settings;
    settings.motionBlurEnabled = false;

    auto result = FrameCompositor::Render(*sequence, source, 0.0, {}, output, 2, 2, 8, settings);
    CHECK_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::Unavailable);
    CHECK(std::all_of(output.begin(), output.end(), [](std::uint8_t value) { return value == 0x42; }));
}

AETHER_TEST("showcase compositor: transparent source colors do not bleed through resampling") {
    auto sequence = CinematicSequence::Create(SmallConfig(1, 1, 1, 1));
    CHECK(sequence.ok());
    FakeFrameSource source;
    source.width = 1;
    source.height = 1;
    source.rowStride = 4;
    source.bytes = {255, 25, 15, 0};
    std::array<std::uint8_t, 4> output{};
    CompositeSettings settings;
    settings.motionBlurEnabled = false;

    auto result = FrameCompositor::Render(*sequence, source, 0.0, {}, output, 1, 1, 4, settings);
    CHECK(result.ok());
    CHECK_EQ(output[0], 0);
    CHECK_EQ(output[1], 0);
    CHECK_EQ(output[2], 0);
    CHECK_EQ(output[3], 0);
}

AETHER_TEST("showcase MP4 writer: invalid settings fail before opening an output file") {
    Mp4WriterOptions options;
    options.outputFile = L"invalid-output.bin";
    auto invalidPath = Mp4Writer::Create(options);
    CHECK_FALSE(invalidPath.ok());
    CHECK_EQ(invalidPath.error().code, ErrorCode::InvalidArgument);

    options.outputFile = L"invalid-output.mp4";
    options.framesPerSecond = 241;
    auto invalidRate = Mp4Writer::Create(options);
    CHECK_FALSE(invalidRate.ok());
    CHECK_EQ(invalidRate.error().code, ErrorCode::InvalidArgument);

    options.framesPerSecond = 30;
    options.width = 63;
    auto oddH264Width = Mp4Writer::Create(options);
    CHECK_FALSE(oddH264Width.ok());
    CHECK_EQ(oddH264Width.error().code, ErrorCode::InvalidArgument);

    Mp4Writer emptyWriter;
    CHECK_EQ(emptyWriter.FramesWritten(), 0U);
    CHECK_FALSE(emptyWriter.AppendFrame({}).ok());
    CHECK_FALSE(emptyWriter.Finalize().ok());

    options.outputFile = L"invalid-audio-settings.mp4";
    options.framesPerSecond = 30;
    options.width = 64;
    options.includeAudio = true;
    options.audioChannels = 1;
    auto invalidAudio = Mp4Writer::Create(options);
    CHECK_FALSE(invalidAudio.ok());
    CHECK_EQ(invalidAudio.error().code, ErrorCode::InvalidArgument);
}

AETHER_TEST("showcase soundtrack: original stereo score is bounded, repeatable, and fades cleanly") {
    auto first = CreateShowcaseSoundtrack(2.0, 0.9);
    auto second = CreateShowcaseSoundtrack(2.0, 0.9);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first->size(), 192'000U);
    CHECK(std::equal(first->begin(), first->end(), second->begin()));
    CHECK_EQ(first->front(), 0);
    CHECK_EQ(first->back(), 0);
    const auto peak = *std::max_element(first->begin(), first->end(), [](std::int16_t left, std::int16_t right) {
        return std::abs(static_cast<int>(left)) < std::abs(static_cast<int>(right));
    });
    CHECK(std::abs(static_cast<int>(peak)) <= 10'000);
    CHECK(std::any_of(first->begin(), first->end(), [](std::int16_t sample) { return sample != 0; }));
    CHECK_FALSE(CreateShowcaseSoundtrack(2.0, 0.9, 44'100).ok());
    CHECK_FALSE(CreateShowcaseSoundtrack(2.0, 2.1).ok());
}

AETHER_TEST("showcase soundtrack: motion cue follows explicit choreography bounds") {
    auto defaultTrack = CreateShowcaseSoundtrack(3.0, 2.5, 0.4, 1.4);
    auto shiftedTrack = CreateShowcaseSoundtrack(3.0, 2.5, 1.1, 2.1);
    auto noSweep = CreateShowcaseSoundtrack(3.0, 2.5, 0.0, 0.0);
    REQUIRE(defaultTrack.ok());
    REQUIRE(shiftedTrack.ok());
    REQUIRE(noSweep.ok());
    CHECK_EQ(defaultTrack->size(), shiftedTrack->size());
    CHECK_FALSE(std::equal(defaultTrack->begin(), defaultTrack->end(), shiftedTrack->begin()));
    CHECK_FALSE(std::equal(defaultTrack->begin(), defaultTrack->end(), noSweep->begin()));
    CHECK_FALSE(CreateShowcaseSoundtrack(3.0, 2.5, 2.0, 1.0).ok());
    CHECK_FALSE(CreateShowcaseSoundtrack(3.0, 2.5, -0.1, 1.0).ok());
    CHECK_FALSE(CreateShowcaseSoundtrack(3.0, 2.5, 0.1, 3.1).ok());
}

AETHER_TEST("showcase MP4 writer: native NV12 decode preserves top-down image orientation and padded source rows") {
    using Microsoft::WRL::ComPtr;
    ComApartmentScope apartment;
    REQUIRE(apartment.IsValid());
    wchar_t temporaryDirectory[MAX_PATH]{};
    wchar_t reservation[MAX_PATH]{};
    const DWORD directoryLength = ::GetTempPathW(MAX_PATH, temporaryDirectory);
    REQUIRE(directoryLength > 0 && directoryLength < MAX_PATH);
    REQUIRE(::GetTempFileNameW(temporaryDirectory, L"aor", 0, reservation) != 0);
    REQUIRE(::DeleteFileW(reservation) != FALSE);
    std::wstring outputPath(reservation);
    outputPath += L".mp4";
    TemporaryFileCleanup cleanup(outputPath);
    Mp4WriterOptions options;
    options.outputFile = outputPath;
    options.width = 128;
    options.height = 96;
    options.averageBitrate = 2'000'000;
    auto writer = Mp4Writer::Create(options);
    REQUIRE(writer.ok());
    constexpr std::size_t sourceStride = 128U * 4U + 32U;
    std::vector<std::uint8_t> pixels(sourceStride * 96U, 0xA5);
    for (std::uint32_t y = 0; y < 96; ++y) {
        const std::uint8_t level = y < 48 ? 230 : 30;
        for (std::uint32_t x = 0; x < 128; ++x) {
            const auto offset = static_cast<std::size_t>(y) * sourceStride + x * 4U;
            pixels[offset] = level;
            pixels[offset + 1U] = level;
            pixels[offset + 2U] = level;
            pixels[offset + 3U] = 255;
        }
    }
    for (std::uint32_t index = 0; index < 3; ++index)
        REQUIRE(writer->AppendFrame({128, 96, sourceStride, pixels}).ok());
    REQUIRE(writer->Finalize().ok());

    // NV12 luma is top-down by definition, unlike RGB32 system-memory buffers. Verify decoded
    // pixels rather than merely counting compressed samples or reinterpreting another RGB stride.
    MediaFoundationScope mediaFoundation;
    REQUIRE(mediaFoundation.IsValid());
    ComPtr<IMFSourceReader> reader;
    REQUIRE(SUCCEEDED(::MFCreateSourceReaderFromURL(outputPath.c_str(), nullptr, &reader)));
    constexpr auto videoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    ComPtr<IMFMediaType> requested;
    REQUIRE(SUCCEEDED(::MFCreateMediaType(&requested)));
    REQUIRE(SUCCEEDED(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)));
    REQUIRE(SUCCEEDED(requested->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)));
    REQUIRE(SUCCEEDED(reader->SetCurrentMediaType(videoStream, nullptr, requested.Get())));
    ComPtr<IMFSample> sample;
    for (std::uint32_t attempt = 0; attempt < 8 && !sample; ++attempt) {
        DWORD actualStream = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        REQUIRE(SUCCEEDED(reader->ReadSample(videoStream, 0, &actualStream, &flags, &timestamp, &sample)));
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
    }
    REQUIRE(sample != nullptr);
    ComPtr<IMFMediaType> decodedType;
    REQUIRE(SUCCEEDED(reader->GetCurrentMediaType(videoStream, &decodedType)));
    UINT32 decodedWidth = 0;
    UINT32 decodedHeight = 0;
    REQUIRE(SUCCEEDED(::MFGetAttributeSize(decodedType.Get(), MF_MT_FRAME_SIZE, &decodedWidth, &decodedHeight)));
    CHECK_EQ(decodedWidth, 128U);
    CHECK_EQ(decodedHeight, 96U);
    UINT32 storedStride = 0;
    LONG stride = 0;
    if (SUCCEEDED(decodedType->GetUINT32(MF_MT_DEFAULT_STRIDE, &storedStride)))
        stride = static_cast<LONG>(storedStride);
    else REQUIRE(SUCCEEDED(::MFGetStrideForBitmapInfoHeader(MFVideoFormat_NV12.Data1, decodedWidth, &stride)));
    REQUIRE(stride >= static_cast<LONG>(decodedWidth));
    ComPtr<IMFMediaBuffer> buffer;
    REQUIRE(SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)));
    BYTE* decoded = nullptr;
    DWORD decodedBytes = 0;
    REQUIRE(SUCCEEDED(buffer->Lock(&decoded, nullptr, &decodedBytes)));
    const auto lastLumaByte = static_cast<std::size_t>(decodedHeight - 1U) * static_cast<std::size_t>(stride) + decodedWidth;
    CHECK(lastLumaByte <= decodedBytes);
    if (lastLumaByte <= decodedBytes) {
        const auto top = decoded[12U * static_cast<std::size_t>(stride) + 32U];
        const auto bottom = decoded[83U * static_cast<std::size_t>(stride) + 32U];
        CHECK(top > 180U);
        CHECK(bottom < 80U);
        CHECK(static_cast<int>(top) - static_cast<int>(bottom) > 100);
    }
    CHECK(SUCCEEDED(buffer->Unlock()));
}

AETHER_TEST("showcase MP4 writer: genuine Media Foundation output finalizes without replacing files") {
    using Microsoft::WRL::ComPtr;
    ComApartmentScope apartment;
    if (!apartment.IsValid()) {
        CHECK(false);
        return;
    }

    wchar_t tempDirectory[MAX_PATH]{};
    wchar_t reservation[MAX_PATH]{};
    const DWORD directoryLength = ::GetTempPathW(MAX_PATH, tempDirectory);
    CHECK(directoryLength > 0 && directoryLength < MAX_PATH);
    if (directoryLength == 0 || directoryLength >= MAX_PATH || ::GetTempFileNameW(tempDirectory, L"ash", 0, reservation) == 0) {
        CHECK(false);
        return;
    }
    (void)::DeleteFileW(reservation);
    std::wstring outputPath(reservation);
    const auto extension = outputPath.find_last_of(L'.');
    if (extension != std::wstring::npos) outputPath.replace(extension, std::wstring::npos, L".mp4");
    TemporaryFileCleanup cleanup(outputPath);

    Mp4WriterOptions options;
    options.outputFile = outputPath;
    options.width = 64;
    options.height = 64;
    options.framesPerSecond = 30;
    options.averageBitrate = 500'000;
    options.includeAudio = true;
    options.audioSampleRate = 48'000;
    options.audioChannels = 2;
    options.audioBitrate = 192'000;

    HANDLE sentinel = ::CreateFileW(outputPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY, nullptr);
    CHECK(sentinel != INVALID_HANDLE_VALUE);
    if (sentinel == INVALID_HANDLE_VALUE) return;
    constexpr std::array<std::uint8_t, 4> marker{0x41, 0x53, 0x48, 0x32};
    DWORD written = 0;
    const BOOL markerWritten = ::WriteFile(sentinel, marker.data(), static_cast<DWORD>(marker.size()), &written, nullptr);
    ::CloseHandle(sentinel);
    CHECK(markerWritten != FALSE);
    CHECK_EQ(written, marker.size());
    if (markerWritten == FALSE) return;

    auto collision = Mp4Writer::Create(options);
    CHECK_FALSE(collision.ok());
    CHECK_EQ(collision.error().code, ErrorCode::AlreadyExists);
    HANDLE existing = ::CreateFileW(outputPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(existing != INVALID_HANDLE_VALUE);
    if (existing == INVALID_HANDLE_VALUE) return;
    std::array<std::uint8_t, marker.size()> preserved{};
    DWORD read = 0;
    const BOOL markerRead = ::ReadFile(existing, preserved.data(), static_cast<DWORD>(preserved.size()), &read, nullptr);
    ::CloseHandle(existing);
    CHECK(markerRead != FALSE);
    CHECK_EQ(read, marker.size());
    CHECK(std::equal(preserved.begin(), preserved.end(), marker.begin()));
    CHECK(::DeleteFileW(outputPath.c_str()) != FALSE);

    auto writerResult = Mp4Writer::Create(options);
    REQUIRE(writerResult.ok());
    auto writer = std::move(writerResult).value();

    constexpr std::uint32_t expectedFrames = 180;
    constexpr std::size_t audioFramesPerVideoFrame = 1'600;  // 48 kHz / 30 FPS.
    constexpr LONGLONG expectedDuration = 60'000'000;  // Six seconds in Media Foundation ticks.
    std::vector<std::uint8_t> pixels(64U * 64U * 4U, 255);
    std::vector<std::int16_t> audio(audioFramesPerVideoFrame * 2U);
    for (std::uint32_t frameIndex = 0; frameIndex < expectedFrames; ++frameIndex) {
        // Real export must advance both streams together: queuing all video before audio can
        // block the sink writer once its inter-stream buffering limit is reached.
        for (std::size_t sample = 0; sample < audioFramesPerVideoFrame; ++sample) {
            const auto absoluteSample = static_cast<std::size_t>(frameIndex) * audioFramesPerVideoFrame + sample;
            const double value = 0.16 * std::sin(6.283185307179586 * 440.0 * static_cast<double>(absoluteSample) / 48'000.0);
            const auto pcm = static_cast<std::int16_t>(std::lround(value * 32'767.0));
            audio[sample * 2U] = pcm;
            audio[sample * 2U + 1U] = pcm;
        }
        REQUIRE(writer.AppendAudio(audio).ok());
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
            pixels[offset] = static_cast<std::uint8_t>(frameIndex % 256U);
            pixels[offset + 1] = static_cast<std::uint8_t>(255U - frameIndex % 256U);
            pixels[offset + 2] = 40;
            pixels[offset + 3] = 255;
        }
        REQUIRE(writer.AppendFrame(RgbaFrameView{64, 64, 64U * 4U, pixels}).ok());
    }
    CHECK_EQ(writer.FramesWritten(), expectedFrames);
    CHECK(writer.Finalize().ok());

    const HRESULT mfStatus = ::MFStartup(MF_VERSION, MFSTARTUP_FULL);
    CHECK(SUCCEEDED(mfStatus));
    ComPtr<IMFSourceReader> reader;
    const HRESULT readerStatus = SUCCEEDED(mfStatus)
                                     ? ::MFCreateSourceReaderFromURL(outputPath.c_str(), nullptr, &reader)
                                     : mfStatus;
    CHECK(SUCCEEDED(readerStatus));
    if (SUCCEEDED(readerStatus)) {
        constexpr auto videoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        ComPtr<IMFMediaType> nativeType;
        HRESULT streamStatus = reader->GetNativeMediaType(videoStream, 0, &nativeType);
        GUID subtype{};
        if (SUCCEEDED(streamStatus)) streamStatus = nativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
        CHECK(SUCCEEDED(streamStatus));
        if (SUCCEEDED(streamStatus)) {
            CHECK(subtype == MFVideoFormat_H264);
            std::uint32_t streamSamples = 0;
            bool reachedVideoEnd = false;
            for (std::uint32_t attempt = 0; attempt < expectedFrames + 8; ++attempt) {
                DWORD streamIndex = 0;
                DWORD flags = 0;
                LONGLONG timestamp = 0;
                ComPtr<IMFSample> sample;
                const HRESULT sampleStatus = reader->ReadSample(videoStream, 0,
                                                                 &streamIndex, &flags, &timestamp, &sample);
                CHECK(SUCCEEDED(sampleStatus));
                if (FAILED(sampleStatus)) break;
                if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) { reachedVideoEnd = true; break; }
                if (sample) ++streamSamples;
            }
            CHECK(reachedVideoEnd);
            CHECK_EQ(streamSamples, expectedFrames);
        }
        constexpr auto audioReaderStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
        ComPtr<IMFMediaType> nativeAudioType;
        HRESULT audioStatus = reader->GetNativeMediaType(audioReaderStream, 0, &nativeAudioType);
        GUID audioSubtype{};
        if (SUCCEEDED(audioStatus)) audioStatus = nativeAudioType->GetGUID(MF_MT_SUBTYPE, &audioSubtype);
        CHECK(SUCCEEDED(audioStatus));
        if (SUCCEEDED(audioStatus)) {
            CHECK(audioSubtype == MFAudioFormat_AAC);
            UINT32 sampleRate = 0;
            UINT32 channels = 0;
            CHECK(SUCCEEDED(nativeAudioType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate)));
            CHECK(SUCCEEDED(nativeAudioType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)));
            CHECK_EQ(sampleRate, 48'000U);
            CHECK_EQ(channels, 2U);
            // Seek explicitly so this checks the whole audio stream after the full video pass.
            PROPVARIANT start{};
            start.vt = VT_I8;
            start.hVal.QuadPart = 0;
            audioStatus = reader->SetCurrentPosition(GUID_NULL, start);
            CHECK(SUCCEEDED(audioStatus));
            std::uint32_t audioSamples = 0;
            LONGLONG audioEnd = 0;
            bool reachedAudioEnd = false;
            for (std::uint32_t attempt = 0; SUCCEEDED(audioStatus) && attempt < 400; ++attempt) {
                DWORD audioStream = 0;
                DWORD audioFlags = 0;
                LONGLONG audioTimestamp = 0;
                ComPtr<IMFSample> audioSample;
                audioStatus = reader->ReadSample(audioReaderStream, 0,
                    &audioStream, &audioFlags, &audioTimestamp, &audioSample);
                CHECK(SUCCEEDED(audioStatus));
                if (FAILED(audioStatus)) break;
                if ((audioFlags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) { reachedAudioEnd = true; break; }
                if (audioSample) {
                    LONGLONG duration = 0;
                    DWORD bytes = 0;
                    CHECK(SUCCEEDED(audioSample->GetSampleDuration(&duration)));
                    CHECK(SUCCEEDED(audioSample->GetTotalLength(&bytes)));
                    CHECK(duration > 0);
                    CHECK(bytes > 0);
                    audioEnd = std::max(audioEnd, audioTimestamp + duration);
                    ++audioSamples;
                }
            }
            CHECK(reachedAudioEnd);
            CHECK(audioSamples > 200U);
            // AAC packets may add encoder priming/padding, bounded here to 100 ms.
            CHECK(audioEnd >= expectedDuration - 1'000'000);
            CHECK(audioEnd <= expectedDuration + 1'000'000);
        }
    }
    reader.Reset();
    if (SUCCEEDED(mfStatus)) CHECK(SUCCEEDED(::MFShutdown()));

    const DWORD attributes = ::GetFileAttributesW(outputPath.c_str());
    CHECK(attributes != INVALID_FILE_ATTRIBUTES);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        WIN32_FILE_ATTRIBUTE_DATA file{};
        CHECK(::GetFileAttributesExW(outputPath.c_str(), GetFileExInfoStandard, &file) != FALSE);
        const auto byteCount = (static_cast<std::uint64_t>(file.nFileSizeHigh) << 32) | file.nFileSizeLow;
        CHECK(byteCount > 0);
    }
    CHECK(::DeleteFileW(outputPath.c_str()) != FALSE);
}
