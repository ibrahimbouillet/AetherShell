// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/IconCache.h"

#include <algorithm>
#include <utility>

namespace aether::apps {

IconCache::IconCache(std::size_t maxEntries, std::size_t maxBytes)
    : maxEntries_(std::max<std::size_t>(1, maxEntries)), maxBytes_(maxBytes) {}

std::wstring IconCache::CompositeKey(const std::wstring& key, std::uint32_t pixelSize) {
    return std::to_wstring(pixelSize) + L":" + key;
}

std::shared_ptr<const IconImage> IconCache::Find(const std::wstring& key, std::uint32_t pixelSize) {
    const auto found = index_.find(CompositeKey(key, pixelSize));
    if (found == index_.end()) return nullptr;
    entries_.splice(entries_.begin(), entries_, found->second);  // iterators stay valid
    return found->second->image;
}

void IconCache::Insert(const std::wstring& key, std::uint32_t pixelSize, std::shared_ptr<const IconImage> image) {
    if (!image) return;
    const std::size_t bytes = image->pixels.size();
    std::wstring composite = CompositeKey(key, pixelSize);
    if (const auto existing = index_.find(composite); existing != index_.end()) {
        bytes_ -= existing->second->bytes;
        entries_.erase(existing->second);
        index_.erase(existing);
    }
    if (bytes > maxBytes_) return;
    entries_.push_front(Entry{composite, std::move(image), bytes});
    index_.emplace(std::move(composite), entries_.begin());
    bytes_ += bytes;
    EvictToLimits();
}

void IconCache::Clear() noexcept {
    entries_.clear();
    index_.clear();
    bytes_ = 0;
}

void IconCache::EvictToLimits() {
    while (!entries_.empty() && (entries_.size() > maxEntries_ || bytes_ > maxBytes_)) {
        const Entry& last = entries_.back();
        bytes_ -= last.bytes;
        index_.erase(last.compositeKey);
        entries_.pop_back();
    }
}

}  // namespace aether::apps
