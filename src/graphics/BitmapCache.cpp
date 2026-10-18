// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/BitmapCache.h"

#include "diagnostics/Logger.h"

namespace aether::graphics {

ID2D1Bitmap1* BitmapCache::Find(const std::wstring& key) {
    const auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;
    if (it->second.generation != device_.Generation()) {
        entries_.erase(it);
        return nullptr;
    }
    return it->second.bitmap.get();
}

ID2D1Bitmap1* BitmapCache::Put(ID2D1DeviceContext* context, const std::wstring& key, std::uint32_t width,
                               std::uint32_t height, std::span<const std::uint8_t> pixels) {
    if (!context || width == 0 || height == 0 || width > 4096 || height > 4096 ||
        pixels.size() < static_cast<std::size_t>(width) * height * 4) {
        return nullptr;
    }
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    if (FAILED(context->CreateBitmap(D2D1::SizeU(width, height), pixels.data(), width * 4, properties,
                                     bitmap.put()))) {
        return nullptr;
    }
    auto* raw = bitmap.get();
    entries_[key] = Entry{device_.Generation(), std::move(bitmap)};
    return raw;
}

ID2D1Bitmap1* BitmapCache::PutFromWicSource(ID2D1DeviceContext* context, const std::wstring& key,
                                            IWICBitmapSource* source) {
    auto* wic = device_.WicFactory();
    if (!wic || !source) return nullptr;
    winrt::com_ptr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeMedianCut))) {
        return nullptr;
    }
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    if (FAILED(context->CreateBitmapFromWicBitmap(converter.get(), nullptr, bitmap.put()))) return nullptr;
    auto* raw = bitmap.get();
    entries_[key] = Entry{device_.Generation(), std::move(bitmap)};
    return raw;
}

ID2D1Bitmap1* BitmapCache::PutFromFile(ID2D1DeviceContext* context, const std::wstring& key,
                                       const std::filesystem::path& file) {
    auto* wic = device_.WicFactory();
    if (!wic) return nullptr;
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              decoder.put()))) {
        AETHER_LOG_WARN("graphics", "cannot decode image {}", file.filename().string());
        return nullptr;
    }
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.put()))) return nullptr;
    return PutFromWicSource(context, key, frame.get());
}

ID2D1Bitmap1* BitmapCache::PutFromResource(ID2D1DeviceContext* context, const std::wstring& key, int resourceId) {
    auto* wic = device_.WicFactory();
    const HMODULE module = ::GetModuleHandleW(nullptr);
    const HRSRC resource = ::FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!wic || !resource) return nullptr;
    const HGLOBAL loaded = ::LoadResource(module, resource);
    const DWORD size = ::SizeofResource(module, resource);
    const void* data = loaded ? ::LockResource(loaded) : nullptr;
    if (!data || size == 0) return nullptr;

    winrt::com_ptr<IWICStream> stream;
    if (FAILED(wic->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), size))) {
        return nullptr;
    }
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.put()))) {
        return nullptr;
    }
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.put()))) return nullptr;
    return PutFromWicSource(context, key, frame.get());
}

void BitmapCache::Remove(const std::wstring& key) { entries_.erase(key); }

}  // namespace aether::graphics
