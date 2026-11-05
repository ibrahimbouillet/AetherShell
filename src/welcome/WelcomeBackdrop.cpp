// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "welcome/WelcomeBackdrop.h"

#include "diagnostics/Logger.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace aether::welcome {
namespace {
constexpr float kBlurShare = 0.012f;      // of the picture's width: soft, while the picture stays recognizable
constexpr float kMinimumVeil = 0.10f;     // a little depth, even over a dark picture
constexpr float kMaximumVeil = 0.55f;
constexpr float kLegibleBrightness = 0.46f;  // pale ink keeps 4.5:1 on an average this bright (as displayed)

// A file only a cloud provider holds would be downloaded just to be blurred.
bool ReadableWithoutDownload(const std::wstring& file) {
    const DWORD attributes = ::GetFileAttributesW(file.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS));
}

std::wstring TakeString(LPWSTR value) {
    std::wstring result = value ? value : L"";
    if (value) ::CoTaskMemFree(value);
    return result;
}

// Windows' answer for this monitor. Empty for a plain colour, but also for a picture whose file is gone.
std::wstring MonitorPicture(const RECT& monitor) {
    winrt::com_ptr<IDesktopWallpaper> desktop;
    if (FAILED(::CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(desktop.put())))) return {};
    UINT count = 0;
    if (FAILED(desktop->GetMonitorDevicePathCount(&count))) return {};
    for (UINT i = 0; i < count; ++i) {
        LPWSTR id = nullptr;
        if (FAILED(desktop->GetMonitorDevicePathAt(i, &id))) continue;
        const auto device = TakeString(id);
        RECT bounds{};
        if (FAILED(desktop->GetMonitorRECT(device.c_str(), &bounds)) || !::EqualRect(&bounds, &monitor)) continue;
        LPWSTR path = nullptr;
        if (SUCCEEDED(desktop->GetWallpaper(device.c_str(), &path))) return TakeString(path);
    }
    return {};
}

// The desktop-wide setting: empty only for a plain colour; it keeps a picture's name after the file is gone.
std::wstring DesktopPicture() {
    wchar_t path[MAX_PATH]{};
    return ::SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) ? std::wstring(path) : std::wstring();
}

// The copy of the picture Windows shows, which it keeps itself. Empty when the folder is unknown.
std::filesystem::path WindowsCopy() {
    PWSTR roaming = nullptr;
    const HRESULT found = ::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming);
    std::wstring folder = TakeString(roaming);
    if (FAILED(found) || folder.empty()) return {};
    return std::filesystem::path(folder) / L"Microsoft" / L"Windows" / L"Themes" / L"TranscodedWallpaper";
}
}  // namespace

std::vector<std::filesystem::path> BackdropCandidates(std::wstring_view monitorPicture, std::wstring_view desktopPicture,
                                                      const std::filesystem::path& windowsCopy) {
    std::vector<std::filesystem::path> files;
    const std::wstring_view named = monitorPicture.empty() ? desktopPicture : monitorPicture;
    if (named.empty()) return files;  // a plain colour: any copy Windows keeps would be an old picture
    files.emplace_back(named);
    if (!windowsCopy.empty() && windowsCopy != files.front()) files.push_back(windowsCopy);
    return files;
}

float MeanBrightness(std::span<const std::uint8_t> pixels) noexcept {
    const std::size_t count = pixels.size() / 4;
    if (count == 0) return 0.0f;
    const std::size_t stride = std::max<std::size_t>(1, count / 4096);  // a sample is plenty for an average
    double total = 0.0;
    std::size_t used = 0;
    for (std::size_t i = 0; i < count; i += stride, ++used)
        total += 0.0722 * pixels[i * 4] + 0.7152 * pixels[i * 4 + 1] + 0.2126 * pixels[i * 4 + 2];
    return static_cast<float>(total / (255.0 * static_cast<double>(used)));
}

float VeilOpacityFor(float brightness) noexcept {
    if (!std::isfinite(brightness) || brightness <= 0.0f) return kMinimumVeil;
    return std::clamp(1.0f - kLegibleBrightness / brightness, kMinimumVeil, kMaximumVeil);
}

D2D1_RECT_F CoverRect(float imageWidth, float imageHeight, float areaWidth, float areaHeight) noexcept {
    if (imageWidth <= 0.0f || imageHeight <= 0.0f) return D2D1::RectF(0.0f, 0.0f, areaWidth, areaHeight);
    const float scale = std::max(areaWidth / imageWidth, areaHeight / imageHeight);
    const float width = imageWidth * scale, height = imageHeight * scale;
    const float left = (areaWidth - width) * 0.5f, top = (areaHeight - height) * 0.5f;
    return D2D1::RectF(left, top, left + width, top + height);
}

