// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/LiquidRenderer.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string_view>
#include <utility>

namespace aether::graphics {
namespace {
constexpr std::string_view kShader =
#include "graphics/LiquidShader.inc"
    ;
constexpr UINT kMarkSize = 512;

// The constant buffer, laid out as LiquidShader.inc's cbuffer Scene.
struct alignas(16) Constants {
    float b[2][4];
    float s[2][4];
    float picture[4];
    float logo[4];
    float size[2];
    float dpr;
    float k;
    float style, lens, veil, blur;
    float background, shadow, lodBias, markLod;
    float transparent, edgeBend, glassOn, glassLod;
    float glassSaturation, glassScale, glassOffset, glassOpacity;
    float glassTint[4];
};
static_assert(sizeof(Constants) == 192, "Constants must match the shader's cbuffer");

Error Failed(const char* what, HRESULT hr) {
    return MakeError(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ? ErrorCode::Unavailable : ErrorCode::Platform, what, hr);
}

// The Aether mark's two ribbons (resources/branding/svg/aether-mark.svg), 1000 units square, as polygons.
std::vector<std::vector<D2D1_POINT_2F>> MarkOutlines() {
    struct Segment { bool curve; D2D1_POINT_2F a, b, c; };
    const auto pt = [](float x, float y) { return D2D1::Point2F(x, y); };
    const auto flatten = [](D2D1_POINT_2F start, const std::vector<Segment>& segments) {
        std::vector<D2D1_POINT_2F> out{start};
        D2D1_POINT_2F from = start;
        for (const auto& s : segments) {
            if (!s.curve) { out.push_back(s.a); from = s.a; continue; }
            for (int i = 1; i <= 48; ++i) {
                const float t = static_cast<float>(i) / 48.0f, u = 1.0f - t;
                out.push_back(D2D1::Point2F(u * u * u * from.x + 3 * u * u * t * s.a.x + 3 * u * t * t * s.b.x + t * t * t * s.c.x,
                                            u * u * u * from.y + 3 * u * u * t * s.a.y + 3 * u * t * t * s.b.y + t * t * t * s.c.y));
            }
            from = s.c;
        }
        return out;
    };
    return {
        flatten(pt(150, 610), {{true, pt(260, 340), pt(445, 170), pt(810, 240)}, {false, pt(700, 390), {}, {}},
                               {true, pt(500, 350), pt(365, 425), pt(280, 640)}}),
        flatten(pt(245, 770), {{false, pt(345, 600), {}, {}}, {true, pt(485, 655), pt(615, 575), pt(725, 375)},
                               {false, pt(850, 375), {}, {}}, {true, pt(715, 700), pt(515, 850), pt(245, 770)}}),
    };
}

// The mark's shape as white premultiplied BGRA, 4 x 4 samples a pixel, by scanlines (even-odd).
std::vector<std::uint8_t> RasterizeMark(UINT size) {
    const auto outlines = MarkOutlines();
    constexpr int kSub = 4;
    std::vector<float> coverage(static_cast<std::size_t>(size) * size, 0.0f);
    std::vector<float> crossings;
    const float scale = static_cast<float>(size) / 1000.0f;
    for (UINT row = 0; row < size * kSub; ++row) {
        const float y = (static_cast<float>(row) + 0.5f) / kSub / scale;
        crossings.clear();
        for (const auto& outline : outlines)
            for (std::size_t i = 0; i < outline.size(); ++i) {
                const auto& a = outline[i];
                const auto& b = outline[(i + 1) % outline.size()];
                if ((a.y <= y) == (b.y <= y)) continue;
                crossings.push_back((a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y)) * scale);
            }
        std::sort(crossings.begin(), crossings.end());
        auto* line = coverage.data() + static_cast<std::size_t>(row / kSub) * size;
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            for (int sub = 0; sub < kSub; ++sub) {
                const float offset = (static_cast<float>(sub) + 0.5f) / kSub;
                const int from = std::max(0, static_cast<int>(std::ceil(crossings[i] - offset)));
                const int to = std::min(static_cast<int>(size) - 1, static_cast<int>(std::floor(crossings[i + 1] - offset)));
                for (int x = from; x <= to; ++x) line[x] += 1.0f / (kSub * kSub);
            }
        }
    }
    std::vector<std::uint8_t> bgra(coverage.size() * 4u);
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        const auto value = static_cast<std::uint8_t>(std::lround(std::min(1.0f, coverage[i]) * 255.0f));
        bgra[i * 4 + 0] = bgra[i * 4 + 1] = bgra[i * 4 + 2] = bgra[i * 4 + 3] = value;
    }
    return bgra;
}
}  // namespace

