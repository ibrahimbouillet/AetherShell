// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

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

class TemporaryArchivePath final {
public:
    TemporaryArchivePath() {
        wchar_t directory[MAX_PATH]{};
        const DWORD length = ::GetTempPathW(MAX_PATH, directory);
        if (length == 0 || length >= MAX_PATH) return;
        wchar_t reservation[MAX_PATH]{};
        if (::GetTempFileNameW(directory, L"asf", 0, reservation) == 0) return;
        (void)::DeleteFileW(reservation);
        path_ = reservation;
    }
    ~TemporaryArchivePath() { if (!path_.empty()) (void)::DeleteFileW(path_.c_str()); }
    [[nodiscard]] const std::wstring& Get() const noexcept { return path_; }
private:
    std::wstring path_;
};

bool CreateReopenFixture(const std::wstring& path) {
    auto archive = FrameArchive::Create(path, 2, 1, 60.0);
    if (!archive) return false;
    const std::array<std::uint8_t, 8> first{10, 20, 30, 255, 40, 50, 60, 255};
    const std::array<std::uint8_t, 8> second{80, 90, 100, 255, 110, 120, 130, 255};
    return archive->Append(0.0, {2, 1, 8, first}).ok() &&
        archive->Append(0.05, {2, 1, 8, second}).ok() && archive->Finalize().ok();
}

template <class T>
bool OverwriteArchiveValue(const std::wstring& path, std::uint64_t offset, const T& value) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD written = 0;
    const bool success = ::SetFilePointerEx(file, position, nullptr, FILE_BEGIN) &&
        ::WriteFile(file, &value, static_cast<DWORD>(sizeof(value)), &written, nullptr) && written == sizeof(value);
    (void)::CloseHandle(file);
    return success;
}

bool ResizeArchive(const std::wstring& path, LONGLONG difference) {
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool success = ::GetFileSizeEx(file, &size) != FALSE;
    size.QuadPart += difference;
    success = success && ::SetFilePointerEx(file, size, nullptr, FILE_BEGIN) && ::SetEndOfFile(file);
    (void)::CloseHandle(file);
    return success;
}

}  // namespace

AETHER_TEST("showcase frame archive: records synthetic frames and reads the nearest timestamp") {
    TemporaryArchivePath path;
    REQUIRE_FALSE(path.Get().empty());
    auto archiveResult = FrameArchive::Create(path.Get(), 2, 1, 30.0);
    REQUIRE(archiveResult.ok());
    auto archive = std::move(archiveResult).value();
    std::array<std::uint8_t, 8> first{10, 20, 30, 255, 10, 20, 30, 255};
    std::array<std::uint8_t, 8> second{80, 90, 100, 255, 80, 90, 100, 255};
    REQUIRE(archive.Append(0.0, {2, 1, 8, first}).ok());
    REQUIRE(archive.Append(0.034, {2, 1, 8, second}).ok());
    REQUIRE(archive.Finalize().ok());
    CHECK_EQ(archive.FrameCount(), 2U);
    CHECK_EQ(archive.Width(), 2U);
    CHECK_EQ(archive.Height(), 1U);
    auto frame = archive.ReadFrameAt(0.03);
    REQUIRE(frame.ok());
    CHECK_EQ(frame->pixels[0], static_cast<std::uint8_t>(80));
    CHECK_EQ(frame->pixels[1], static_cast<std::uint8_t>(90));
    CHECK_EQ(frame->pixels[2], static_cast<std::uint8_t>(100));
    CHECK_EQ(frame->pixels[3], static_cast<std::uint8_t>(255));
}

