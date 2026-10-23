// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/CaptureBuffer.h"

#include "showcase/FrameArchive.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace aether::showcase {
namespace {
constexpr std::uint32_t kTileSize = 32;
constexpr std::uint64_t kAllocationAllowance = 64;

struct StoredFrame final {
    double timestamp = 0;
    std::uint64_t next = 0;
    std::uint32_t tileCount = 0;
    std::uint32_t reserved = 0;
};

struct StoredTile final {
    std::uint32_t index = 0;
    std::uint32_t bytes = 0;
};

std::uint64_t AlignRecord(std::uint64_t value) noexcept {
    return (value + alignof(StoredFrame) - 1) & ~(static_cast<std::uint64_t>(alignof(StoredFrame)) - 1);
}
}  // namespace

struct CaptureBuffer::Impl final {
    std::unique_ptr<std::uint8_t, void (*)(std::uint8_t*)> arena{nullptr, [](std::uint8_t* pointer) {
        if (pointer) (void)::VirtualFree(pointer, 0, MEM_RELEASE);
    }};
    std::uint64_t capacity = 0;
    std::uint64_t committed = 0;
    std::uint64_t used = 0;
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    std::uint64_t imageBytes = 0;
    std::uint64_t pageSize = 0;
    std::size_t count = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t tilesAcross = 0;
    std::uint32_t tilesDown = 0;
    bool failed = false;
    bool sealed = false;

    [[nodiscard]] bool Ensure(std::uint64_t needed) noexcept {
        if (needed > capacity) return false;
        if (needed <= committed) return true;
        const auto newCommitted = ((needed + pageSize - 1) / pageSize) * pageSize;
        if (newCommitted > capacity) return false;
        if (!::VirtualAlloc(arena.get() + committed, static_cast<SIZE_T>(newCommitted - committed),
            MEM_COMMIT, PAGE_READWRITE)) return false;
        committed = newCommitted;
        return true;
    }

    StoredFrame& Frame(std::uint64_t offset) noexcept {
        return *reinterpret_cast<StoredFrame*>(arena.get() + offset);
    }

    [[nodiscard]] bool TileChanged(const RgbaFrameView& frame, std::uint32_t tileX,
                                    std::uint32_t tileY) const noexcept {
        const auto x = tileX * kTileSize;
        const auto y = tileY * kTileSize;
        const auto rowBytes = static_cast<std::size_t>(std::min(kTileSize, width - x)) * 4U;
        const auto rows = std::min(kTileSize, height - y);
        const auto sourceRow = frame.pixels.data() + static_cast<std::size_t>(y) * frame.rowStrideBytes + x * 4U;
        const auto previousRow = arena.get() + (static_cast<std::size_t>(y) * width + x) * 4U;
        for (std::uint32_t row = 0; row < rows; ++row)
            if (std::memcmp(sourceRow + static_cast<std::size_t>(row) * frame.rowStrideBytes,
                previousRow + static_cast<std::size_t>(row) * width * 4U, rowBytes) != 0) return true;
        return false;
    }
};

CaptureBuffer::CaptureBuffer() noexcept = default;
CaptureBuffer::~CaptureBuffer() = default;
CaptureBuffer::CaptureBuffer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
CaptureBuffer::CaptureBuffer(CaptureBuffer&&) noexcept = default;
CaptureBuffer& CaptureBuffer::operator=(CaptureBuffer&&) noexcept = default;

