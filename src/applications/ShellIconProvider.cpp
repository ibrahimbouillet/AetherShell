// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/ShellIconProvider.h"

#include "applications/ApplicationIdentity.h"
#include "applications/IconCache.h"
#include "applications/ShellItems.h"
#include "diagnostics/Logger.h"
#include "platform/windows/Handles.h"
#include "platform/windows/Win32Error.h"

#include <windows.h>
#include <shobjidl_core.h>

#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aether::apps {
namespace {

constexpr std::string_view kLog = "applications";
// Largest bitmap accepted back from the shell (BIGGERSIZEOK may exceed the request; jumbo icons are
// 256 px, thumbnails of executables never come back with SIIGBF_ICONONLY).
constexpr LONG kMaxBitmapSide = 2048;

// Screen DC for GetDIBits (the bitmap must not be selected into it, and it is not).
class ScreenDc {
public:
    ScreenDc() noexcept : dc_(::GetDC(nullptr)) {}
    ~ScreenDc() {
        if (dc_) ::ReleaseDC(nullptr, dc_);
    }
    ScreenDc(const ScreenDc&) = delete;
    ScreenDc& operator=(const ScreenDc&) = delete;
    [[nodiscard]] HDC Get() const noexcept { return dc_; }

private:
    HDC dc_;
};

Result<ExtractedIcon> ReadBitmap(HBITMAP bitmap) {
    BITMAP description{};
    if (::GetObjectW(bitmap, sizeof(description), &description) == 0) {
        return MakeError(ErrorCode::Platform, "the shell returned an unreadable bitmap");
    }
    const LONG width = description.bmWidth;
    const LONG height = description.bmHeight < 0 ? -description.bmHeight : description.bmHeight;
    if (width <= 0 || height <= 0 || width > kMaxBitmapSide || height > kMaxBitmapSide) {
        return MakeError(ErrorCode::Unsupported, "the shell returned a bitmap of unexpected size");
    }

    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth = width;
    format.bmiHeader.biHeight = -height;  // negative: top-down rows
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;

    ExtractedIcon result;
    result.image.width = static_cast<std::uint32_t>(width);
    result.image.height = static_cast<std::uint32_t>(height);
    result.image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    const ScreenDc dc;
    if (!dc.Get()) return platform::LastWin32Error("GetDC");
    // 32-bpp sources are copied byte for byte (alpha included); for lower depths GDI writes 0 into
    // the fourth byte, which NormalizeToPremultipliedBgra treats as "no alpha".
    const int lines = ::GetDIBits(dc.Get(), bitmap, 0, static_cast<UINT>(height), result.image.pixels.data(),
                                  &format, DIB_RGB_COLORS);
    if (lines != height) return MakeError(ErrorCode::Platform, "GetDIBits could not read the icon bitmap");

    // IShellItemImageFactory returns straight alpha (measured; see ShellIconProvider.h), so images
    // that cannot be classified from their pixels alone are premultiplied too.
    result.source = NormalizeToPremultipliedBgra(result.image.pixels, AmbiguousAlpha::TreatAsStraight);
    if (result.source.format == AlphaFormat::Transparent) {
        return MakeError(ErrorCode::Unsupported, "the shell returned an empty icon");
    }
    return result;
}

Result<ExtractedIcon> ExtractItem(IShellItem* item, std::uint32_t pixelSize) {
    winrt::com_ptr<IShellItemImageFactory> factory;
    HRESULT hr = item->QueryInterface(IID_PPV_ARGS(factory.put()));
    if (FAILED(hr)) return platform::ErrorFromHResult(hr, "the shell item has no image factory");

    const int side = static_cast<int>(pixelSize);
    HBITMAP raw = nullptr;
    hr = factory->GetImage(SIZE{side, side}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &raw);
    if (FAILED(hr) || !raw) return platform::ErrorFromHResult(FAILED(hr) ? hr : E_FAIL, "IShellItemImageFactory::GetImage");
    const platform::UniqueGdiObject owner(raw);
    return ReadBitmap(raw);
}

Result<ExtractedIcon> ExtractOnSta(const ApplicationInfo& app, std::uint32_t pixelSize) {
    if (app.kind == ApplicationKind::Desktop && !app.shortcutPath.empty()) {
        // Profile shortcuts may expose an avatar/icon override even when the generic Apps-folder
        // item shares the executable's logo. This changes presentation only, never launch routing.
        const auto path = NormalizeShortcutPath(app.shortcutPath);
        if (path && shell::ShouldPreferShortcutIcon(app)) {
            auto shortcut = shell::CreateFileItem(*path);
            if (shortcut) {
                auto image = ExtractItem(shortcut->get(), pixelSize);
                if (image) return image;
            }
        }
        // A missing/broken override must still have an honest generic fallback. Avoid asking the
        // same failed shortcut a second time through CreateApplicationItem's normal fallback chain.
        auto fallback = app;
        fallback.shortcutPath.clear();
        auto created = shell::CreateApplicationItem(fallback);
        return created ? ExtractItem(created->item.get(), pixelSize) : Result<ExtractedIcon>(created.error());
    }
    auto created = shell::CreateApplicationItem(app);
    return created ? ExtractItem(created->item.get(), pixelSize) : Result<ExtractedIcon>(created.error());
}

}  // namespace