MaterialScene SceneFromArrival(const ArrivalFrame& frame, MaterialStyle style, D2D1_POINT_2F originPx, float pxPerDip) {
    MaterialScene scene;
    scene.style = style;
    scene.pxPerDip = pxPerDip;
    for (std::size_t i = 0; i < 2; ++i) {
        auto body = frame.bodies[i];
        body.cx = body.cx * pxPerDip - originPx.x;
        body.cy = body.cy * pxPerDip - originPx.y;
        body.hx *= pxPerDip;
        body.hy *= pxPerDip;
        body.radius *= pxPerDip;
        scene.bodies[i] = body;
    }
    scene.smoothness = frame.smoothness * pxPerDip;
    scene.lens = frame.lens;
    scene.veil = frame.veil;
    scene.blur = frame.blur;
    scene.logoAlpha = frame.logoAlpha;
    scene.logoCx = frame.logoCx * pxPerDip - originPx.x;
    scene.logoCy = frame.logoCy * pxPerDip - originPx.y;
    scene.logoHalf = frame.logoHalf * pxPerDip;
    return scene;
}

struct LiquidRenderer::Impl {
    winrt::com_ptr<ID3D11Device1> device;
    winrt::com_ptr<ID3D11DeviceContext1> context;
    winrt::com_ptr<ID3DDeviceContextState> state;
    winrt::com_ptr<ID3D11VertexShader> vertexShader;
    winrt::com_ptr<ID3D11PixelShader> pixelShader;
    winrt::com_ptr<ID3D11Buffer> constants;
    winrt::com_ptr<ID3D11SamplerState> sampler;
    winrt::com_ptr<ID3D11ShaderResourceView> pictureView, markView;
    struct Target { winrt::com_ptr<ID3D11Texture2D> texture; winrt::com_ptr<ID3D11RenderTargetView> view; winrt::com_ptr<ID3D11Texture2D> staging; };
    std::map<std::pair<UINT, UINT>, Target> targets;   // one per size: the Dock, the Top Bar, the arrival, previews
    // D69: the texture a Direct2D context was drawing into when the arrival last rendered straight into it, and its view.
    winrt::com_ptr<ID3D11Texture2D> directTexture;
    winrt::com_ptr<ID3D11RenderTargetView> directView;
    std::shared_ptr<const std::vector<std::uint8_t>> picture;
    UINT pictureWidth = 0, pictureHeight = 0;
    D2D1_RECT_F placement{};
    std::uint64_t generation = 0;
    bool pictureUploaded = false;
    std::vector<std::uint8_t> mark;

