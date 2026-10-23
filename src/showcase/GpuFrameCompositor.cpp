// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/GpuFrameCompositor.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace aether::showcase {
namespace {

using Microsoft::WRL::ComPtr;
constexpr std::size_t kShaderSamples = TemporalSampleSet::MaximumSamples;

struct alignas(16) ShaderConstants final {
    float camera[kShaderSamples][4]{};
    float sourceSize[2]{};
    float outputSize[2]{};
    std::uint32_t sampleCount = 0;
    std::uint32_t padding[3]{};
};
static_assert(sizeof(ShaderConstants) == 160);

constexpr char kShader[] = R"(
cbuffer FrameConstants : register(b0) {
    float4 camera[8];
    float2 sourceSize;
    float2 outputSize;
    uint sampleCount;
    uint3 padding;
};
Texture2D<float4> sourceFrames[8] : register(t0);
SamplerState linearClamp : register(s0);
struct VertexOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VertexOutput VSMain(uint vertexId : SV_VertexID) {
    float2 positions[3] = { float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0) };
    float2 position = positions[vertexId];
    VertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.uv = float2((position.x + 1.0) * 0.5, (1.0 - position.y) * 0.5);
    return output;
}
float4 PSMain(VertexOutput input) : SV_TARGET {
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    [unroll]
    for (uint index = 0; index < 8; ++index) {
        if (index >= sampleCount) break;
        float2 outputPixel = input.uv * outputSize;
        float2 sourcePixel = camera[index].xy + (outputPixel - outputSize * 0.5) / camera[index].z;
        float2 sourceUv = sourcePixel / sourceSize;
        sum += sourceFrames[index].Sample(linearClamp, sourceUv);
    }
    return sum / max((float)sampleCount, 1.0);
}
)";

Error HResultError(ErrorCode code, const char* message, HRESULT status) noexcept {
    return MakeError(code, message, static_cast<std::int64_t>(status));
}

bool Positive(double value) noexcept { return std::isfinite(value) && value > 0.0; }

bool ValidFrame(const RgbaFrameView& frame, std::uint32_t width, std::uint32_t height) noexcept {
    if (frame.width != width || frame.height != height || width == 0 || height == 0 ||
        static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / 4U) return false;
    const auto rowBytes = static_cast<std::size_t>(width) * 4U;
    return frame.rowStrideBytes >= rowBytes &&
        frame.pixels.size() >= frame.rowStrideBytes * static_cast<std::size_t>(height - 1) + rowBytes;
}

}  // namespace

struct GpuFrameCompositor::Impl final {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
    std::array<ComPtr<ID3D11Texture2D>, kShaderSamples> sourceTextures;
    std::array<ComPtr<ID3D11ShaderResourceView>, kShaderSamples> sourceViews;
    std::array<std::uint64_t, kShaderSamples> sourceContentIds{};
    const ITemporalFrameSource* sourceProvider = nullptr;
    ComPtr<ID3D11Texture2D> renderTexture;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    ComPtr<ID3D11Texture2D> stagingTexture;
    std::uint32_t sourceWidth = 0;
    std::uint32_t sourceHeight = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    bool hardware = false;
};

