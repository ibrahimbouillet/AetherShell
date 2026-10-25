// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/GlassPainter.h"

#include "graphics/WinRT.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace aether::graphics {
namespace {

constexpr float kStretchMiddle = 2.0f;        // DIPs of uniform shadow between baked corners
constexpr std::size_t kMaxCachedShadows = 16;
constexpr std::uint32_t kNoiseTileSize = 128;
constexpr std::uint32_t kNoiseSeed = 0x2545F491u;

float Clamp01(float value) noexcept { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }
float Quantize(float value) noexcept { return std::round(value * 4.0f) / 4.0f; }

bool ValidRect(const D2D1_RECT_F& rect) noexcept {
    return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) && std::isfinite(rect.bottom) &&
           rect.right > rect.left && rect.bottom > rect.top;
}

float SafeRadius(const D2D1_RECT_F& rect, float radius) noexcept {
    const float limit = std::min(rect.right - rect.left, rect.bottom - rect.top) / 2.0f;
    return std::isfinite(radius) ? std::clamp(radius, 0.0f, std::max(0.0f, limit)) : 0.0f;
}

D2D1_RECT_F Inflate(const D2D1_RECT_F& rect, float dx, float dy) noexcept {
    return D2D1::RectF(rect.left - dx, rect.top - dy, rect.right + dx, rect.bottom + dy);
}

winrt::com_ptr<ID2D1SolidColorBrush> Solid(ID2D1RenderTarget* target, const Color& color) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    if (target) target->CreateSolidColorBrush(color.ToD2D(), brush.put());
    return brush;
}

winrt::com_ptr<ID2D1LinearGradientBrush> Vertical(ID2D1RenderTarget* target, float top, float bottom,
                                                  const Color& from, const Color& to) {
    const D2D1_GRADIENT_STOP stops[] = {{0.0f, from.ToD2D()}, {1.0f, to.ToD2D()}};
    winrt::com_ptr<ID2D1GradientStopCollection> collection;
    if (FAILED(target->CreateGradientStopCollection(stops, 2, collection.put()))) return nullptr;
    winrt::com_ptr<ID2D1LinearGradientBrush> brush;
    target->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, top), D2D1::Point2F(0.0f, bottom)),
        collection.get(), brush.put());
    return brush;
}

bool SameLayer(const ShadowLayer& a, const ShadowLayer& b) noexcept {
    return a.blur == b.blur && a.offsetY == b.offsetY && a.alpha == b.alpha;
}

struct ShadowKey {
    float bakeWidth = 0.0f;   // silhouette size in DIPs
    float bakeHeight = 0.0f;
    float radius = 0.0f;
    float dpi = 96.0f;
    ElevationLayers layers;
    Color color;

    [[nodiscard]] bool operator==(const ShadowKey& other) const noexcept {
        return bakeWidth == other.bakeWidth && bakeHeight == other.bakeHeight && radius == other.radius &&
               dpi == other.dpi && SameLayer(layers.key, other.layers.key) &&
               SameLayer(layers.ambient, other.layers.ambient) && color.r == other.color.r &&
               color.g == other.color.g && color.b == other.color.b && color.a == other.color.a;
    }
};

struct ShadowEntry {
    ShadowKey key;
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    std::uint64_t lastUse = 0;
};

// Device-dependent resources of the UI thread, per Direct2D device. Composition surfaces share the
// GraphicsDevice's device; native forms paint through their own DC render target on the same
// thread. A few devices are kept so alternating between them never discards the other's baked
// shadows; the least recently used one goes first (a replaced device simply ages out).
constexpr std::size_t kMaxCachedDevices = 4;
struct DeviceCache {
    winrt::com_ptr<ID2D1Device> device;
    std::vector<ShadowEntry> shadows;
    winrt::com_ptr<ID2D1DeviceContext> bakeContext;  // private context for baking shadows
    winrt::com_ptr<ID2D1BitmapBrush1> noiseBrush;
    float noiseDpi = 0.0f;
    float noiseAlpha = -1.0f;
    std::uint64_t clock = 0;
    std::uint64_t lastUse = 0;
};