    Result<winrt::com_ptr<ID3D11ShaderResourceView>> MipmappedTexture(const std::uint8_t* bgra, UINT width, UINT height) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 0;   // the whole chain: the blur reads a level of it
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::com_ptr<ID3D11ShaderResourceView> view;
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, texture.put());
        if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(texture.get(), nullptr, view.put());
        if (FAILED(hr)) return Failed("material texture could not be created", hr);
        context->UpdateSubresource(texture.get(), 0, nullptr, bgra, width * 4u, 0);
        context->GenerateMips(view.get());
        return view;
    }

    Result<void> EnsureResources() {
        if (!pictureUploaded && (picture || !pictureView)) {   // the stand-in is made once, not with every frame
            static const std::uint8_t kDark[4] = {24, 22, 26, 255};   // until the picture arrives: a quiet dark
            auto view = picture && pictureWidth && pictureHeight ? MipmappedTexture(picture->data(), pictureWidth, pictureHeight)
                                                                 : MipmappedTexture(kDark, 1, 1);
            if (!view) return view.error();
            pictureView = std::move(*view);
            pictureUploaded = picture != nullptr;
            // On the device now: the CPU copy (a whole screen of pixels) is not kept beside it.
            picture.reset();
        }
        if (!markView) {
            if (mark.empty()) mark = RasterizeMark(kMarkSize);
            auto view = MipmappedTexture(mark.data(), kMarkSize, kMarkSize);
            if (!view) return view.error();
            markView = std::move(*view);
        }
        return Ok();
    }

    Result<Target*> TargetFor(D2D1_SIZE_U size) {
        // A Dock that changes width while it magnifies asks for many sizes: keep only the last few.
        if (!targets.contains({size.width, size.height}) && targets.size() >= 6) targets.clear();
        auto& target = targets[{size.width, size.height}];
        if (target.texture) return &target;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = size.width;
        desc.Height = size.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, target.texture.put());
        if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(target.texture.get(), nullptr, target.view.put());
        if (FAILED(hr)) {
            targets.erase({size.width, size.height});
            return Failed("material target could not be created", hr);
        }
        return &target;
    }

    Result<void> Draw(const MaterialScene& scene, Target& target, D2D1_SIZE_U size) {
        return Draw(scene, target.view.get(), D3D11_VIEWPORT{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height), 0.0f, 1.0f});
    }
    // `viewport` is where the frame goes in `view`'s texture; the scene's coordinates are in that texture's pixels.
    Result<void> Draw(const MaterialScene& scene, ID3D11RenderTargetView* view, const D3D11_VIEWPORT& viewport) {
        const D2D1_SIZE_U size{static_cast<UINT32>(viewport.Width), static_cast<UINT32>(viewport.Height)};
        if (auto ready = EnsureResources(); !ready) return ready;
        Constants c{};
        for (int i = 0; i < 2; ++i) {
            const auto& b = scene.bodies[static_cast<std::size_t>(i)];
            const float smaller = std::min(b.hx, b.hy);
            const float bevel = smaller * 0.95f + (scene.bevelDip * scene.pxPerDip - smaller * 0.95f) * scene.lens;
            c.b[i][0] = b.cx; c.b[i][1] = b.cy; c.b[i][2] = std::max(b.hx, 0.01f); c.b[i][3] = std::max(b.hy, 0.01f);
            c.s[i][0] = b.radius; c.s[i][1] = b.present ? 1.0f : 0.0f; c.s[i][2] = std::max(bevel, 1.0f); c.s[i][3] = b.a2;
        }
        const float pictureWidthPx = std::max(scene.picture.right - scene.picture.left, 1.0f);
        c.picture[0] = scene.picture.left; c.picture[1] = scene.picture.top;
        c.picture[2] = pictureWidthPx; c.picture[3] = std::max(scene.picture.bottom - scene.picture.top, 1.0f);
        c.logo[0] = scene.logoCx; c.logo[1] = scene.logoCy; c.logo[2] = std::max(scene.logoHalf, 1.0f); c.logo[3] = scene.logoAlpha;
        c.size[0] = static_cast<float>(size.width); c.size[1] = static_cast<float>(size.height);
        c.dpr = std::max(scene.pxPerDip, 0.25f);
        c.k = scene.smoothness;
        c.style = scene.style == MaterialStyle::Grain ? 1.0f : 0.0f;
        c.lens = scene.lens;
        c.veil = scene.veil;
        c.blur = scene.blur;
        c.background = scene.background ? 1.0f : 0.0f;
        c.shadow = scene.shadow;
        c.lodBias = pictureUploaded ? std::max(-2.0f, std::log2(static_cast<float>(pictureWidth) / pictureWidthPx)) : 0.0f;
        c.markLod = std::max(0.0f, std::log2(static_cast<float>(kMarkSize) / std::max(2.0f * scene.logoHalf, 1.0f)));
        c.transparent = scene.transparent ? 1.0f : 0.0f;
        c.edgeBend = scene.edgeBend;
        // D66: the glass's diffusion, as a mip level of the target (the picture's own scale is in lodBias).
        c.glassOn = scene.glass ? 1.0f : 0.0f;
        c.glassLod = std::log2(std::max(1.0f, scene.glassBlurDip * scene.pxPerDip));
        c.glassSaturation = scene.glassTokens.saturation;
        c.glassScale = scene.glassTokens.scale;
        c.glassOffset = scene.glassTokens.offset;
        c.glassOpacity = std::clamp(scene.glassTokens.blurOpacity, 0.0f, 1.0f);   // D67: the blur layer's cover
        c.glassTint[0] = scene.glassTokens.tintR; c.glassTint[1] = scene.glassTokens.tintG; c.glassTint[2] = scene.glassTokens.tintB;
        c.glassTint[3] = std::clamp(scene.glassTokens.tintStrength, 0.0f, 1.0f);

        winrt::com_ptr<ID3DDeviceContextState> previous;
        context->SwapDeviceContextState(state.get(), previous.put());
        context->UpdateSubresource(constants.get(), 0, nullptr, &c, 0, 0);
        ID3D11RenderTargetView* views[] = {view};
        context->OMSetRenderTargets(1, views, nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->RSSetViewports(1, &viewport);
        context->RSSetState(nullptr);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader.get(), nullptr, 0);
        context->PSSetShader(pixelShader.get(), nullptr, 0);
        ID3D11Buffer* buffers[] = {constants.get()};
        context->PSSetConstantBuffers(0, 1, buffers);
        ID3D11ShaderResourceView* resources[] = {pictureView.get(), markView.get()};
        context->PSSetShaderResources(0, 2, resources);
        ID3D11SamplerState* samplers[] = {sampler.get()};
        context->PSSetSamplers(0, 1, samplers);
        context->Draw(3, 0);
        // Leave nothing bound that Direct2D may read or write next.
        ID3D11ShaderResourceView* none[] = {nullptr, nullptr};
        context->PSSetShaderResources(0, 2, none);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->SwapDeviceContextState(previous.get(), nullptr);
        const HRESULT removed = device->GetDeviceRemovedReason();
        return FAILED(removed) ? Result<void>(Failed("the graphics device was lost", removed)) : Ok();
    }
};