GpuFrameCompositor::GpuFrameCompositor() noexcept = default;
GpuFrameCompositor::GpuFrameCompositor(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
GpuFrameCompositor::~GpuFrameCompositor() = default;
GpuFrameCompositor::GpuFrameCompositor(GpuFrameCompositor&& other) noexcept = default;
GpuFrameCompositor& GpuFrameCompositor::operator=(GpuFrameCompositor&& other) noexcept = default;

Result<GpuFrameCompositor> GpuFrameCompositor::Create() noexcept {
    try {
        auto impl = std::make_unique<Impl>();
        constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL selected{};
        HRESULT status = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                             static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                             &impl->device, &selected, &impl->context);
        if (SUCCEEDED(status)) {
            impl->hardware = true;
        } else {
            status = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                         D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                         static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                         &impl->device, &selected, &impl->context);
        }
        if (FAILED(status)) return HResultError(ErrorCode::Unavailable, "showcase compositor could not create a Direct3D device", status);

        ComPtr<ID3DBlob> vertexCode;
        ComPtr<ID3DBlob> pixelCode;
        ComPtr<ID3DBlob> compilerErrors;
        constexpr UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
                                      D3DCOMPILE_OPTIMIZATION_LEVEL3;
        status = ::D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, "VSMain", "vs_5_0",
                              compileFlags, 0, &vertexCode, &compilerErrors);
        if (FAILED(status)) return HResultError(ErrorCode::Platform, "showcase vertex shader compilation failed", status);
        compilerErrors.Reset();
        status = ::D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, "PSMain", "ps_5_0",
                              compileFlags, 0, &pixelCode, &compilerErrors);
        if (FAILED(status)) return HResultError(ErrorCode::Platform, "showcase pixel shader compilation failed", status);
        status = impl->device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr,
                                                   &impl->vertexShader);
        if (SUCCEEDED(status)) status = impl->device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(),
                                                                         nullptr, &impl->pixelShader);
        if (FAILED(status)) return HResultError(ErrorCode::Platform, "showcase shaders could not be created", status);

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(ShaderConstants);
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        status = impl->device->CreateBuffer(&buffer, nullptr, &impl->constants);
        if (FAILED(status)) return HResultError(ErrorCode::Platform, "showcase compositor constants could not be allocated", status);

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        status = impl->device->CreateSamplerState(&sampler, &impl->sampler);
        if (FAILED(status)) return HResultError(ErrorCode::Platform, "showcase sampling state could not be created", status);

        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        status = impl->device->CreateRasterizerState(&rasterizer, &impl->rasterizer);
        if (FAILED(status)) return HResultError(ErrorCode::Platform, "showcase rasterizer state could not be created", status);
        return GpuFrameCompositor(std::move(impl));
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase compositor allocation failed");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase compositor initialization failed");
    }
}