thread_local std::vector<DeviceCache> threadCaches;
thread_local std::uint64_t threadCacheClock = 0;

DeviceCache* CacheFor(ID2D1DeviceContext* context) {
    winrt::com_ptr<ID2D1Device> device;
    context->GetDevice(device.put());
    if (!device) return nullptr;
    ++threadCacheClock;
    for (auto& cache : threadCaches) {
        if (cache.device.get() == device.get()) {
            cache.lastUse = threadCacheClock;
            return &cache;
        }
    }
    if (threadCaches.size() >= kMaxCachedDevices) {
        threadCaches.erase(std::min_element(threadCaches.begin(), threadCaches.end(),
            [](const DeviceCache& a, const DeviceCache& b) { return a.lastUse < b.lastUse; }));
    }
    DeviceCache cache;
    cache.device = device;
    cache.lastUse = threadCacheClock;
    threadCaches.push_back(std::move(cache));
    return &threadCaches.back();
}

// Bakes on a private device context of the same Direct2D device, so the caller's target, transform,
// clips and layers (a tooltip's fade layer, for example) never affect the cached bitmap and are never
// touched while the caller is drawing. Bitmaps are device resources, usable by every context.
winrt::com_ptr<ID2D1Bitmap1> BakeShadow(DeviceCache& cache, const ShadowKey& key, float extent) {
    const float width = key.bakeWidth + 2.0f * extent;
    const float height = key.bakeHeight + 2.0f * extent;
    const float scale = key.dpi / 96.0f;
    const auto pixels = D2D1::SizeU(static_cast<UINT32>(std::ceil(width * scale)), static_cast<UINT32>(std::ceil(height * scale)));
    if (pixels.width == 0 || pixels.height == 0 || pixels.width > 4096 || pixels.height > 4096) return nullptr;
    if (!cache.bakeContext &&
        FAILED(cache.device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, cache.bakeContext.put()))) {
        return nullptr;
    }
    ID2D1DeviceContext* context = cache.bakeContext.get();

    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    const auto properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        key.dpi, key.dpi);
    if (FAILED(context->CreateBitmap(pixels, nullptr, 0, properties, bitmap.put()))) return nullptr;

    winrt::com_ptr<ID2D1Factory> factory;
    context->GetFactory(factory.put());
    const D2D1_ROUNDED_RECT silhouette{D2D1::RectF(extent, extent, extent + key.bakeWidth, extent + key.bakeHeight),
                                       key.radius, key.radius};
    winrt::com_ptr<ID2D1RectangleGeometry> outer;
    winrt::com_ptr<ID2D1RoundedRectangleGeometry> interior;
    winrt::com_ptr<ID2D1PathGeometry> exterior;
    winrt::com_ptr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreateRectangleGeometry(D2D1::RectF(0, 0, width, height), outer.put())) ||
        FAILED(factory->CreateRoundedRectangleGeometry(silhouette, interior.put())) ||
        FAILED(factory->CreatePathGeometry(exterior.put())) || FAILED(exterior->Open(sink.put())) ||
        FAILED(outer->CombineWithGeometry(interior.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.get())) ||
        FAILED(sink->Close())) {
        return nullptr;
    }

    context->SetDpi(key.dpi, key.dpi);
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    // The silhouette, recorded once as the shadow effects' input.
    winrt::com_ptr<ID2D1CommandList> list;
    if (FAILED(context->CreateCommandList(list.put()))) return nullptr;
    context->SetTarget(list.get());
    context->BeginDraw();
    if (auto black = Solid(context, Color{0, 0, 0, 1})) context->FillRoundedRectangle(silhouette, black.get());
    const bool recorded = SUCCEEDED(context->EndDraw()) && SUCCEEDED(list->Close());
    context->SetTarget(nullptr);
    if (!recorded) return nullptr;

    context->SetTarget(bitmap.get());
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    // Only the outside of the silhouette: the shadow never darkens the translucent panel itself.
    context->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), exterior.get()), nullptr);
    for (const ShadowLayer* layer : {&key.layers.ambient, &key.layers.key}) {
        if (!(layer->alpha > 0.0f)) continue;
        winrt::com_ptr<ID2D1Effect> shadow;
        if (FAILED(context->CreateEffect(CLSID_D2D1Shadow, shadow.put()))) continue;
        shadow->SetInput(0, list.get());
        shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, std::max(0.0f, layer->blur));
        shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(key.color.r, key.color.g, key.color.b, Clamp01(layer->alpha)));
        context->DrawImage(shadow.get(), D2D1::Point2F(0.0f, layer->offsetY));
    }
    context->PopLayer();
    const HRESULT drawn = context->EndDraw();
    context->SetTarget(nullptr);
    if (drawn == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) cache.bakeContext = nullptr;
    return SUCCEEDED(drawn) ? bitmap : nullptr;
}