LiquidRenderer::LiquidRenderer() : impl_(std::make_unique<Impl>()) {}
LiquidRenderer::~LiquidRenderer() = default;

Result<void> LiquidRenderer::Initialize(ID3D11Device* device) try {
    if (Ready()) return Ok();
    if (!device) return MakeError(ErrorCode::InvalidArgument, "no Direct3D device");
    auto& m = *impl_;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(m.device.put())))) return MakeError(ErrorCode::Unsupported, "Direct3D 11.1 is required");
    winrt::com_ptr<ID3D11DeviceContext> immediate;
    m.device->GetImmediateContext(immediate.put());
    if (!immediate || FAILED(immediate->QueryInterface(IID_PPV_ARGS(m.context.put())))) return MakeError(ErrorCode::Unsupported, "Direct3D 11.1 is required");
    const D3D_FEATURE_LEVEL level = m.device->GetFeatureLevel();
    HRESULT hr = m.device->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, m.state.put());
    if (FAILED(hr)) return Failed("material state could not be created", hr);
    const auto compile = [&](const char* entry, const char* profile, winrt::com_ptr<ID3DBlob>& code) -> Result<void> {
        winrt::com_ptr<ID3DBlob> errors;
        const HRESULT compiled = ::D3DCompile(kShader.data(), kShader.size(), "LiquidShader", nullptr, nullptr, entry, profile,
                                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.put(), errors.put());
        if (FAILED(compiled)) return MakeError(ErrorCode::Internal, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "material shader did not compile", compiled);
        return Ok();
    };
    winrt::com_ptr<ID3DBlob> vertex, pixel;
    if (auto done = compile("VS", "vs_4_0", vertex); !done) return done;
    if (auto done = compile("PS", "ps_4_0", pixel); !done) return done;
    hr = m.device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, m.vertexShader.put());
    if (SUCCEEDED(hr)) hr = m.device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, m.pixelShader.put());
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Constants);
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (SUCCEEDED(hr)) hr = m.device->CreateBuffer(&buffer, nullptr, m.constants.put());
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(hr)) hr = m.device->CreateSamplerState(&sampler, m.sampler.put());
    if (FAILED(hr)) { Reset(); return Failed("material pipeline could not be created", hr); }
    return Ok();
} catch (...) {
    Reset();
    return MakeError(ErrorCode::Internal, "material renderer failed");
}

