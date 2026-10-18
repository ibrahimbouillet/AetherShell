// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Least-recently-used memory cache of decoded icons keyed by (key, pixel size). Bounded both by
// entry count and by pixel bytes, because a 256 px icon weighs 256 KiB while a 32 px one weighs
// 4 KiB. Not thread-safe: ShellIconProvider serializes access with its own mutex.
#pragma once

#include "applications/ApplicationTypes.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

namespace aether::apps {

class IconCache {
public:
    IconCache(std::size_t maxEntries, std::size_t maxBytes);

    // The cached image (and marks it most recently used), or null.
    [[nodiscard]] std::shared_ptr<const IconImage> Find(const std::wstring& key, std::uint32_t pixelSize);

    // Inserts or replaces, then evicts least recently used entries until both limits hold. An image
    // larger than the byte budget on its own is not cached. Null images are ignored.
    void Insert(const std::wstring& key, std::uint32_t pixelSize, std::shared_ptr<const IconImage> image);

    void Clear() noexcept;

    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t Bytes() const noexcept { return bytes_; }

    // Composite map key: "<size>:<key>" (the size has no ':' so keys cannot collide).
    [[nodiscard]] static std::wstring CompositeKey(const std::wstring& key, std::uint32_t pixelSize);

private:
    struct Entry {
        std::wstring compositeKey;
        std::shared_ptr<const IconImage> image;
        std::size_t bytes = 0;
    };
    void EvictToLimits();

    std::size_t maxEntries_;
    std::size_t maxBytes_;
    std::size_t bytes_ = 0;
    std::list<Entry> entries_;  // most recently used first
    std::unordered_map<std::wstring, std::list<Entry>::iterator> index_;
};

}  // namespace aether::apps