ID2D1Bitmap1* CachedShadow(ID2D1DeviceContext* context, const ShadowKey& key, float extent) {
    auto* cache = CacheFor(context);
    if (!cache) return nullptr;
    ++cache->clock;
    for (auto& entry : cache->shadows) {
        if (entry.key == key) {
            entry.lastUse = cache->clock;
            return entry.bitmap.get();
        }
    }
    auto bitmap = BakeShadow(*cache, key, extent);
    if (!bitmap) return nullptr;
    if (cache->shadows.size() >= kMaxCachedShadows) {
        const auto oldest = std::min_element(cache->shadows.begin(), cache->shadows.end(),
                                             [](const ShadowEntry& a, const ShadowEntry& b) { return a.lastUse < b.lastUse; });
        cache->shadows.erase(oldest);
    }
    cache->shadows.push_back(ShadowEntry{key, bitmap, cache->clock});
    return cache->shadows.back().bitmap.get();
}

ID2D1BitmapBrush1* NoiseBrush(ID2D1DeviceContext* context, float meanAlpha) {
    auto* cache = CacheFor(context);
    if (!cache) return nullptr;
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    context->GetDpi(&dpiX, &dpiY);
    if (cache->noiseBrush && cache->noiseDpi == dpiX && cache->noiseAlpha == meanAlpha) return cache->noiseBrush.get();
    cache->noiseBrush = nullptr;
    const auto pixels = MakeNoiseTile(kNoiseTileSize, kNoiseSeed, meanAlpha);
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    const auto properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX, dpiX);
    if (FAILED(context->CreateBitmap(D2D1::SizeU(kNoiseTileSize, kNoiseTileSize), pixels.data(), kNoiseTileSize * 4,
                                     properties, bitmap.put()))) {
        return nullptr;
    }
    winrt::com_ptr<ID2D1BitmapBrush1> brush;
    if (FAILED(context->CreateBitmapBrush(bitmap.get(),
                                          D2D1::BitmapBrushProperties1(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP,
                                                                       D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
                                          brush.put()))) {
        return nullptr;
    }
    cache->noiseBrush = brush;
    cache->noiseDpi = dpiX;
    cache->noiseAlpha = meanAlpha;
    return cache->noiseBrush.get();
}

// Rounds DIP coordinates to device pixels so adjacent slices meet without seams.
D2D1_RECT_F SnapToPixels(const D2D1_RECT_F& rect, float scale) noexcept {
    const auto snap = [scale](float value) { return std::round(value * scale) / scale; };
    return D2D1::RectF(snap(rect.left), snap(rect.top), snap(rect.right), snap(rect.bottom));
}

float ContextScale(ID2D1RenderTarget* target) noexcept {
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    target->GetDpi(&dpiX, &dpiY);
    D2D1_MATRIX_3X2_F transform{};
    target->GetTransform(&transform);
    const float transformScale = std::sqrt(transform._11 * transform._11 + transform._12 * transform._12);
    const float scale = dpiX / 96.0f * (std::isfinite(transformScale) && transformScale > 0.0f ? transformScale : 1.0f);
    return scale > 0.0f ? scale : 1.0f;
}