AETHER_TEST("showcase frame archive: rejects existing files and nonlocal destinations") {
    TemporaryArchivePath path;
    REQUIRE_FALSE(path.Get().empty());
    HANDLE sentinel = ::CreateFileW(path.Get().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(sentinel != INVALID_HANDLE_VALUE);
    (void)::CloseHandle(sentinel);
    auto collision = FrameArchive::Create(path.Get(), 2, 2, 30.0);
    CHECK_FALSE(collision.ok());
    CHECK_EQ(collision.error().code, ErrorCode::AlreadyExists);
    auto network = FrameArchive::Create(L"\\\\invalid\\share\\capture.aetherframes", 2, 2, 30.0);
    CHECK_FALSE(network.ok());
    CHECK_EQ(network.error().code, ErrorCode::InvalidArgument);
}

AETHER_TEST("showcase frame archive: validates monotonic timestamps, frame geometry, and finalization") {
    TemporaryArchivePath path;
    REQUIRE_FALSE(path.Get().empty());
    auto archiveResult = FrameArchive::Create(path.Get(), 1, 1, 60.0);
    REQUIRE(archiveResult.ok());
    auto archive = std::move(archiveResult).value();
    std::array<std::uint8_t, 4> pixels{1, 2, 3, 255};
    REQUIRE(archive.Append(1.0 / 60.0, {1, 1, 4, pixels}).ok());
    CHECK_FALSE(archive.Append(1.0 / 60.0, {1, 1, 4, pixels}).ok());
    CHECK_FALSE(archive.Append(2.0 / 60.0, {2, 1, 8, pixels}).ok());
    REQUIRE(archive.Finalize().ok());
    CHECK_FALSE(archive.Finalize().ok());
    CHECK_EQ(archive.CaptureTimes().size(), 1U);
    auto first = archive.ReadFrameAt(0.0);
    REQUIRE(first.ok());
    CHECK_EQ(first->pixels[0], static_cast<std::uint8_t>(1));
}

AETHER_TEST("showcase frame archive: reopens finalized capture read-only and preserves exact synthetic pixels") {
    TemporaryArchivePath path;
    REQUIRE_FALSE(path.Get().empty());
    REQUIRE(CreateReopenFixture(path.Get()));  // The writer is destroyed before Open.
    auto archive = FrameArchive::Open(path.Get());
    REQUIRE(archive.ok());
    CHECK_EQ(archive->FrameCount(), 2U);
    CHECK_EQ(archive->Width(), 2U);
    CHECK_EQ(archive->Height(), 1U);
    CHECK_EQ(archive->NominalFramesPerSecond(), 60.0);
    CHECK_EQ(archive->CaptureTimes().front(), 0.0);
    CHECK_EQ(archive->CaptureTimes().back(), 0.05);
    const std::array<std::uint8_t, 8> first{10, 20, 30, 255, 40, 50, 60, 255};
    auto frame = archive->ReadFrameAt(0.0);
    REQUIRE(frame.ok());
    CHECK(std::equal(frame->pixels.begin(), frame->pixels.end(), first.begin(), first.end()));
    const std::array<std::uint8_t, 8> second{80, 90, 100, 255, 110, 120, 130, 255};
    frame = archive->ReadFrameAt(0.049);
    REQUIRE(frame.ok());
    CHECK(std::equal(frame->pixels.begin(), frame->pixels.end(), second.begin(), second.end()));
    CHECK_FALSE(archive->Append(0.1, {2, 1, 8, first}).ok());
    CHECK_FALSE(archive->Finalize().ok());
    HANDLE writer = ::CreateFileW(path.Get().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(writer == INVALID_HANDLE_VALUE);  // Read-only reopen excludes concurrent mutation.
    if (writer != INVALID_HANDLE_VALUE) (void)::CloseHandle(writer);
}

AETHER_TEST("showcase frame archive: reopen rejects nonlocal inputs and unfinished captures without leaking paths") {
    for (const auto* path : {L"relative-private-marker.aetherframes", L"\\\\private-marker\\share\\capture.aetherframes"}) {
        const auto archive = FrameArchive::Open(path);
        REQUIRE_FALSE(archive.ok());
        CHECK_EQ(archive.error().code, ErrorCode::InvalidArgument);
        CHECK(archive.error().message.find("private-marker") == std::string::npos);
    }
    TemporaryArchivePath path;
    REQUIRE_FALSE(path.Get().empty());
    {
        auto unfinished = FrameArchive::Create(path.Get(), 2, 1, 60.0);
        REQUIRE(unfinished.ok());
        const std::array<std::uint8_t, 8> pixels{1, 2, 3, 255, 4, 5, 6, 255};
        REQUIRE(unfinished->Append(0.0, {2, 1, 8, pixels}).ok());
    }
    const auto reopened = FrameArchive::Open(path.Get());
    REQUIRE_FALSE(reopened.ok());
    CHECK_EQ(reopened.error().code, ErrorCode::Integrity);
}

AETHER_TEST("showcase frame archive: reopen validates magic version geometry and finite frame rate") {
    for (unsigned int variant = 0; variant < 7; ++variant) {
        TemporaryArchivePath path;
        REQUIRE(CreateReopenFixture(path.Get()));
        // Version-one header offsets: magic0, version8, width12, height16, frame rate20, count28.
        switch (variant) {
            case 0: REQUIRE(OverwriteArchiveValue(path.Get(), 0, 'X')); break;
            case 1: REQUIRE(OverwriteArchiveValue(path.Get(), 8, std::uint32_t{2})); break;
            case 2: REQUIRE(OverwriteArchiveValue(path.Get(), 12, std::uint32_t{0})); break;
            case 3: REQUIRE(OverwriteArchiveValue(path.Get(), 16, std::uint32_t{16'385})); break;
            case 4: REQUIRE(OverwriteArchiveValue(path.Get(), 20, std::numeric_limits<double>::quiet_NaN())); break;
            case 5: REQUIRE(OverwriteArchiveValue(path.Get(), 20, 0.0)); break;
            default: REQUIRE(OverwriteArchiveValue(path.Get(), 20, 241.0)); break;
        }
        const auto archive = FrameArchive::Open(path.Get());
        REQUIRE_FALSE(archive.ok());
        CHECK_EQ(archive.error().code, ErrorCode::Integrity);
    }
}

AETHER_TEST("showcase frame archive: reopen bounds record count and exact file size before allocation") {
    for (const auto count : {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{1'000'001},
                             std::numeric_limits<std::uint64_t>::max()}) {
        TemporaryArchivePath path;
        REQUIRE(CreateReopenFixture(path.Get()));
        REQUIRE(OverwriteArchiveValue(path.Get(), 28, count));
        const auto archive = FrameArchive::Open(path.Get());
        REQUIRE_FALSE(archive.ok());
        CHECK_EQ(archive.error().code, ErrorCode::Integrity);
    }
    for (const auto difference : {LONGLONG{-1}, LONGLONG{1}, LONGLONG{-40}}) {
        TemporaryArchivePath path;
        REQUIRE(CreateReopenFixture(path.Get()));
        REQUIRE(ResizeArchive(path.Get(), difference));
        const auto archive = FrameArchive::Open(path.Get());
        REQUIRE_FALSE(archive.ok());
        CHECK_EQ(archive.error().code, ErrorCode::Integrity);
    }
}

AETHER_TEST("showcase frame archive: reopen rejects repeated negative and nonfinite timestamps") {
    for (const auto timestamp : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
        TemporaryArchivePath path;
        REQUIRE(CreateReopenFixture(path.Get()));
        // Each fixture record is an 8-byte timestamp plus 8 bytes of RGBA pixels.
        REQUIRE(OverwriteArchiveValue(path.Get(), 36 + 16, timestamp));
        const auto archive = FrameArchive::Open(path.Get());
        REQUIRE_FALSE(archive.ok());
        CHECK_EQ(archive.error().code, ErrorCode::Integrity);
    }
}

AETHER_TEST("showcase frame archive: padded rows omit padding while contiguous frames remain exact") {
    TemporaryArchivePath path;
    REQUIRE_FALSE(path.Get().empty());
    {
        auto archive = FrameArchive::Create(path.Get(), 1, 2, 60.0);
        REQUIRE(archive.ok());
        const std::array<std::uint8_t, 16> padded{10, 20, 30, 255, 99, 99, 99, 99,
                                                 40, 50, 60, 255, 88, 88, 88, 88};
        const std::array<std::uint8_t, 8> contiguous{70, 80, 90, 255, 100, 110, 120, 255};
        REQUIRE(archive->Append(0.0, {1, 2, 8, padded}).ok());
        REQUIRE(archive->Append(0.05, {1, 2, 4, contiguous}).ok());
        REQUIRE(archive->Finalize().ok());
    }
    auto archive = FrameArchive::Open(path.Get());
    REQUIRE(archive.ok());
    const std::array<std::uint8_t, 8> first{10, 20, 30, 255, 40, 50, 60, 255};
    auto frame = archive->ReadFrameAt(0.0);
    REQUIRE(frame.ok());
    CHECK(std::equal(frame->pixels.begin(), frame->pixels.end(), first.begin(), first.end()));
    const std::array<std::uint8_t, 8> second{70, 80, 90, 255, 100, 110, 120, 255};
    frame = archive->ReadFrameAt(0.05);
    REQUIRE(frame.ok());
    CHECK(std::equal(frame->pixels.begin(), frame->pixels.end(), second.begin(), second.end()));
}
