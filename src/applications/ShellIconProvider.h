// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// IIconProvider backed by IShellItemImageFactory.
//
//  * An ApplicationInfo::iconResource (the relaunch icon a running window declared for its group,
//    for example a browser profile's avatar) is tried first, only in the form ParseIconResource
//    accepts ("<local path>.ico,0"), only for an ordinary file on a non-network drive (no directory,
//    reparse point, device or cloud placeholder), and never logged. Any failure falls back to the
//    chain below.
//  * Desktop entries prefer an existing validated local .lnk, preserving shortcut/profile icon
//    overrides. Failed shortcut extraction falls back to the Apps-folder item (parsing name, else
//    packaged/explicit AUMID), then executable. Other entries retain CreateApplicationItem's normal
//    Apps-folder/.lnk/executable order, including packaged-app plates. Launch routing is unchanged.
//  * GetImage(size, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK). With BIGGERSIZEOK the shell may return
//    an image larger than requested (it then skips its own low-quality GDI stretch); IconImage
//    carries the real dimensions and the caller scales.
//  * The HBITMAP is read back with GetDIBits as 32-bpp top-down BGRA and normalized to premultiplied
//    alpha (IconPixels.h). The returned DIB is NOT premultiplied: on Windows 11 build 26300 the
//    integration survey found 45 of 48 application icons (packaged and desktop alike) provably
//    straight (a colour component above its alpha), 2 fully opaque and 1 ambiguous, and none that
//    could only be premultiplied. Every image with translucent pixels is therefore premultiplied here
//    (AmbiguousAlpha::TreatAsStraight); bitmaps without alpha (every alpha byte 0) are made opaque.
//    The survey test re-checks this on every run.
//
// Threading: extraction runs on one dedicated STA worker thread (shell extensions and icon handlers
// require an STA). RequestIcon and CancelAll may be called from any thread; callbacks always run on
// the UI thread through the IUiDispatcher. Identical in-flight requests (same key and size) are
// coalesced into one extraction. A memory LRU cache (about 256 entries, 32 MiB of pixels) serves
// repeated requests without touching the shell; failures are not cached.
// Cancellation: when CancelAll() is called on the UI thread, no callback of an earlier request runs
// after it returns. Called from another thread, it cannot stop a callback the UI thread is already
// running (or about to run in the same instant). After destruction no callback runs at all. The
// destructor discards queued extractions but waits for the one in progress (normally well under a
// second; a hung shell extension would delay it), so destroy the provider at shutdown, not per frame.
#pragma once

#include "applications/ApplicationTypes.h"
#include "applications/IconPixels.h"
#include "core/Dispatcher.h"
#include "core/Result.h"
#include "core/WorkerPool.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace aether::apps {

struct ExtractedIcon {
    IconImage image;       // premultiplied, top-down BGRA
    AlphaAnalysis source;  // what the shell returned, before normalization
};

class ShellIconProvider final : public IIconProvider {
public:
    static constexpr std::uint32_t kMaxPixelSize = 1024;

    struct Options {
        std::size_t cacheEntries = 256;
        std::size_t cacheBytes = 32u * 1024u * 1024u;
    };

    explicit ShellIconProvider(IUiDispatcher& dispatcher);
    ShellIconProvider(IUiDispatcher& dispatcher, Options options);
    ~ShellIconProvider() override;
    ShellIconProvider(const ShellIconProvider&) = delete;
    ShellIconProvider& operator=(const ShellIconProvider&) = delete;

    // Requests with an empty key or a pixel size outside 1..kMaxPixelSize complete with a null image.
    void RequestIcon(IconRequest request, Callback callback) override;
    // Drops every pending callback (see "Cancellation" above). Extractions already running finish
    // (their result still fills the cache) but report to nobody. The cache is kept.
    void CancelAll() noexcept override;

    // Synchronous extraction on the calling thread, which must be (or be able to become) an STA; see
    // shell::RunOnSta. NotFound when the application has no shell item, Unsupported when the shell
    // returned an empty or unusable bitmap. Exposed for the provider's worker and for diagnostics.
    [[nodiscard]] static Result<ExtractedIcon> ExtractIcon(const ApplicationInfo& app, std::uint32_t pixelSize);

private:
    struct State;
    std::shared_ptr<State> state_;
    std::unique_ptr<WorkerPool> pool_;
};

}  // namespace aether::apps