void StrokeRing(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float cornerRadius, float offset, float width,
                const Color& color) {
    if (!(width > 0.0f) || !(color.a > 0.0f)) return;
    const D2D1_RECT_F ring = Inflate(rect, offset, offset);
    const float radius = std::max(0.0f, cornerRadius + offset);
    if (auto brush = Solid(target, color)) target->DrawRoundedRectangle(D2D1::RoundedRect(ring, radius, radius), brush.get(), width);
}

}  // namespace

void ReleaseGlassResources() noexcept { threadCaches.clear(); }

float GlassTintAlpha(const DesignTokens& tokens, const GlassStyle& style) noexcept {
    const float opacity = std::clamp(style.opacity, 0.3f, 1.0f);
    if (tokens.mode == ThemeMode::HighContrast || !tokens.transparencyEnabled) return 1.0f;
    const float tint = style.backdropBlur ? tokens.glass.tintAlphaWithBlur : tokens.glass.tintAlphaWithoutBlur;
    // The default preference (.8) maps to the material tint; maximum opacity is truly opaque.
    // A clear fallback remains translucent on Windows 10 and when host blur is unavailable.
    if (opacity > 0.8f) return std::clamp(tint + (1.0f - tint) * (opacity - 0.8f) / 0.2f, 0.0f, 1.0f);
    const float floor = style.backdropBlur ? 0.18f : 0.60f;
    return std::clamp(tint * opacity / 0.8f, floor, 1.0f);
}

const ElevationLayers& ElevationFor(const DesignTokens& tokens, Elevation elevation) noexcept {
    static const ElevationLayers none{};
    switch (elevation) {
        case Elevation::Tooltip: return tokens.elevation.tooltip;
        case Elevation::Bar: return tokens.elevation.bar;
        case Elevation::Dock: return tokens.elevation.dock;
        case Elevation::Popup: return tokens.elevation.popup;
        case Elevation::Lift: return tokens.elevation.lift;
        case Elevation::None: break;
    }
    return none;
}

float ShadowExtent(const ElevationLayers& layers) noexcept {
    float extent = 0.0f;
    for (const ShadowLayer* layer : {&layers.key, &layers.ambient}) {
        if (!(layer->alpha > 0.0f)) continue;
        const float blur = std::isfinite(layer->blur) ? std::max(0.0f, layer->blur) : 0.0f;
        const float offset = std::isfinite(layer->offsetY) ? std::fabs(layer->offsetY) : 0.0f;
        extent = std::max(extent, 3.0f * blur + offset);
    }
    return std::ceil(extent);
}

float ShadowBakeLength(float length, float extent, float cornerRadius) noexcept {
    if (!std::isfinite(length) || length <= 0.0f) return 0.0f;
    const float stretchable = 2.0f * std::max(0.0f, extent) + 2.0f * std::max(0.0f, cornerRadius) + kStretchMiddle;
    return std::min(length, stretchable);
}

NineSlice NineSliceRects(const D2D1_SIZE_F& sourceSize, float cornerX, float cornerY, const D2D1_RECT_F& target) noexcept {
    NineSlice slices;
    const float sourceWidth = std::max(0.0f, sourceSize.width);
    const float sourceHeight = std::max(0.0f, sourceSize.height);
    const float targetWidth = std::max(0.0f, target.right - target.left);
    const float targetHeight = std::max(0.0f, target.bottom - target.top);
    const float sx = std::clamp(cornerX, 0.0f, sourceWidth / 2.0f);
    const float sy = std::clamp(cornerY, 0.0f, sourceHeight / 2.0f);
    const float tx = std::min(sx, targetWidth / 2.0f);
    const float ty = std::min(sy, targetHeight / 2.0f);
    const float sourceX[] = {0.0f, sx, sourceWidth - sx, sourceWidth};
    const float sourceY[] = {0.0f, sy, sourceHeight - sy, sourceHeight};
    const float targetX[] = {target.left, target.left + tx, target.left + targetWidth - tx, target.left + targetWidth};
    const float targetY[] = {target.top, target.top + ty, target.top + targetHeight - ty, target.top + targetHeight};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const auto index = static_cast<std::size_t>(row * 3 + column);
            slices.source[index] = D2D1::RectF(sourceX[column], sourceY[row], sourceX[column + 1], sourceY[row + 1]);
            slices.target[index] = D2D1::RectF(targetX[column], targetY[row], targetX[column + 1], targetY[row + 1]);
        }
    }
    return slices;
}