void LiquidRenderer::Reset() noexcept {
    auto& m = *impl_;
    m.targets.clear();
    m.directView = nullptr;
    m.directTexture = nullptr;
    m.pictureView = nullptr;
    m.markView = nullptr;
    // A picture that was only on the lost device is gone: the caller reads it again (HasPicture is false).
    if (m.pictureUploaded && !m.picture) { m.pictureWidth = m.pictureHeight = 0; }
    m.pictureUploaded = false;
    m.sampler = nullptr;
    m.constants = nullptr;
    m.pixelShader = nullptr;
    m.vertexShader = nullptr;
    m.state = nullptr;
    m.context = nullptr;
    m.device = nullptr;
}

bool LiquidRenderer::Ready() const noexcept { return impl_->device && impl_->pixelShader; }

void LiquidRenderer::SetPicture(std::shared_ptr<const std::vector<std::uint8_t>> bgra, UINT width, UINT height, D2D1_RECT_F placementPx) {
    auto& m = *impl_;
    if (!bgra || bgra->size() < static_cast<std::size_t>(width) * height * 4u) { bgra = nullptr; width = height = 0; }
    m.picture = std::move(bgra);
    m.pictureWidth = width;
    m.pictureHeight = height;
    m.placement = placementPx;
    ++m.generation;
    m.pictureUploaded = false;   // uploaded on the next frame
    m.pictureView = nullptr;
}

bool LiquidRenderer::HasPicture() const noexcept { return impl_->picture != nullptr || (impl_->pictureUploaded && impl_->pictureView); }

void LiquidRenderer::ReleaseTransient(bool keepPicture) noexcept {
    auto& m = *impl_;
    m.targets.clear();
    m.directView = nullptr;
    m.directTexture = nullptr;
    if (keepPicture) return;
    m.picture.reset();
    m.pictureView = nullptr;
    m.pictureUploaded = false;
    m.pictureWidth = m.pictureHeight = 0;
}
SIZE LiquidRenderer::PictureSize() const noexcept { return {static_cast<LONG>(impl_->pictureWidth), static_cast<LONG>(impl_->pictureHeight)}; }
D2D1_RECT_F LiquidRenderer::PicturePlacement() const noexcept { return impl_->placement; }
std::uint64_t LiquidRenderer::PictureGeneration() const noexcept { return impl_->generation; }

Result<winrt::com_ptr<ID2D1Bitmap1>> LiquidRenderer::Render(ID2D1DeviceContext* context, const MaterialScene& scene, D2D1_SIZE_U sizePx) try {
    if (!Ready()) return MakeError(ErrorCode::Unavailable, "material renderer is not ready");
    if (!context || sizePx.width == 0 || sizePx.height == 0) return MakeError(ErrorCode::InvalidArgument, "material target is empty");
    auto target = impl_->TargetFor(sizePx);
    if (!target) return target.error();
    if (auto drawn = impl_->Draw(scene, **target, sizePx); !drawn) return drawn.error();
    winrt::com_ptr<IDXGISurface> surface;
    if (FAILED((*target)->texture->QueryInterface(IID_PPV_ARGS(surface.put())))) return MakeError(ErrorCode::Platform, "material surface unavailable");
    float dpiX = 96.0f, dpiY = 96.0f;
    context->GetDpi(&dpiX, &dpiY);
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX, dpiY);
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    const HRESULT hr = context->CreateBitmapFromDxgiSurface(surface.get(), &properties, bitmap.put());
    if (FAILED(hr)) return Failed("material bitmap could not be created", hr);
    return bitmap;
} catch (...) {
    return MakeError(ErrorCode::Internal, "material frame failed");
}