Result<CaptureBuffer> CaptureBuffer::Create(std::uint32_t width, std::uint32_t height,
                                           std::uint64_t maxBytes) noexcept {
    constexpr auto bookkeeping = sizeof(CaptureBuffer) + sizeof(Impl) + kAllocationAllowance;
    if (width == 0 || height == 0 || width > 16'384 || height > 16'384 ||
        maxBytes > MaximumBytes || maxBytes <= bookkeeping)
        return MakeError(ErrorCode::InvalidArgument, "capture buffer geometry or memory limit is invalid");
    try {
        SYSTEM_INFO system{};
        ::GetSystemInfo(&system);
        const auto pageSize = static_cast<std::uint64_t>(system.dwPageSize);
        if (pageSize == 0) return MakeError(ErrorCode::Unavailable, "capture memory page size is unavailable");
        auto impl = std::make_unique<Impl>();
        impl->width = width;
        impl->height = height;
        impl->imageBytes = static_cast<std::uint64_t>(width) * height * 4U;
        impl->pageSize = pageSize;
        impl->capacity = ((maxBytes - bookkeeping) / pageSize) * pageSize;
        const auto initialBytes = AlignRecord(impl->imageBytes) + sizeof(StoredFrame) + impl->imageBytes;
        if (initialBytes > impl->capacity)
            return MakeError(ErrorCode::InvalidArgument, "capture memory limit cannot hold the initial lossless frame");
        impl->arena.reset(static_cast<std::uint8_t*>(::VirtualAlloc(nullptr,
            static_cast<SIZE_T>(impl->capacity), MEM_RESERVE, PAGE_NOACCESS)));
        if (!impl->arena) return MakeError(ErrorCode::Unavailable, "capture memory could not be reserved");
        impl->used = AlignRecord(impl->imageBytes);
        impl->tilesAcross = (width + kTileSize - 1) / kTileSize;
        impl->tilesDown = (height + kTileSize - 1) / kTileSize;
        return CaptureBuffer(std::move(impl));
    } catch (...) { return MakeError(ErrorCode::Unavailable, "capture buffer allocation failed"); }
}

Result<void> CaptureBuffer::Append(double timestampSeconds, const RgbaFrameView& frame) noexcept {
    if (!impl_ || impl_->failed || impl_->sealed)
        return MakeError(ErrorCode::InvalidState, "capture buffer is not accepting frames");
    if (!std::isfinite(timestampSeconds) || timestampSeconds < 0.0 ||
        (impl_->count && timestampSeconds <= impl_->Frame(impl_->last).timestamp) ||
        frame.width != impl_->width || frame.height != impl_->height ||
        frame.rowStrideBytes < static_cast<std::size_t>(impl_->width) * 4U ||
        frame.rowStrideBytes > std::numeric_limits<std::size_t>::max() / frame.height ||
        frame.pixels.size() < frame.rowStrideBytes * (frame.height - 1U) + static_cast<std::size_t>(frame.width) * 4U)
        return MakeError(ErrorCode::InvalidArgument, "capture frame geometry or timestamp is invalid");

    std::uint32_t changedTiles = 0;
    std::uint64_t payloadBytes = impl_->count == 0 ? impl_->imageBytes : 0;
    if (impl_->count != 0) {
        for (std::uint32_t tileY = 0; tileY < impl_->tilesDown; ++tileY) {
            for (std::uint32_t tileX = 0; tileX < impl_->tilesAcross; ++tileX) {
                if (!impl_->TileChanged(frame, tileX, tileY)) continue;
                ++changedTiles;
                payloadBytes += sizeof(StoredTile) +
                    static_cast<std::uint64_t>(std::min(kTileSize, impl_->width - tileX * kTileSize)) *
                    std::min(kTileSize, impl_->height - tileY * kTileSize) * 4U;
            }
        }
    }
    const auto offset = AlignRecord(impl_->used);
    const auto end = AlignRecord(offset + sizeof(StoredFrame) + payloadBytes);
    if (!impl_->Ensure(end)) {
        impl_->failed = true;
        return MakeError(ErrorCode::Unavailable, "lossless capture reached its bounded memory limit");
    }
    auto* stored = std::construct_at(reinterpret_cast<StoredFrame*>(impl_->arena.get() + offset));
    stored->timestamp = timestampSeconds;
    stored->tileCount = changedTiles;
    auto* output = impl_->arena.get() + offset + sizeof(StoredFrame);
    if (impl_->count == 0) {
        const auto rowBytes = static_cast<std::size_t>(impl_->width) * 4U;
        for (std::uint32_t row = 0; row < impl_->height; ++row) {
            const auto* input = frame.pixels.data() + static_cast<std::size_t>(row) * frame.rowStrideBytes;
            std::memcpy(output + static_cast<std::size_t>(row) * rowBytes, input, rowBytes);
            std::memcpy(impl_->arena.get() + static_cast<std::size_t>(row) * rowBytes, input, rowBytes);
        }
        impl_->first = offset;
    } else {
        for (std::uint32_t tileY = 0; tileY < impl_->tilesDown; ++tileY) {
            for (std::uint32_t tileX = 0; tileX < impl_->tilesAcross; ++tileX) {
                if (!impl_->TileChanged(frame, tileX, tileY)) continue;
                const auto x = tileX * kTileSize;
                const auto y = tileY * kTileSize;
                const auto rowBytes = std::min(kTileSize, impl_->width - x) * 4U;
                const auto rows = std::min(kTileSize, impl_->height - y);
                const StoredTile tile{tileY * impl_->tilesAcross + tileX, rowBytes * rows};
                std::memcpy(output, &tile, sizeof(tile));
                output += sizeof(tile);
                for (std::uint32_t row = 0; row < rows; ++row) {
                    const auto* input = frame.pixels.data() + static_cast<std::size_t>(y + row) * frame.rowStrideBytes + x * 4U;
                    std::memcpy(output, input, rowBytes);
                    std::memcpy(impl_->arena.get() + (static_cast<std::size_t>(y + row) * impl_->width + x) * 4U,
                        input, rowBytes);
                    output += rowBytes;
                }
            }
        }
        impl_->Frame(impl_->last).next = offset;
    }
    impl_->used = end;
    impl_->last = offset;
    ++impl_->count;
    return {};
}