std::vector<std::uint8_t> MakeNoiseTile(std::uint32_t size, std::uint32_t seed, float meanAlpha) {
    size = std::min<std::uint32_t>(size, 1024);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4u, 0);
    std::uint32_t state = seed ? seed : 0x9E3779B9u;
    const float peak = std::clamp(std::isfinite(meanAlpha) ? meanAlpha : 0.0f, 0.0f, 0.5f) * 2.0f * 255.0f;
    for (std::size_t index = 0; index < pixels.size(); index += 4) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const float unit = static_cast<float>(state >> 8) / 16777215.0f;
        const auto alpha = static_cast<std::uint8_t>(std::lround(std::clamp(unit * peak, 0.0f, 255.0f)));
        const std::uint8_t value = (state & 1u) != 0 ? alpha : 0;  // premultiplied white or black
        pixels[index] = value;
        pixels[index + 1] = value;
        pixels[index + 2] = value;
        pixels[index + 3] = alpha;
    }
    return pixels;
}

D2D1_INTERPOLATION_MODE IconInterpolation(float sourcePx, float targetPx) noexcept {
    if (!(sourcePx > 0.0f) || !(targetPx > 0.0f) || !std::isfinite(sourcePx) || !std::isfinite(targetPx))
        return D2D1_INTERPOLATION_MODE_LINEAR;
    return sourcePx / targetPx > 1.2f ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_LINEAR;
}

void PaintElevation(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius, const DesignTokens& tokens,
                    Elevation elevation, float opacity) {
    if (!context || elevation == Elevation::None || tokens.mode == ThemeMode::HighContrast || !ValidRect(rect)) return;
    opacity = Clamp01(opacity);
    if (opacity <= 0.0f) return;
    const auto& layers = ElevationFor(tokens, elevation);
    if (!(layers.key.alpha > 0.0f) && !(layers.ambient.alpha > 0.0f)) return;
    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    const float radius = Quantize(SafeRadius(rect, cornerRadius));
    const float extent = ShadowExtent(layers);
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    context->GetDpi(&dpiX, &dpiY);

    ShadowKey key;
    key.bakeWidth = Quantize(ShadowBakeLength(width, extent, radius));
    key.bakeHeight = Quantize(ShadowBakeLength(height, extent, radius));
    key.radius = std::min(radius, std::min(key.bakeWidth, key.bakeHeight) / 2.0f);
    key.dpi = dpiX;
    key.layers = layers;
    key.color = tokens.elevation.color;
    if (key.bakeWidth <= 0.0f || key.bakeHeight <= 0.0f) return;
    auto* bitmap = CachedShadow(context, key, extent);
    if (!bitmap) return;

    const D2D1_SIZE_F source{key.bakeWidth + 2.0f * extent, key.bakeHeight + 2.0f * extent};
    const bool stretchX = key.bakeWidth < width;
    const bool stretchY = key.bakeHeight < height;
    const float cornerX = stretchX ? (source.width - kStretchMiddle) / 2.0f : source.width / 2.0f;
    const float cornerY = stretchY ? (source.height - kStretchMiddle) / 2.0f : source.height / 2.0f;
    const auto slices = NineSliceRects(source, cornerX, cornerY, Inflate(rect, extent, extent));
    const float scale = ContextScale(context);
    for (std::size_t index = 0; index < slices.target.size(); ++index) {
        const auto target = SnapToPixels(slices.target[index], scale);
        const auto& from = slices.source[index];
        if (target.right <= target.left || target.bottom <= target.top || from.right <= from.left || from.bottom <= from.top)
            continue;
        context->DrawBitmap(bitmap, &target, opacity, D2D1_INTERPOLATION_MODE_LINEAR, &from, nullptr);
    }
}