Result<bool> LiquidRenderer::RenderInto(ID2D1DeviceContext* context, const MaterialScene& scene, D2D1_SIZE_U sizePx, POINT originPx) try {
    if (!Ready()) return MakeError(ErrorCode::Unavailable, "material renderer is not ready");
    if (!context || sizePx.width == 0 || sizePx.height == 0) return MakeError(ErrorCode::InvalidArgument, "material target is empty");
    auto& m = *impl_;
    winrt::com_ptr<ID2D1Image> image;
    context->GetTarget(image.put());
    const auto bitmap = image.try_as<ID2D1Bitmap1>();
    if (!bitmap) return false;
    winrt::com_ptr<IDXGISurface> surface;
    if (FAILED(bitmap->GetSurface(surface.put())) || !surface) return false;
    const auto texture = surface.try_as<ID3D11Texture2D>();
    if (!texture) return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    winrt::com_ptr<ID3D11Device> owner;
    texture->GetDevice(owner.put());
    // Only a plain render target of this very device, holding the whole frame where it is asked to go.
    if (owner.get() != static_cast<ID3D11Device*>(m.device.get()) || !(desc.BindFlags & D3D11_BIND_RENDER_TARGET) ||
        desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || originPx.x < 0 || originPx.y < 0 ||
        static_cast<UINT>(originPx.x) + sizePx.width > desc.Width || static_cast<UINT>(originPx.y) + sizePx.height > desc.Height)
        return false;
    if (m.directTexture.get() != texture.get() || !m.directView) {
        m.directView = nullptr;
        m.directTexture = texture;
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = desc.Format;
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (const HRESULT hr = m.device->CreateRenderTargetView(texture.get(), &view, m.directView.put()); FAILED(hr)) {
            m.directTexture = nullptr;
            return false;   // the caller draws through a texture of its own instead
        }
    }
    // The scene in the target texture's pixels: the frame's top-left lies at originPx there.
    auto shifted = scene;
    const float dx = static_cast<float>(originPx.x), dy = static_cast<float>(originPx.y);
    for (auto& body : shifted.bodies) { body.cx += dx; body.cy += dy; }
    shifted.logoCx += dx; shifted.logoCy += dy;
    shifted.picture.left += dx; shifted.picture.right += dx;
    shifted.picture.top += dy; shifted.picture.bottom += dy;
    context->Flush();   // Direct2D's work so far reaches the texture first
    if (auto drawn = m.Draw(shifted, m.directView.get(),
                            D3D11_VIEWPORT{dx, dy, static_cast<float>(sizePx.width), static_cast<float>(sizePx.height), 0.0f, 1.0f}); !drawn)
        return drawn.error();
    return true;
} catch (...) {
    return MakeError(ErrorCode::Internal, "material frame failed");
}

Result<void> LiquidRenderer::RenderToMemory(const MaterialScene& scene, D2D1_SIZE_U sizePx, std::vector<std::uint8_t>& bgra) try {
    if (!Ready()) return MakeError(ErrorCode::Unavailable, "material renderer is not ready");
    if (sizePx.width == 0 || sizePx.height == 0) return MakeError(ErrorCode::InvalidArgument, "material target is empty");
    auto& m = *impl_;
    auto target = m.TargetFor(sizePx);
    if (!target) return target.error();
    auto& t = **target;
    if (!t.staging) {
        D3D11_TEXTURE2D_DESC desc{};
        t.texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (const HRESULT hr = m.device->CreateTexture2D(&desc, nullptr, t.staging.put()); FAILED(hr)) return Failed("material read-back unavailable", hr);
    }
    if (auto drawn = m.Draw(scene, t, sizePx); !drawn) return drawn;
    m.context->CopyResource(t.staging.get(), t.texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (const HRESULT hr = m.context->Map(t.staging.get(), 0, D3D11_MAP_READ, 0, &mapped); FAILED(hr)) return Failed("material read-back failed", hr);
    bgra.resize(static_cast<std::size_t>(sizePx.width) * sizePx.height * 4u);
    for (UINT row = 0; row < sizePx.height; ++row)
        std::memcpy(bgra.data() + static_cast<std::size_t>(row) * sizePx.width * 4u, static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, sizePx.width * 4u);
    m.context->Unmap(t.staging.get(), 0);
    return Ok();
} catch (...) {
    return MakeError(ErrorCode::Internal, "material frame failed");
}

}  // namespace aether::graphics