struct ShellIconProvider::State {
    State(IUiDispatcher& uiDispatcher, const Options& options)
        : dispatcher(uiDispatcher), cache(options.cacheEntries, options.cacheBytes) {}

    IUiDispatcher& dispatcher;  // used only while the provider (and its pool) is alive
    std::mutex mutex;
    IconCache cache;
    std::unordered_map<std::wstring, std::vector<Callback>> inflight;  // composite key -> waiting callbacks
    std::uint64_t generation = 0;  // bumped by CancelAll and the destructor
    bool closed = false;

    // Posts `callbacks` to the UI thread. Each one runs only if neither CancelAll nor the destructor
    // happened since the request was accepted.
    static void Deliver(const std::shared_ptr<State>& self, std::uint64_t generation, const std::wstring& key,
                        std::shared_ptr<const IconImage> image, std::vector<Callback> callbacks) {
        if (callbacks.empty()) return;
        self->dispatcher.Post([self, generation, key, image = std::move(image), callbacks = std::move(callbacks)] {
            for (const auto& callback : callbacks) {
                {
                    std::lock_guard lock(self->mutex);
                    if (self->closed || self->generation != generation) return;
                }
                try {
                    callback(key, image);
                } catch (...) {
                    AETHER_LOG_ERROR(kLog, "icon callback threw: {}",
                                     shell::CurrentExceptionToError("icon callback").Describe());
                }
            }
        });
    }

    // Worker thread (STA). Whatever happens (including an exception such as std::bad_alloc), the
    // in-flight entry of this request is completed: otherwise every later request for the same key and
    // size would be coalesced into it and never answered.
    static void Extract(const std::shared_ptr<State>& self, std::uint64_t generation, const std::wstring& composite,
                        const IconRequest& request) {
        std::shared_ptr<const IconImage> image;
        try {
            {
                std::lock_guard lock(self->mutex);
                if (self->closed || self->generation != generation) return;  // cancelled before it started
                // A run cancelled by CancelAll may already have cached this icon.
                image = self->cache.Find(request.key, request.pixelSize);
            }
            if (!image) {
                auto extracted = ExtractIcon(request.app, request.pixelSize);
                if (extracted) {
                    image = std::make_shared<const IconImage>(std::move(extracted.value().image));
                } else {
                    AETHER_LOG_DEBUG(kLog, "no icon for {}: {}", shell::ForLog(request.app.displayName),
                                     extracted.error().Describe());
                }
                std::lock_guard lock(self->mutex);
                if (image) self->cache.Insert(request.key, request.pixelSize, image);
            }
        } catch (...) {
            AETHER_LOG_ERROR(kLog, "icon extraction failed: {}",
                             shell::CurrentExceptionToError("icon extraction").Describe());
            image.reset();
        }
        std::vector<Callback> callbacks;
        {
            std::lock_guard lock(self->mutex);
            if (self->closed || self->generation != generation) return;
            const auto waiting = self->inflight.find(composite);
            if (waiting == self->inflight.end()) return;
            callbacks = std::move(waiting->second);
            self->inflight.erase(waiting);
        }
        Deliver(self, generation, request.key, std::move(image), std::move(callbacks));
    }
};