void PaintGlassPanel(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, const DesignTokens& tokens,
                     const GlassStyle& style) {
    if (!context || !ValidRect(rect)) return;
    const float radius = SafeRadius(rect, style.cornerRadius);
    const D2D1_ROUNDED_RECT shape{rect, radius, radius};
    const bool highContrast = tokens.mode == ThemeMode::HighContrast;
    const bool neutral = style.neutral || style.elevation == Elevation::Dock || style.elevation == Elevation::Bar;
    if (style.shadow && !highContrast) PaintElevation(context, rect, radius, tokens, style.elevation);

    if (auto tint = Solid(context, tokens.glass.tint.WithAlpha(GlassTintAlpha(tokens, style)))) {
        context->FillRoundedRectangle(shape, tint.get());
    }

    const float height = rect.bottom - rect.top;
    if (!highContrast && style.noise && tokens.transparencyEnabled && tokens.glass.noiseAlpha > 0.0f) {
        if (auto* grain = NoiseBrush(context, tokens.glass.noiseAlpha)) context->FillRoundedRectangle(shape, grain);
    }
    if (!highContrast && !neutral && style.highlight) {
        // Top-weighted sheen: strongest along the top edge, gone after highlightFraction of the height.
        const float fraction = std::clamp(tokens.glass.highlightFraction, 0.05f, 1.0f);
        if (tokens.glass.highlight.a > 0.0f) {
            if (auto sheen = Vertical(context, rect.top, rect.top + height * fraction, tokens.glass.highlight,
                                      tokens.glass.highlight.WithAlpha(0.0f))) {
                context->FillRoundedRectangle(shape, sheen.get());
            }
        }
        // 1-DIP inner rim just inside the border, fading out towards the middle.
        if (tokens.glass.rim.a > 0.0f && radius >= 1.5f && height > 4.0f) {
            const D2D1_ROUNDED_RECT rim{Inflate(rect, -1.5f, -1.5f), radius - 1.5f, radius - 1.5f};
            if (auto brush = Vertical(context, rect.top, rect.top + std::max(8.0f, height * 0.5f), tokens.glass.rim,
                                      tokens.glass.rim.WithAlpha(0.0f))) {
                context->DrawRoundedRectangle(rim, brush.get(), 1.0f);
            }
        }
    }

    if (!style.border) return;
    // Hairline border inset by half a DIP so it stays inside the panel.
    const D2D1_ROUNDED_RECT borderShape{Inflate(rect, -0.5f, -0.5f), std::max(0.0f, radius - 0.5f),
                                        std::max(0.0f, radius - 0.5f)};
    const float borderWidth = highContrast ? 2.0f : 1.0f;
    if (neutral || highContrast) {
        if (auto border = Solid(context, highContrast ? tokens.glass.borderTop : tokens.glass.uniformBorder))
            context->DrawRoundedRectangle(borderShape, border.get(), borderWidth);
    } else {
        if (auto border = Vertical(context, rect.top, rect.bottom, tokens.glass.borderTop, tokens.glass.borderBottom))
            context->DrawRoundedRectangle(borderShape, border.get(), borderWidth);
    }
}

void PaintFocusRing(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float cornerRadius, const DesignTokens& tokens) {
    if (!target || !ValidRect(rect)) return;
    const auto& focus = tokens.focus;
    const float gap = std::max(0.0f, focus.gap);
    const float inner = std::max(0.0f, focus.innerWidth);
    const float outer = std::max(0.0f, focus.outerWidth);
    const float radius = std::max(0.0f, cornerRadius);
    StrokeRing(target, rect, radius, gap + inner / 2.0f, inner, focus.inner);
    StrokeRing(target, rect, radius, gap + inner + outer / 2.0f, outer, focus.outer);
}

