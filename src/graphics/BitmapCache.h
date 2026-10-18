// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Device-dependent Direct2D bitmaps created from CPU pixel buffers (icons) or
// image files/resources (branding). Entries are tagged with the device
// generation and dropped automatically after device loss.
#pragma once

#include "core/Result.h"
#include "graphics/GraphicsDevice.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>

namespace aether::graphics {

class BitmapCache {
public:
    explicit BitmapCache(GraphicsDevice& device) : device_(device) {}

    // Returns the cached bitmap for `key`, or nullptr.
    [[nodiscard]] ID2D1Bitmap1* Find(const std::wstring& key);

    // Creates (or replaces) a bitmap from premultiplied, top-down BGRA pixels.
    ID2D1Bitmap1* Put(ID2D1DeviceContext* context, const std::wstring& key, std::uint32_t width,
                      std::uint32_t height, std::span<const std::uint8_t> pixels);

    // Decodes an image file (PNG/ICO/...) through WIC and caches it.
    ID2D1Bitmap1* PutFromFile(ID2D1DeviceContext* context, const std::wstring& key, const std::filesystem::path& file);

    // Decodes an RCDATA resource of the current executable (e.g. the embedded logo PNG).
    ID2D1Bitmap1* PutFromResource(ID2D1DeviceContext* context, const std::wstring& key, int resourceId);

    void Remove(const std::wstring& key);
    void Clear() noexcept { entries_.clear(); }

private:
    struct Entry {
        std::uint64_t generation = 0;
        winrt::com_ptr<ID2D1Bitmap1> bitmap;
    };
    ID2D1Bitmap1* PutFromWicSource(ID2D1DeviceContext* context, const std::wstring& key, IWICBitmapSource* source);

    GraphicsDevice& device_;
    std::unordered_map<std::wstring, Entry> entries_;
};

}  // namespace aether::graphics