ShellIconProvider::ShellIconProvider(IUiDispatcher& dispatcher) : ShellIconProvider(dispatcher, Options{}) {}

ShellIconProvider::ShellIconProvider(IUiDispatcher& dispatcher, Options options)
    : state_(std::make_shared<State>(dispatcher, options)),
      pool_(std::make_unique<WorkerPool>(WorkerPool::Options{1, L"Aether icons", ComApartment::SingleThreaded})) {}

ShellIconProvider::~ShellIconProvider() {
    decltype(State::inflight) dropped;
    try {
        std::lock_guard lock(state_->mutex);
        state_->closed = true;
        ++state_->generation;
        dropped.swap(state_->inflight);
    } catch (...) {
    }
    pool_->Shutdown();  // joins the worker; tasks that have not started are discarded
}

Result<ExtractedIcon> ShellIconProvider::ExtractIcon(const ApplicationInfo& app, std::uint32_t pixelSize) {
    if (pixelSize == 0 || pixelSize > kMaxPixelSize) {
        return MakeError(ErrorCode::InvalidArgument, "icon size out of range");
    }
    return shell::RunOnSta<ExtractedIcon>([&] { return ExtractOnSta(app, pixelSize); }, "icon extraction");
}

void ShellIconProvider::RequestIcon(IconRequest request, Callback callback) {
    if (!callback) return;
    try {
        const bool valid = !request.key.empty() && request.pixelSize >= 1 && request.pixelSize <= kMaxPixelSize;
        std::shared_ptr<const IconImage> cached;
        std::uint64_t generation = 0;
        std::wstring composite;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->closed) return;
            generation = state_->generation;
            if (valid) {
                cached = state_->cache.Find(request.key, request.pixelSize);
                if (!cached) {
                    composite = IconCache::CompositeKey(request.key, request.pixelSize);
                    auto [waiting, inserted] = state_->inflight.try_emplace(composite);
                    waiting->second.push_back(std::move(callback));
                    if (!inserted) return;  // coalesced with the extraction already queued
                }
            }
        }
        if (!valid || cached) {
            std::vector<Callback> single;
            single.push_back(std::move(callback));
            State::Deliver(state_, generation, request.key, std::move(cached), std::move(single));
            return;
        }
        auto state = state_;
        const bool posted = pool_->Post([state, generation, composite, request = std::move(request)] {
            State::Extract(state, generation, composite, request);
        });
        if (!posted) {
            // Only happens during destruction, which drops pending callbacks anyway.
            std::vector<Callback> dropped;
            std::lock_guard lock(state_->mutex);
            if (const auto waiting = state_->inflight.find(composite); waiting != state_->inflight.end()) {
                dropped = std::move(waiting->second);
                state_->inflight.erase(waiting);
            }
        }
    } catch (...) {
        AETHER_LOG_ERROR(kLog, "icon request failed: {}", shell::CurrentExceptionToError("icon request").Describe());
    }
}

void ShellIconProvider::CancelAll() noexcept {
    decltype(State::inflight) dropped;  // destroyed outside the lock
    try {
        std::lock_guard lock(state_->mutex);
        ++state_->generation;
        dropped.swap(state_->inflight);
    } catch (...) {
    }
}

}  // namespace aether::apps