void PaintFocusRing(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius,
                    const DesignTokens& tokens) {
    PaintFocusRing(static_cast<ID2D1RenderTarget*>(context), rect, cornerRadius, tokens);
}

void PaintRunningIndicator(ID2D1DeviceContext* context, const D2D1_RECT_F& pill, const DesignTokens& tokens,
                           IndicatorState state, float opacity) {
    if (!context || !ValidRect(pill)) return;
    opacity = Clamp01(opacity);
    if (opacity <= 0.0f) return;
    Color color = tokens.runningIndicator;
    switch (state) {
        case IndicatorState::Running: break;
        case IndicatorState::Active: color = tokens.accentRoles.fill; break;
        case IndicatorState::Attention: color = tokens.caution; break;
    }
    if (tokens.mode == ThemeMode::HighContrast && state != IndicatorState::Active) color = tokens.textPrimary;
    const float radius = std::min(pill.right - pill.left, pill.bottom - pill.top) / 2.0f;
    if (auto brush = Solid(context, color.WithAlpha(color.a * opacity))) {
        context->FillRoundedRectangle(D2D1::RoundedRect(pill, radius, radius), brush.get());
    }
}

void PaintLiftShadow(ID2D1DeviceContext* context, const D2D1_RECT_F& iconRect, float cornerRadius,
                     const DesignTokens& tokens, float lift) {
    if (!context || !ValidRect(iconRect) || tokens.mode == ThemeMode::HighContrast) return;
    lift = Clamp01(lift);
    if (lift <= 0.0f) return;
    const auto& layers = tokens.elevation.lift;
    if (!(layers.key.alpha > 0.0f) && !(layers.ambient.alpha > 0.0f)) return;
    // Baked for the icon size rounded to 8 DIPs and scaled, so a magnified, moving icon reuses one bitmap.
    const float width = iconRect.right - iconRect.left;
    const float height = iconRect.bottom - iconRect.top;
    const float size = std::max(width, height);
    const float bucket = std::max(8.0f, std::round(size / 8.0f) * 8.0f);
    const float extent = ShadowExtent(layers);
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    context->GetDpi(&dpiX, &dpiY);
    ShadowKey key;
    key.bakeWidth = bucket;
    key.bakeHeight = bucket;
    key.radius = Quantize(std::clamp(cornerRadius * bucket / size, 0.0f, bucket / 2.0f));
    key.dpi = dpiX;
    key.layers = layers;
    key.color = tokens.elevation.color;
    auto* bitmap = CachedShadow(context, key, extent);
    if (!bitmap) return;
    const D2D1_RECT_F source = D2D1::RectF(0.0f, 0.0f, bucket + 2.0f * extent, bucket + 2.0f * extent);
    const D2D1_RECT_F target = Inflate(iconRect, extent * width / bucket, extent * height / bucket);
    context->DrawBitmap(bitmap, &target, lift, D2D1_INTERPOLATION_MODE_LINEAR, &source, nullptr);
}

void PaintItemHighlight(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius,
                        const DesignTokens& tokens, float hover, float pressed) {
    if (!context || !ValidRect(rect)) return;
    hover = Clamp01(hover);
    pressed = Clamp01(pressed);
    const auto shape = D2D1::RoundedRect(rect, SafeRadius(rect, cornerRadius), SafeRadius(rect, cornerRadius));
    if (hover > 0.0f) {
        if (auto brush = Solid(context, tokens.hoverOverlay.WithAlpha(tokens.hoverOverlay.a * hover)))
            context->FillRoundedRectangle(shape, brush.get());
    }
    if (pressed > 0.0f) {
        if (auto brush = Solid(context, tokens.pressedOverlay.WithAlpha(tokens.pressedOverlay.a * pressed)))
            context->FillRoundedRectangle(shape, brush.get());
    }
    if (tokens.mode == ThemeMode::HighContrast && std::max(hover, pressed) >= 0.5f) {
        if (auto brush = Solid(context, tokens.accentRoles.fill)) {
            const auto inner = Inflate(rect, -0.5f, -0.5f);
            context->DrawRoundedRectangle(D2D1::RoundedRect(inner, SafeRadius(inner, cornerRadius - 0.5f),
                                                            SafeRadius(inner, cornerRadius - 0.5f)),
                                          brush.get(), 1.0f);
        }
    }
}

