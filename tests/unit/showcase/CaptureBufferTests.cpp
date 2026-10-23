// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/CaptureBuffer.h"
#include "showcase/FrameArchive.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace aether;
using namespace aether::showcase;

namespace {
class BufferArchivePath final {
public:
    BufferArchivePath() {
        wchar_t directory[MAX_PATH]{};
        wchar_t reserved[MAX_PATH]{};
        const DWORD length = ::GetTempPathW(MAX_PATH, directory);
        if (length > 0 && length < MAX_PATH && ::GetTempFileNameW(directory, L"asb", 0, reserved)) {
            (void)::DeleteFileW(reserved);
            path_ = reserved;
        }
    }
    ~BufferArchivePath() { if (!path_.empty()) (void)::DeleteFileW(path_.c_str()); }
    const std::wstring& Get() const noexcept { return path_; }
private:
    std::wstring path_;
};
}  // namespace

AETHER_TEST("showcase capture buffer: validates dimensions and total memory budget before capture") {
    CHECK_FALSE(CaptureBuffer::Create(0, 32).ok());
    CHECK_FALSE(CaptureBuffer::Create(32, 0).ok());
    CHECK_FALSE(CaptureBuffer::Create(16'385, 32).ok());
    CHECK_FALSE(CaptureBuffer::Create(32, 32, 0).ok());
    CHECK_FALSE(CaptureBuffer::Create(32, 32, 4'096).ok());
    CHECK_FALSE(CaptureBuffer::Create(32, 32, CaptureBuffer::MaximumBytes + 1).ok());
    CHECK_FALSE(CaptureBuffer::Create(16'384, 16'384).ok());
    auto buffer = CaptureBuffer::Create(32, 32, 65'536);
    REQUIRE(buffer.ok());
    CHECK_EQ(buffer->FrameCount(), 0U);
    CHECK_EQ(buffer->Width(), 32U);
    CHECK_EQ(buffer->Height(), 32U);
    CHECK(buffer->MemoryBytes() <= 65'536U);
}

AETHER_TEST("showcase capture buffer: preserves strict timestamps and rejects malformed frames") {
    auto buffer = CaptureBuffer::Create(2, 1, 65'536);
    REQUIRE(buffer.ok());
    const std::array<std::uint8_t, 8> pixels{1, 2, 3, 255, 4, 5, 6, 0};
    const RgbaFrameView frame{2, 1, 8, pixels};
    CHECK_FALSE(buffer->Append(-1, frame).ok());
    CHECK_FALSE(buffer->Append(std::numeric_limits<double>::infinity(), frame).ok());
    CHECK_FALSE(buffer->Append(std::numeric_limits<double>::quiet_NaN(), frame).ok());
    CHECK_FALSE(buffer->Append(0, {1, 1, 4, pixels}).ok());
    CHECK_FALSE(buffer->Append(0, {2, 1, 7, pixels}).ok());
    CHECK_FALSE(buffer->Append(0, {2, 1, 8, std::span<const std::uint8_t>(pixels).first(7)}).ok());
    REQUIRE(buffer->Append(0.5, frame).ok());
    CHECK_FALSE(buffer->Append(0.5, frame).ok());
    CHECK_FALSE(buffer->Append(0.4, frame).ok());
    REQUIRE(buffer->Append(0.6, frame).ok());
    CHECK_EQ(buffer->FrameCount(), 2U);
    CaptureBuffer empty;
    CHECK_FALSE(empty.Append(0, frame).ok());
}

AETHER_TEST("showcase capture buffer: identical images use metadata rather than repeated image storage") {
    auto buffer = CaptureBuffer::Create(64, 64, 131'072);
    REQUIRE(buffer.ok());
    const std::vector<std::uint8_t> pixels(64U * 64U * 4U, 127);
    REQUIRE(buffer->Append(0, {64, 64, 256, pixels}).ok());
    const auto firstBytes = buffer->StoredBytes();
    for (std::uint32_t index = 1; index <= 100; ++index)
        REQUIRE(buffer->Append(static_cast<double>(index) / 60, {64, 64, 256, pixels}).ok());
    CHECK_EQ(buffer->FrameCount(), 101U);
    CHECK(buffer->StoredBytes() - firstBytes <= 100U * 32U);
    CHECK(buffer->MemoryBytes() <= 131'072U);
}

AETHER_TEST("showcase capture buffer: full first frame and changed edge tiles flush losslessly") {
    constexpr std::uint32_t width = 66, height = 35;
    constexpr std::size_t stride = width * 4U + 12U;
    auto buffer = CaptureBuffer::Create(width, height, 131'072);
    REQUIRE(buffer.ok());
    std::vector<std::uint8_t> padded(stride * height, 0xCC);
    std::vector<std::vector<std::uint8_t>> expected;
    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        if (frame == 0) {
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                    for (std::uint32_t channel = 0; channel < 4; ++channel)
                        padded[static_cast<std::size_t>(y) * stride + x * 4U + channel] =
                            static_cast<std::uint8_t>((y * 17U + x * 3U + channel) % 256U);
        } else if (frame == 2) {
            padded[7U * stride + 6U * 4U] ^= 0x7F;  // Changes one complete 32x32 tile.
        } else if (frame == 3) {
            padded[34U * stride + 65U * 4U + 3U] ^= 0xFF;  // Bottom-right tile is only 2x3.
        }
        std::vector<std::uint8_t> tight(static_cast<std::size_t>(width) * height * 4U);
        for (std::uint32_t y = 0; y < height; ++y)
            std::copy_n(padded.begin() + static_cast<std::ptrdiff_t>(y * stride), width * 4U,
                tight.begin() + static_cast<std::ptrdiff_t>(y * width * 4U));
        expected.push_back(std::move(tight));
        const auto previousBytes = buffer->StoredBytes();
        REQUIRE(buffer->Append(static_cast<double>(frame) / 60, {width, height, stride, padded}).ok());
        if (frame == 1) CHECK(buffer->StoredBytes() - previousBytes <= 32U);
        if (frame == 3) CHECK(buffer->StoredBytes() - previousBytes <= 64U);
    }
    BufferArchivePath path;
    REQUIRE_FALSE(path.Get().empty());
    auto archive = FrameArchive::Create(path.Get(), width, height, 60);
    REQUIRE(archive.ok());
    const auto bytesBeforeFlush = buffer->MemoryBytes();
    REQUIRE(buffer->FlushTo(*archive).ok());
    CHECK_EQ(buffer->MemoryBytes(), bytesBeforeFlush);
    CHECK_FALSE(buffer->Append(1, {width, height, stride, padded}).ok());
    REQUIRE(archive->Finalize().ok());
    CHECK_EQ(archive->FrameCount(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK_EQ(archive->CaptureTimes()[index], static_cast<double>(index) / 60);
        const auto frame = archive->ReadFrameAt(static_cast<double>(index) / 60);
        REQUIRE(frame.ok());
        CHECK(std::equal(frame->pixels.begin(), frame->pixels.end(), expected[index].begin(), expected[index].end()));
    }
}

AETHER_TEST("showcase capture buffer: stops at bounded memory and never exports a failed capture") {
    constexpr std::uint64_t limit = 65'536;
    auto buffer = CaptureBuffer::Create(32, 32, limit);
    REQUIRE(buffer.ok());
    std::vector<std::uint8_t> pixels(32U * 32U * 4U);
    bool reachedLimit = false;
    for (std::uint32_t index = 0; index < 100; ++index) {
        std::fill(pixels.begin(), pixels.end(), static_cast<std::uint8_t>(index));
        const auto count = buffer->FrameCount();
        const auto result = buffer->Append(static_cast<double>(index) / 60, {32, 32, 128, pixels});
        CHECK(buffer->MemoryBytes() <= limit);
        if (!result) {
            CHECK_EQ(result.error().code, ErrorCode::Unavailable);
            CHECK_EQ(buffer->FrameCount(), count);
            reachedLimit = true;
            break;
        }
    }
    CHECK(reachedLimit);
    CHECK_FALSE(buffer->Append(3, {32, 32, 128, pixels}).ok());
    BufferArchivePath path;
    auto archive = FrameArchive::Create(path.Get(), 32, 32, 60);
    REQUIRE(archive.ok());
    CHECK_FALSE(buffer->FlushTo(*archive).ok());
    CHECK_EQ(archive->FrameCount(), 0U);
}

AETHER_TEST("showcase capture buffer: rejects nonempty or mismatched archives without sealing capture") {
    auto buffer = CaptureBuffer::Create(1, 1, 65'536);
    REQUIRE(buffer.ok());
    const std::array<std::uint8_t, 4> pixel{10, 20, 30, 255};
    BufferArchivePath wrongPath, occupiedPath;
    auto wrong = FrameArchive::Create(wrongPath.Get(), 2, 1, 60);
    auto occupied = FrameArchive::Create(occupiedPath.Get(), 1, 1, 60);
    REQUIRE(wrong.ok()); REQUIRE(occupied.ok());
    CHECK_FALSE(buffer->FlushTo(*occupied).ok());
    REQUIRE(buffer->Append(0, {1, 1, 4, pixel}).ok());
    CHECK_FALSE(buffer->FlushTo(*wrong).ok());
    REQUIRE(occupied->Append(0, {1, 1, 4, pixel}).ok());
    CHECK_FALSE(buffer->FlushTo(*occupied).ok());
    REQUIRE(buffer->Append(0.1, {1, 1, 4, pixel}).ok());
    CHECK_EQ(buffer->FrameCount(), 2U);
}