Result<void> CaptureBuffer::FlushTo(FrameArchive& archive) noexcept {
    if (!impl_ || impl_->failed || impl_->count == 0)
        return MakeError(ErrorCode::InvalidState, "capture buffer has no complete capture to flush");
    if (archive.Width() != impl_->width || archive.Height() != impl_->height || archive.FrameCount() != 0)
        return MakeError(ErrorCode::InvalidArgument, "capture archive must be empty and match the capture geometry");
    impl_->sealed = true;
    auto offset = impl_->first;
    for (std::size_t index = 0; index < impl_->count; ++index) {
        const auto& record = impl_->Frame(offset);
        const auto* input = impl_->arena.get() + offset + sizeof(StoredFrame);
        if (index == 0) {
            std::memcpy(impl_->arena.get(), input, static_cast<std::size_t>(impl_->imageBytes));
        } else {
            for (std::uint32_t patch = 0; patch < record.tileCount; ++patch) {
                StoredTile tile{};
                std::memcpy(&tile, input, sizeof(tile));
                input += sizeof(tile);
                const auto x = (tile.index % impl_->tilesAcross) * kTileSize;
                const auto y = (tile.index / impl_->tilesAcross) * kTileSize;
                const auto rowBytes = std::min(kTileSize, impl_->width - x) * 4U;
                const auto rows = std::min(kTileSize, impl_->height - y);
                for (std::uint32_t row = 0; row < rows; ++row) {
                    std::memcpy(impl_->arena.get() + (static_cast<std::size_t>(y + row) * impl_->width + x) * 4U,
                        input, rowBytes);
                    input += rowBytes;
                }
            }
        }
        if (auto result = archive.Append(record.timestamp, RgbaFrameView{impl_->width, impl_->height,
            static_cast<std::size_t>(impl_->width) * 4U,
            std::span<const std::uint8_t>(impl_->arena.get(), static_cast<std::size_t>(impl_->imageBytes))}); !result)
            return result.error();
        offset = record.next;
    }
    return {};
}

std::size_t CaptureBuffer::FrameCount() const noexcept { return impl_ ? impl_->count : 0; }
std::uint64_t CaptureBuffer::MemoryBytes() const noexcept {
    return impl_ ? sizeof(CaptureBuffer) + sizeof(Impl) + kAllocationAllowance + impl_->committed : sizeof(CaptureBuffer);
}
std::uint64_t CaptureBuffer::StoredBytes() const noexcept {
    return impl_ ? sizeof(CaptureBuffer) + sizeof(Impl) + kAllocationAllowance + impl_->used : sizeof(CaptureBuffer);
}
std::uint32_t CaptureBuffer::Width() const noexcept { return impl_ ? impl_->width : 0; }
std::uint32_t CaptureBuffer::Height() const noexcept { return impl_ ? impl_->height : 0; }
}  // namespace aether::showcase
