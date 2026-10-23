// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/FrameArchive.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace aether::showcase {
namespace {

#pragma pack(push, 1)
struct ArchiveHeader final {
    char magic[8];
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    double nominalFramesPerSecond;
    std::uint64_t frameCount;
};
#pragma pack(pop)
static_assert(sizeof(ArchiveHeader) == 36);

constexpr char kMagic[8] = {'A', 'E', 'T', 'H', 'F', 'R', 'M', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaximumDimension = 16'384;
constexpr std::uint64_t kMaximumIndexedFrames = 1'000'000;

class ScopedFile final {
public:
    explicit ScopedFile(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~ScopedFile() { if (value_ != INVALID_HANDLE_VALUE) (void)::CloseHandle(value_); }
    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;
    ScopedFile(ScopedFile&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    ScopedFile& operator=(ScopedFile&& other) noexcept {
        if (this != &other) {
            if (value_ != INVALID_HANDLE_VALUE) (void)::CloseHandle(value_);
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    [[nodiscard]] HANDLE Get() const noexcept { return value_; }
    [[nodiscard]] bool Valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

Error FileError(ErrorCode code, const char* message) noexcept {
    return MakeError(code, message, static_cast<std::int64_t>(::GetLastError()));
}

bool Seek(HANDLE file, std::uint64_t offset) noexcept {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    return ::SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
}

bool WriteExact(HANDLE file, const void* data, std::size_t bytes) noexcept {
    const auto* cursor = static_cast<const std::byte*>(data);
    while (bytes != 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!::WriteFile(file, cursor, chunk, &written, nullptr) || written == 0) return false;
        cursor += written;
        bytes -= written;
    }
    return true;
}

bool ReadExact(HANDLE file, void* data, std::size_t bytes) noexcept {
    auto* cursor = static_cast<std::byte*>(data);
    while (bytes != 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        if (!::ReadFile(file, cursor, chunk, &read, nullptr) || read == 0) return false;
        cursor += read;
        bytes -= read;
    }
    return true;
}

bool ValidFrame(const RgbaFrameView& frame, std::uint32_t width, std::uint32_t height) noexcept {
    if (frame.width != width || frame.height != height || width == 0 || height == 0 ||
        static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / 4U) return false;
    const auto rowBytes = static_cast<std::size_t>(width) * 4U;
    return frame.rowStrideBytes >= rowBytes &&
        frame.pixels.size() >= frame.rowStrideBytes * static_cast<std::size_t>(height - 1) + rowBytes;
}

bool LocalAbsolutePath(std::wstring_view path) noexcept {
    if (path.size() < 3 || path[1] != L':' || path[2] != L'\\') return false;
    const wchar_t root[] = {path[0], L':', L'\\', L'\0'};
    const UINT driveType = ::GetDriveTypeW(root);
    return driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE || driveType == DRIVE_RAMDISK;
}

}  // namespace

struct FrameArchive::Impl final {
    ScopedFile file;
    std::wstring path;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double framesPerSecond = 0.0;
    std::uint64_t frameBytes = 0;
    std::vector<double> timestamps;
    std::vector<std::uint8_t> current;
    std::size_t currentIndex = std::numeric_limits<std::size_t>::max();
    bool finalized = false;
    bool failed = false;
};

FrameArchive::FrameArchive() noexcept = default;
FrameArchive::FrameArchive(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
FrameArchive::~FrameArchive() = default;
FrameArchive::FrameArchive(FrameArchive&& other) noexcept = default;
FrameArchive& FrameArchive::operator=(FrameArchive&& other) noexcept = default;

Result<FrameArchive> FrameArchive::Create(std::wstring localFile, std::uint32_t width, std::uint32_t height,
                                          double nominalFramesPerSecond) noexcept {
    if (localFile.empty() || !LocalAbsolutePath(localFile) || width == 0 || height == 0 ||
        width > kMaximumDimension || height > kMaximumDimension || !std::isfinite(nominalFramesPerSecond) ||
        nominalFramesPerSecond < 1.0 || nominalFramesPerSecond > 240.0 ||
        static_cast<std::uint64_t>(width) * height > std::numeric_limits<DWORD>::max() / 4ULL) {
        return MakeError(ErrorCode::InvalidArgument, "showcase frame archive path or geometry is invalid");
    }
    try {
        auto impl = std::make_unique<Impl>();
        impl->path = std::move(localFile);
        impl->width = width;
        impl->height = height;
        impl->framesPerSecond = nominalFramesPerSecond;
        impl->frameBytes = static_cast<std::uint64_t>(width) * height * 4ULL;
        impl->current.resize(static_cast<std::size_t>(impl->frameBytes));
        impl->file = ScopedFile(::CreateFileW(impl->path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
                                              nullptr));
        if (!impl->file.Valid()) {
            const DWORD error = ::GetLastError();
            const auto code = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
                                  ? ErrorCode::AlreadyExists : ErrorCode::Io;
            return MakeError(code, "showcase frame archive could not create a new local file", error);
        }
        ArchiveHeader header{};
        std::memcpy(header.magic, kMagic, sizeof(kMagic));
        header.version = kVersion;
        header.width = width;
        header.height = height;
        header.nominalFramesPerSecond = nominalFramesPerSecond;
        if (!WriteExact(impl->file.Get(), &header, sizeof(header)))
            return FileError(ErrorCode::Io, "showcase frame archive header could not be written");
        return FrameArchive(std::move(impl));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase frame archive buffer allocation failed");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase frame archive initialization failed");
    }
}

Result<FrameArchive> FrameArchive::Open(std::wstring localFile) noexcept {
    if (localFile.empty() || !LocalAbsolutePath(localFile))
        return MakeError(ErrorCode::InvalidArgument, "showcase frame archive requires an absolute local file");
    try {
        auto impl = std::make_unique<Impl>();
        impl->path = std::move(localFile);
        impl->file = ScopedFile(::CreateFileW(impl->path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                              nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr));
        if (!impl->file.Valid())
            return FileError(ErrorCode::Io, "showcase frame archive could not open a completed local file");
        LARGE_INTEGER fileSize{};
        if (!::GetFileSizeEx(impl->file.Get(), &fileSize))
            return FileError(ErrorCode::Io, "showcase frame archive size is unavailable");
        if (fileSize.QuadPart < static_cast<LONGLONG>(sizeof(ArchiveHeader)))
            return MakeError(ErrorCode::Integrity, "showcase frame archive header is incomplete");

        ArchiveHeader header{};
        if (!ReadExact(impl->file.Get(), &header, sizeof(header)))
            return MakeError(ErrorCode::Integrity, "showcase frame archive header could not be read");
        if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.version != kVersion ||
            header.width == 0 || header.height == 0 || header.width > kMaximumDimension ||
            header.height > kMaximumDimension || !std::isfinite(header.nominalFramesPerSecond) ||
            header.nominalFramesPerSecond < 1.0 || header.nominalFramesPerSecond > 240.0 ||
            header.frameCount == 0 || header.frameCount > kMaximumIndexedFrames ||
            header.frameCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            static_cast<std::uint64_t>(header.width) * header.height > std::numeric_limits<DWORD>::max() / 4ULL)
            return MakeError(ErrorCode::Integrity, "showcase frame archive header is invalid or unfinalized");

        const std::uint64_t frameBytes = static_cast<std::uint64_t>(header.width) * header.height * 4ULL;
        const std::uint64_t recordBytes = sizeof(double) + frameBytes;
        if (header.frameCount > (static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()) -
                                sizeof(ArchiveHeader)) / recordBytes)
            return MakeError(ErrorCode::Integrity, "showcase frame archive record bounds are invalid");
        const std::uint64_t expectedBytes = sizeof(ArchiveHeader) + header.frameCount * recordBytes;
        if (static_cast<std::uint64_t>(fileSize.QuadPart) != expectedBytes)
            return MakeError(ErrorCode::Integrity, "showcase frame archive size does not match its complete records");

        impl->timestamps.reserve(static_cast<std::size_t>(header.frameCount));
        for (std::uint64_t index = 0; index < header.frameCount; ++index) {
            const std::uint64_t recordOffset = sizeof(ArchiveHeader) + index * recordBytes;
            double timestamp = 0.0;
            if (!Seek(impl->file.Get(), recordOffset) || !ReadExact(impl->file.Get(), &timestamp, sizeof(timestamp)))
                return MakeError(ErrorCode::Integrity, "showcase frame archive timestamp index is incomplete");
            if (!std::isfinite(timestamp) || timestamp < 0.0 ||
                (!impl->timestamps.empty() && timestamp <= impl->timestamps.back()))
                return MakeError(ErrorCode::Integrity, "showcase frame archive timestamps are invalid");
            impl->timestamps.push_back(timestamp);
        }
        impl->width = header.width;
        impl->height = header.height;
        impl->framesPerSecond = header.nominalFramesPerSecond;
        impl->frameBytes = frameBytes;
        impl->finalized = true;
        return FrameArchive(std::move(impl));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase frame archive timestamp index allocation failed");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase frame archive reopen failed");
    }
}

Result<void> FrameArchive::Append(double timestampSeconds, const RgbaFrameView& frame) noexcept {
    if (!impl_ || impl_->finalized || impl_->failed)
        return MakeError(ErrorCode::InvalidState, "showcase frame archive is not open for capture");
    if (!std::isfinite(timestampSeconds) || timestampSeconds < 0.0 ||
        (!impl_->timestamps.empty() && timestampSeconds <= impl_->timestamps.back()) ||
        !ValidFrame(frame, impl_->width, impl_->height) || impl_->timestamps.size() >= std::numeric_limits<std::uint32_t>::max()) {
        return MakeError(ErrorCode::InvalidArgument, "showcase captured frame timestamp or pixels are invalid");
    }
    try {
        impl_->timestamps.push_back(timestampSeconds);
        if (!WriteExact(impl_->file.Get(), &timestampSeconds, sizeof(timestampSeconds))) {
            impl_->failed = true;
            return FileError(ErrorCode::Io, "showcase frame timestamp could not be stored locally");
        }
        const auto rowBytes = static_cast<std::size_t>(impl_->width) * 4U;
        if (frame.rowStrideBytes == rowBytes) {
            if (!WriteExact(impl_->file.Get(), frame.pixels.data(), static_cast<std::size_t>(impl_->frameBytes))) {
                impl_->failed = true;
                return FileError(ErrorCode::Io, "showcase frame pixels could not be stored locally");
            }
        } else {
            for (std::uint32_t row = 0; row < impl_->height; ++row) {
                const auto sourceOffset = static_cast<std::size_t>(row) * frame.rowStrideBytes;
                if (!WriteExact(impl_->file.Get(), frame.pixels.data() + sourceOffset, rowBytes)) {
                    impl_->failed = true;
                    return FileError(ErrorCode::Io, "showcase frame pixels could not be stored locally");
                }
            }
        }
        return Ok();
    } catch (const std::bad_alloc&) {
        impl_->failed = true;
        return MakeError(ErrorCode::Unavailable, "showcase frame timestamp index allocation failed");
    } catch (...) {
        impl_->failed = true;
        return MakeError(ErrorCode::Internal, "showcase frame archive write failed");
    }
}

Result<void> FrameArchive::Finalize() noexcept {
    if (!impl_ || impl_->finalized || impl_->failed)
        return MakeError(ErrorCode::InvalidState, "showcase frame archive cannot be finalized");
    if (impl_->timestamps.empty()) return MakeError(ErrorCode::InvalidState, "showcase frame archive contains no captured frames");
    ArchiveHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.width = impl_->width;
    header.height = impl_->height;
    header.nominalFramesPerSecond = impl_->framesPerSecond;
    header.frameCount = impl_->timestamps.size();
    if (!Seek(impl_->file.Get(), 0) || !WriteExact(impl_->file.Get(), &header, sizeof(header)) ||
        !::FlushFileBuffers(impl_->file.Get())) {
        impl_->failed = true;
        return FileError(ErrorCode::Io, "showcase frame archive could not be finalized");
    }
    impl_->finalized = true;
    return Ok();
}

Result<RgbaFrameView> FrameArchive::ReadFrameAt(double timestampSeconds) noexcept {
    if (!impl_ || !impl_->finalized || impl_->failed || impl_->timestamps.empty() || !std::isfinite(timestampSeconds))
        return MakeError(ErrorCode::InvalidState, "showcase frame archive is not ready for rendering");
    try {
        const auto found = std::lower_bound(impl_->timestamps.begin(), impl_->timestamps.end(), timestampSeconds);
        std::size_t index = found == impl_->timestamps.end() ? impl_->timestamps.size() - 1
                                                              : static_cast<std::size_t>(found - impl_->timestamps.begin());
        if (index > 0 && std::abs(impl_->timestamps[index - 1] - timestampSeconds) <=
                             std::abs(impl_->timestamps[index] - timestampSeconds)) --index;
        if (index == impl_->currentIndex)
            return RgbaFrameView{impl_->width, impl_->height, static_cast<std::size_t>(impl_->width) * 4U,
                                 impl_->current, static_cast<std::uint64_t>(index) + 1U};
        const std::uint64_t recordBytes = sizeof(double) + impl_->frameBytes;
        if (index > (std::numeric_limits<std::uint64_t>::max() - sizeof(ArchiveHeader)) / recordBytes)
            return MakeError(ErrorCode::Integrity, "showcase frame archive index is invalid");
        const std::uint64_t recordOffset = sizeof(ArchiveHeader) + static_cast<std::uint64_t>(index) * recordBytes;
        if (impl_->current.size() != impl_->frameBytes)
            impl_->current.resize(static_cast<std::size_t>(impl_->frameBytes));
        impl_->currentIndex = std::numeric_limits<std::size_t>::max();
        if (!Seek(impl_->file.Get(), recordOffset + sizeof(double)) ||
            !ReadExact(impl_->file.Get(), impl_->current.data(), impl_->current.size()))
            return FileError(ErrorCode::Io, "showcase frame archive could not read a captured frame");
        impl_->currentIndex = index;
        return RgbaFrameView{impl_->width, impl_->height, static_cast<std::size_t>(impl_->width) * 4U,
                             impl_->current, static_cast<std::uint64_t>(index) + 1U};
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase renderer could not allocate an archive read buffer");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase frame archive read failed");
    }
}

std::size_t FrameArchive::FrameCount() const noexcept { return impl_ ? impl_->timestamps.size() : 0; }
std::uint32_t FrameArchive::Width() const noexcept { return impl_ ? impl_->width : 0; }
std::uint32_t FrameArchive::Height() const noexcept { return impl_ ? impl_->height : 0; }
double FrameArchive::NominalFramesPerSecond() const noexcept { return impl_ ? impl_->framesPerSecond : 0.0; }
const std::vector<double>& FrameArchive::CaptureTimes() const noexcept {
    static const std::vector<double> empty;
    return impl_ ? impl_->timestamps : empty;
}

}  // namespace aether::showcase