void PaintSelectionPill(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float cornerRadius, const DesignTokens& tokens,
                        bool selected, bool hover) {
    if (!target || !ValidRect(rect) || (!selected && !hover)) return;
    const float radius = SafeRadius(rect, cornerRadius);
    const auto shape = D2D1::RoundedRect(rect, radius, radius);
    if (tokens.mode == ThemeMode::HighContrast) {
        if (selected) {
            if (auto brush = Solid(target, tokens.surface.selectedFill)) target->FillRoundedRectangle(shape, brush.get());
        } else if (auto brush = Solid(target, tokens.accentRoles.fill)) {
            target->DrawRoundedRectangle(D2D1::RoundedRect(Inflate(rect, -0.5f, -0.5f), std::max(0.0f, radius - 0.5f),
                                                           std::max(0.0f, radius - 0.5f)),
                                         brush.get(), 1.0f);
        }
        return;
    }
    const Color fill = tokens.hoverOverlay.WithAlpha(tokens.hoverOverlay.a * (selected ? 1.0f : 0.6f));
    if (auto brush = Solid(target, fill)) target->FillRoundedRectangle(shape, brush.get());
}

void PaintAccentIndicator(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, const DesignTokens& tokens) {
    if (!target || !ValidRect(rect)) return;
    const Color color = tokens.mode == ThemeMode::HighContrast ? tokens.surface.selectedText : tokens.accentRoles.fill;
    const float radius = std::min(rect.right - rect.left, rect.bottom - rect.top) / 2.0f;
    if (auto brush = Solid(target, color)) target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.get());
}

void DrawIconBitmap(ID2D1DeviceContext* context, ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect, float opacity) {
    if (!context || !bitmap || !ValidRect(rect)) return;
    const auto pixels = bitmap->GetPixelSize();
    const float scale = ContextScale(context);
    const float targetPx = std::max(rect.right - rect.left, rect.bottom - rect.top) * scale;
    const auto mode = IconInterpolation(static_cast<float>(std::max(pixels.width, pixels.height)), targetPx);
    context->DrawBitmap(bitmap, &rect, Clamp01(opacity), mode, nullptr, nullptr);
}

void PaintFallbackIcon(ID2D1DeviceContext* context, IDWriteFactory* dwrite, const D2D1_RECT_F& rect,
                       wchar_t initial, const DesignTokens& tokens) {
    if (!context || !ValidRect(rect)) return;
    const float size = std::min(rect.right - rect.left, rect.bottom - rect.top);
    const float radius = size * 0.24f;
    const D2D1_ROUNDED_RECT tile{rect, radius, radius};
    if (auto gradient = Vertical(context, rect.top, rect.bottom, tokens.fallbackIconBackground,
                                 tokens.fallbackIconBackground.WithAlpha(0.8f))) {
        context->FillRoundedRectangle(tile, gradient.get());
    }
    if (!dwrite || initial == 0) return;
    winrt::com_ptr<IDWriteTextFormat> format;
    if (FAILED(dwrite->CreateTextFormat(tokens.displayFontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size * 0.46f, L"",
                                        format.put()))) {
        return;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    const wchar_t text[2] = {static_cast<wchar_t>(std::towupper(initial)), 0};
    if (auto brush = Solid(context, tokens.textPrimary)) context->DrawTextW(text, 1, format.get(), rect, brush.get());
}

}  // namespace aether::graphics