Result<BackdropImage> DecodeBackdrop(const std::filesystem::path& file, UINT coverWidth, UINT coverHeight) try {
    const auto failed = [] { return MakeError(ErrorCode::Unavailable, "The desktop picture could not be read"); };
    if (coverWidth == 0 || coverHeight == 0) return MakeError(ErrorCode::InvalidArgument, "Welcome backdrop size is invalid");
    if (!ReadableWithoutDownload(file.wstring())) return failed();
    winrt::com_ptr<IWICImagingFactory> factory;
    winrt::com_ptr<IWICBitmapDecoder> decoder;
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))) ||
        FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())) ||
        FAILED(decoder->GetFrame(0, frame.put()))) return failed();
    UINT sourceWidth = 0, sourceHeight = 0;
    if (FAILED(frame->GetSize(&sourceWidth, &sourceHeight)) || sourceWidth == 0 || sourceHeight == 0) return failed();
    const double reduce = std::min(1.0, std::max(static_cast<double>(coverWidth) / sourceWidth, static_cast<double>(coverHeight) / sourceHeight));
    const UINT width = std::max(1u, static_cast<UINT>(std::lround(sourceWidth * reduce)));
    const UINT height = std::max(1u, static_cast<UINT>(std::lround(sourceHeight * reduce)));
    winrt::com_ptr<IWICBitmapScaler> scaler;
    winrt::com_ptr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(frame.get(), width, height, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(scaler.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return failed();
    BackdropImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4u);
    if (FAILED(converter->CopyPixels(nullptr, width * 4u, static_cast<UINT>(image.pixels.size()), image.pixels.data()))) return failed();
    image.brightness = MeanBrightness(image.pixels);
    return image;
} catch (...) {
    return MakeError(ErrorCode::Internal, "The desktop picture could not be read");
}

std::optional<BackdropImage> LoadDesktopBackdrop(const RECT& monitorPx) {
    // The log says which way it went, never which file: a black welcome can then be told apart from a plain colour.
    try {
        const auto files = BackdropCandidates(MonitorPicture(monitorPx), DesktopPicture(), WindowsCopy());
        if (files.empty()) {
            AETHER_LOG_INFO("welcome", "background: the desktop is a plain colour");
            return std::nullopt;
        }
        const UINT width = static_cast<UINT>(std::max(1L, (monitorPx.right - monitorPx.left) / 2));
        const UINT height = static_cast<UINT>(std::max(1L, (monitorPx.bottom - monitorPx.top) / 2));
        // Moved, deleted or only in the cloud: Windows keeps its own local copy of the picture it shows.
        for (std::size_t i = 0; i < files.size(); ++i) {
            if (auto image = DecodeBackdrop(files[i], width, height)) {
                AETHER_LOG_INFO("welcome", "background: {} ({}x{})", i == 0 ? "the desktop picture" : "Windows' copy of the desktop picture", image->width, image->height);
                return std::move(*image);
            }
        }
        AETHER_LOG_WARN("welcome", "background: the desktop picture could not be read ({} tried)", files.size());
    } catch (...) {
        AETHER_LOG_WARN("welcome", "background: the desktop picture could not be read");
    }
    return std::nullopt;
}

winrt::com_ptr<ID2D1Bitmap1> BlurBackdrop(ID2D1Device* device, const BackdropImage& image) {
    if (!device || image.width == 0 || image.height == 0 || image.pixels.size() < static_cast<std::size_t>(image.width) * image.height * 4u) return nullptr;
    winrt::com_ptr<ID2D1DeviceContext> context;
    if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.put()))) return nullptr;
    context->SetDpi(96.0f, 96.0f);
    const auto size = D2D1::SizeU(image.width, image.height);
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    winrt::com_ptr<ID2D1Bitmap1> source, blurred;
    winrt::com_ptr<ID2D1Effect> edge, blur;
    if (FAILED(context->CreateBitmap(size, image.pixels.data(), image.width * 4u, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, format), source.put())) ||
        FAILED(context->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), blurred.put())) ||
        FAILED(context->CreateEffect(CLSID_D2D1Border, edge.put())) ||
        FAILED(context->CreateEffect(CLSID_D2D1GaussianBlur, blur.put()))) return nullptr;
    // Mirrored edges: otherwise the blur fades towards nothing, and the screen gets a dark rim.
    edge->SetInput(0, source.get());
    (void)edge->SetValue(D2D1_BORDER_PROP_EDGE_MODE_X, D2D1_BORDER_EDGE_MODE_MIRROR);
    (void)edge->SetValue(D2D1_BORDER_PROP_EDGE_MODE_Y, D2D1_BORDER_EDGE_MODE_MIRROR);
    blur->SetInputEffect(0, edge.get());
    (void)blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, std::max(1.0f, static_cast<float>(image.width) * kBlurShare));
    (void)blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    context->SetTarget(blurred.get());
    context->BeginDraw();
    const auto all = D2D1::RectF(0.0f, 0.0f, static_cast<float>(image.width), static_cast<float>(image.height));
    context->DrawImage(blur.get(), D2D1::Point2F(0.0f, 0.0f), all);
    const HRESULT drawn = context->EndDraw();
    context->SetTarget(nullptr);
    return SUCCEEDED(drawn) ? blurred : nullptr;
}

void PaintBackdrop(ID2D1DeviceContext* context, ID2D1Bitmap* blurred, const D2D1_COLOR_F& solid, float brightness, float width, float height) {
    if (!context) return;
    context->Clear(solid);
    if (blurred) {
        const auto size = blurred->GetPixelSize();
        const auto cover = CoverRect(static_cast<float>(size.width), static_cast<float>(size.height), width, height);
        context->DrawBitmap(blurred, cover, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
    }
    winrt::com_ptr<ID2D1SolidColorBrush> veil;
    if (SUCCEEDED(context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, VeilOpacityFor(brightness)), veil.put())))
        context->FillRectangle(D2D1::RectF(0.0f, 0.0f, width, height), veil.get());
}

}  // namespace aether::welcome