Result<GpuCompositeMetrics> GpuFrameCompositor::Render(
    const CinematicSequence& sequence, ITemporalFrameSource& source, double frameCenterSeconds,
    const SequenceEvents& events, std::span<std::uint8_t> destination, std::uint32_t destinationWidth,
    std::uint32_t destinationHeight, std::size_t destinationRowStrideBytes,
    const CompositeSettings& settings) noexcept {
    if (!impl_ || !std::isfinite(frameCenterSeconds) || frameCenterSeconds < 0.0 ||
        !Positive(settings.framesPerSecond) || !std::isfinite(settings.shutterAngleDegrees) ||
        settings.shutterAngleDegrees < 0.0 || settings.shutterAngleDegrees > 360.0 || settings.sampleCount == 0 ||
        settings.sampleCount > TemporalSampleSet::MaximumSamples ||
        destinationWidth != static_cast<std::uint32_t>(sequence.Config().outputPixels.width) ||
        destinationHeight != static_cast<std::uint32_t>(sequence.Config().outputPixels.height) ||
        destinationRowStrideBytes < static_cast<std::size_t>(destinationWidth) * 4U ||
        destination.size() < destinationRowStrideBytes * static_cast<std::size_t>(destinationHeight - 1) +
                                static_cast<std::size_t>(destinationWidth) * 4U) {
        return MakeError(ErrorCode::InvalidArgument, "showcase GPU compositor geometry or sampling settings are invalid");
    }

    const auto start = std::chrono::steady_clock::now();
    const std::size_t sampleCount = settings.motionBlurEnabled && settings.shutterAngleDegrees > 0.0
                                        ? settings.sampleCount : 1;
    auto planned = CinematicSequence::BuildMotionBlurSamples(frameCenterSeconds, settings.framesPerSecond,
                                                              settings.motionBlurEnabled ? settings.shutterAngleDegrees : 0.0,
                                                              sampleCount);
    if (!planned) return planned.error();
    const auto& samples = *planned;
    const auto sourceWidth = static_cast<std::uint32_t>(sequence.Config().sourcePixels.width);
    const auto sourceHeight = static_cast<std::uint32_t>(sequence.Config().sourcePixels.height);
    if (sourceWidth == 0 || sourceHeight == 0 || destinationWidth == 0 || destinationHeight == 0 ||
        sourceWidth > 16'384 || sourceHeight > 16'384 || destinationWidth > 16'384 || destinationHeight > 16'384) {
        return MakeError(ErrorCode::InvalidArgument, "showcase GPU compositor dimensions are outside supported limits");
    }

    try {
        if (impl_->sourceWidth != sourceWidth || impl_->sourceHeight != sourceHeight ||
            impl_->outputWidth != destinationWidth || impl_->outputHeight != destinationHeight) {
            for (auto& texture : impl_->sourceTextures) texture.Reset();
            for (auto& view : impl_->sourceViews) view.Reset();
            impl_->sourceContentIds.fill(0);
            impl_->sourceProvider = nullptr;
            impl_->renderTarget.Reset();
            impl_->renderTexture.Reset();
            impl_->stagingTexture.Reset();

            D3D11_TEXTURE2D_DESC sourceDesc{};
            sourceDesc.Width = sourceWidth;
            sourceDesc.Height = sourceHeight;
            sourceDesc.MipLevels = 1;
            sourceDesc.ArraySize = 1;
            sourceDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            sourceDesc.SampleDesc.Count = 1;
            sourceDesc.Usage = D3D11_USAGE_DEFAULT;
            sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
            viewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipLevels = 1;
            for (std::size_t index = 0; index < kShaderSamples; ++index) {
                HRESULT status = impl_->device->CreateTexture2D(&sourceDesc, nullptr, &impl_->sourceTextures[index]);
                if (SUCCEEDED(status)) status = impl_->device->CreateShaderResourceView(impl_->sourceTextures[index].Get(),
                                                                                          &viewDesc, &impl_->sourceViews[index]);
                if (FAILED(status)) return HResultError(ErrorCode::Unavailable, "showcase source texture could not be created", status);
            }

            D3D11_TEXTURE2D_DESC outputDesc{};
            outputDesc.Width = destinationWidth;
            outputDesc.Height = destinationHeight;
            outputDesc.MipLevels = 1;
            outputDesc.ArraySize = 1;
            outputDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            outputDesc.SampleDesc.Count = 1;
            outputDesc.Usage = D3D11_USAGE_DEFAULT;
            outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
            HRESULT status = impl_->device->CreateTexture2D(&outputDesc, nullptr, &impl_->renderTexture);
            D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};
            targetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            targetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            if (SUCCEEDED(status)) status = impl_->device->CreateRenderTargetView(impl_->renderTexture.Get(),
                                                                                    &targetDesc, &impl_->renderTarget);
            if (FAILED(status)) return HResultError(ErrorCode::Unavailable, "showcase output surface could not be created", status);

            D3D11_TEXTURE2D_DESC stagingDesc = outputDesc;
            stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            status = impl_->device->CreateTexture2D(&stagingDesc, nullptr, &impl_->stagingTexture);
            if (FAILED(status)) return HResultError(ErrorCode::Unavailable, "showcase readback surface could not be created", status);
            impl_->sourceWidth = sourceWidth;
            impl_->sourceHeight = sourceHeight;
            impl_->outputWidth = destinationWidth;
            impl_->outputHeight = destinationHeight;
        }

        ShaderConstants values{};
        values.sourceSize[0] = static_cast<float>(sourceWidth);
        values.sourceSize[1] = static_cast<float>(sourceHeight);
        values.outputSize[0] = static_cast<float>(destinationWidth);
        values.outputSize[1] = static_cast<float>(destinationHeight);
        values.sampleCount = static_cast<std::uint32_t>(samples.count);
        std::array<ID3D11ShaderResourceView*, kShaderSamples> views{};
        std::array<bool, kShaderSamples> usedTextures{};
        std::size_t textureUploads = 0;
        if (impl_->sourceProvider != &source) {
            impl_->sourceContentIds.fill(0);
            impl_->sourceProvider = &source;
        }
        for (std::size_t index = 0; index < samples.count; ++index) {
            auto frameResult = source.ReadFrameAt(samples.timestamps[index]);
            if (!frameResult) return frameResult.error();
            const auto& frame = *frameResult;
            if (!ValidFrame(frame, sourceWidth, sourceHeight))
                return MakeError(ErrorCode::Validation, "showcase capture frame does not match the configured source geometry");
            std::size_t textureIndex = kShaderSamples;
            if (frame.contentId != 0) {
                const auto cached = std::find(impl_->sourceContentIds.begin(), impl_->sourceContentIds.end(), frame.contentId);
                if (cached != impl_->sourceContentIds.end())
                    textureIndex = static_cast<std::size_t>(cached - impl_->sourceContentIds.begin());
            }
            if (textureIndex == kShaderSamples) {
                // Never overwrite a texture already referenced by this frame's shutter samples.
                // At most eight samples can require eight distinct textures, so one is available.
                for (std::size_t candidate = 0; candidate < kShaderSamples; ++candidate) {
                    if (!usedTextures[candidate]) { textureIndex = candidate; break; }
                }
                impl_->context->UpdateSubresource(impl_->sourceTextures[textureIndex].Get(), 0, nullptr,
                                                  frame.pixels.data(), static_cast<UINT>(frame.rowStrideBytes), 0);
                impl_->sourceContentIds[textureIndex] = frame.contentId;
                ++textureUploads;
            }
            usedTextures[textureIndex] = true;
            const auto pose = sequence.Evaluate(samples.timestamps[index], events).camera;
            values.camera[index][0] = static_cast<float>(pose.centerPixels.x);
            values.camera[index][1] = static_cast<float>(pose.centerPixels.y);
            values.camera[index][2] = static_cast<float>(pose.zoom);
            views[index] = impl_->sourceViews[textureIndex].Get();
        }
        for (std::size_t index = samples.count; index < kShaderSamples; ++index) {
            const auto duplicate = samples.count - 1;
            values.camera[index][0] = values.camera[duplicate][0];
            values.camera[index][1] = values.camera[duplicate][1];
            values.camera[index][2] = values.camera[duplicate][2];
            views[index] = views[duplicate];
        }

        D3D11_MAPPED_SUBRESOURCE mappedConstants{};
        HRESULT status = impl_->context->Map(impl_->constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedConstants);
        if (FAILED(status)) return HResultError(ErrorCode::Platform, "showcase compositor constants could not be updated", status);
        std::memcpy(mappedConstants.pData, &values, sizeof(values));
        impl_->context->Unmap(impl_->constants.Get(), 0);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(destinationWidth);
        viewport.Height = static_cast<float>(destinationHeight);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        impl_->context->RSSetViewports(1, &viewport);
        impl_->context->RSSetState(impl_->rasterizer.Get());
        impl_->context->IASetInputLayout(nullptr);
        impl_->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        impl_->context->VSSetShader(impl_->vertexShader.Get(), nullptr, 0);
        impl_->context->PSSetShader(impl_->pixelShader.Get(), nullptr, 0);
        ID3D11Buffer* constantBuffer = impl_->constants.Get();
        impl_->context->VSSetConstantBuffers(0, 1, &constantBuffer);
        impl_->context->PSSetConstantBuffers(0, 1, &constantBuffer);
        ID3D11SamplerState* sampler = impl_->sampler.Get();
        impl_->context->PSSetSamplers(0, 1, &sampler);
        impl_->context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
        ID3D11RenderTargetView* target = impl_->renderTarget.Get();
        impl_->context->OMSetRenderTargets(1, &target, nullptr);
        impl_->context->Draw(3, 0);
        impl_->context->OMSetRenderTargets(0, nullptr, nullptr);
        std::array<ID3D11ShaderResourceView*, kShaderSamples> emptyViews{};
        impl_->context->PSSetShaderResources(0, static_cast<UINT>(emptyViews.size()), emptyViews.data());
        impl_->context->CopyResource(impl_->stagingTexture.Get(), impl_->renderTexture.Get());

        D3D11_MAPPED_SUBRESOURCE pixels{};
        status = impl_->context->Map(impl_->stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &pixels);
        if (FAILED(status)) return HResultError(ErrorCode::Platform, "showcase output frame could not be read back", status);
        const auto rowBytes = static_cast<std::size_t>(destinationWidth) * 4U;
        auto* destinationPixels = destination.data();
        const auto* sourcePixels = static_cast<const std::uint8_t*>(pixels.pData);
        for (std::uint32_t row = 0; row < destinationHeight; ++row) {
            std::memcpy(destinationPixels + static_cast<std::size_t>(row) * destinationRowStrideBytes,
                        sourcePixels + static_cast<std::size_t>(row) * pixels.RowPitch, rowBytes);
        }
        impl_->context->Unmap(impl_->stagingTexture.Get(), 0);

        GpuCompositeMetrics metrics;
        metrics.sourceFrameReads = samples.count;
        metrics.sourceTextureUploads = textureUploads;
        metrics.hardwareAccelerated = impl_->hardware;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        metrics.elapsedMicroseconds = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0;
        return metrics;
    } catch (const std::bad_alloc&) {
        return MakeError(ErrorCode::Unavailable, "showcase GPU compositor allocation failed");
    } catch (...) {
        return MakeError(ErrorCode::Internal, "showcase GPU compositor failed");
    }
}

bool GpuFrameCompositor::HardwareAccelerated() const noexcept { return impl_ && impl_->hardware; }

}  // namespace aether::showcase
