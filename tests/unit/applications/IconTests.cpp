// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "applications/IconCache.h"
#include "applications/IconPixels.h"
#include "applications/ShellIconProvider.h"
#include "core/Dispatcher.h"

#include <array>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

std::shared_ptr<const IconImage> MakeImage(std::uint32_t side) {
    auto image = std::make_shared<IconImage>();
    image->width = side;
    image->height = side;
    image->pixels.assign(static_cast<std::size_t>(side) * side * 4u, 0x80);
    return image;
}

}  // namespace

AETHER_TEST("icon pixels: bitmaps without alpha become opaque") {
    std::vector<std::uint8_t> pixels{10, 20, 30, 0, 40, 50, 60, 0};
    const auto analysis = NormalizeToPremultipliedBgra(pixels, AmbiguousAlpha::TreatAsStraight);
    CHECK_EQ(analysis.format, AlphaFormat::NoAlpha);
    CHECK_EQ(pixels, (std::vector<std::uint8_t>{10, 20, 30, 255, 40, 50, 60, 255}));
    CHECK(IsPremultipliedBgra(pixels));
}

AETHER_TEST("icon pixels: fully transparent and fully opaque bitmaps") {
    std::vector<std::uint8_t> transparent(16, 0);
    CHECK_EQ(NormalizeToPremultipliedBgra(transparent, AmbiguousAlpha::TreatAsStraight).format,
             AlphaFormat::Transparent);
    CHECK_EQ(transparent, std::vector<std::uint8_t>(16, 0));

    std::vector<std::uint8_t> opaque{1, 2, 3, 255, 250, 251, 252, 255};
    const auto original = opaque;
    const auto analysis = NormalizeToPremultipliedBgra(opaque, AmbiguousAlpha::TreatAsStraight);
    CHECK_EQ(analysis.format, AlphaFormat::Opaque);
    CHECK_EQ(analysis.translucentPixels, std::size_t{0});
    CHECK_EQ(opaque, original);
}

AETHER_TEST("icon pixels: straight alpha is detected and premultiplied with rounding") {
    std::vector<std::uint8_t> pixels{200, 100, 50, 128,   // half transparent, colour above alpha
                                     255, 255, 255, 0,    // invisible colour must vanish
                                     12, 34, 56, 255,     // opaque stays as is
                                     255, 255, 255, 1};
    CHECK_FALSE(IsPremultipliedBgra(pixels));
    const auto analysis = NormalizeToPremultipliedBgra(pixels, AmbiguousAlpha::TreatAsPremultiplied);
    CHECK_EQ(analysis.format, AlphaFormat::Straight);  // provable regardless of the ambiguity policy
    CHECK_EQ(analysis.translucentPixels, std::size_t{2});
    CHECK_EQ(analysis.colourAboveAlphaPixels, std::size_t{3});
    CHECK_EQ(pixels, (std::vector<std::uint8_t>{100, 50, 25, 128, 0, 0, 0, 0, 12, 34, 56, 255, 1, 1, 1, 1}));
    CHECK(IsPremultipliedBgra(pixels));
}

AETHER_TEST("icon pixels: ambiguous images follow the caller's policy") {
    const std::vector<std::uint8_t> ambiguous{64, 32, 16, 128, 0, 0, 0, 0, 9, 9, 9, 255};
    CHECK_EQ(AnalyzeAlpha(ambiguous).format, AlphaFormat::Ambiguous);

    auto keep = ambiguous;
    CHECK_EQ(NormalizeToPremultipliedBgra(keep, AmbiguousAlpha::TreatAsPremultiplied).format, AlphaFormat::Ambiguous);
    CHECK_EQ(keep, ambiguous);

    auto premultiplied = ambiguous;
    CHECK_EQ(NormalizeToPremultipliedBgra(premultiplied, AmbiguousAlpha::TreatAsStraight).format,
             AlphaFormat::Ambiguous);
    CHECK_EQ(premultiplied, (std::vector<std::uint8_t>{32, 16, 8, 128, 0, 0, 0, 0, 9, 9, 9, 255}));
}

AETHER_TEST("icon pixels: premultiplication is exact for every alpha") {
    for (unsigned alpha = 1; alpha < 255; ++alpha) {
        std::vector<std::uint8_t> pixels{255, 128, 0, static_cast<std::uint8_t>(alpha)};
        PremultiplyBgra(pixels);
        CHECK_EQ(static_cast<unsigned>(pixels[0]), alpha);
        CHECK_EQ(static_cast<unsigned>(pixels[1]), (128u * alpha + 127u) / 255u);
        CHECK_EQ(static_cast<unsigned>(pixels[2]), 0u);
        CHECK_EQ(static_cast<unsigned>(pixels[3]), alpha);
    }
}

AETHER_TEST("icon cache: least recently used entries are evicted first") {
    IconCache cache(2, 1u << 20);
    cache.Insert(L"a", 32, MakeImage(4));
    cache.Insert(L"b", 32, MakeImage(4));
    CHECK(cache.Find(L"a", 32) != nullptr);  // "a" becomes most recent
    cache.Insert(L"c", 32, MakeImage(4));
    CHECK_EQ(cache.Size(), std::size_t{2});
    CHECK(cache.Find(L"b", 32) == nullptr);
    CHECK(cache.Find(L"a", 32) != nullptr);
    CHECK(cache.Find(L"c", 32) != nullptr);
}

AETHER_TEST("icon cache: keys include the pixel size") {
    IconCache cache(8, 1u << 20);
    const auto small = MakeImage(2);
    const auto large = MakeImage(8);
    cache.Insert(L"app", 16, small);
    cache.Insert(L"app", 64, large);
    CHECK(cache.Find(L"app", 16) == small);
    CHECK(cache.Find(L"app", 64) == large);
    CHECK(cache.Find(L"app", 32) == nullptr);
    CHECK_NE(IconCache::CompositeKey(L"1:x", 2), IconCache::CompositeKey(L"x", 21));
}

AETHER_TEST("icon cache: the byte budget is enforced") {
    IconCache cache(100, 200);  // 200 bytes: three 4x4 images (64 bytes each) fit, a fourth does not
    cache.Insert(L"a", 1, MakeImage(4));
    cache.Insert(L"b", 1, MakeImage(4));
    cache.Insert(L"c", 1, MakeImage(4));
    CHECK_EQ(cache.Bytes(), std::size_t{192});
    cache.Insert(L"d", 1, MakeImage(4));
    CHECK_EQ(cache.Size(), std::size_t{3});
    CHECK(cache.Find(L"a", 1) == nullptr);
    CHECK_EQ(cache.Bytes(), std::size_t{192});

    cache.Insert(L"huge", 1, MakeImage(16));  // 1024 bytes: larger than the whole budget, not cached
    CHECK(cache.Find(L"huge", 1) == nullptr);
    CHECK_EQ(cache.Size(), std::size_t{3});

    cache.Insert(L"b", 1, MakeImage(2));  // replacing updates the accounting
    CHECK_EQ(cache.Bytes(), std::size_t{64 + 64 + 16});
    cache.Insert(L"null", 1, nullptr);
    CHECK_EQ(cache.Size(), std::size_t{3});
    cache.Clear();
    CHECK_EQ(cache.Size(), std::size_t{0});
    CHECK_EQ(cache.Bytes(), std::size_t{0});
}

AETHER_TEST("icon provider: invalid requests complete with a null image on the UI thread") {
    ManualDispatcher dispatcher;
    ShellIconProvider provider(dispatcher);
    int calls = 0;
    std::wstring seenKey = L"unset";
    std::shared_ptr<const IconImage> seenImage = MakeImage(1);
    provider.RequestIcon(IconRequest{L"", {}, 32}, [&](const std::wstring& key, std::shared_ptr<const IconImage> image) {
        ++calls;
        seenKey = key;
        seenImage = std::move(image);
    });
    provider.RequestIcon(IconRequest{L"too-big", {}, ShellIconProvider::kMaxPixelSize + 1},
                         [&](const std::wstring&, std::shared_ptr<const IconImage> image) {
                             ++calls;
                             CHECK(image == nullptr);
                         });
    provider.RequestIcon(IconRequest{L"zero", {}, 0}, [&](const std::wstring&, std::shared_ptr<const IconImage>) { ++calls; });
    CHECK_EQ(calls, 0);  // never synchronous
    dispatcher.RunPending();
    CHECK_EQ(calls, 3);
    CHECK(seenKey.empty());
    CHECK(seenImage == nullptr);
}

AETHER_TEST("icon provider: coalesced requests all complete, and a failure does not block later requests") {
    // The application has no shell item at all, so extraction fails on the worker without asking the
    // shell anything; every caller must still be answered (with a null image), and the next request
    // for the same key must not be coalesced into a finished extraction.
    ManualDispatcher dispatcher;
    ShellIconProvider provider(dispatcher);
    ApplicationInfo ghost;
    ghost.displayName = L"Ghost";
    int calls = 0;
    int nonNull = 0;
    const auto callback = [&](const std::wstring& key, std::shared_ptr<const IconImage> image) {
        CHECK_EQ(key, std::wstring(L"ghost"));
        ++calls;
        if (image) ++nonNull;
    };
    const auto pumpUntil = [&](int expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (calls < expected && std::chrono::steady_clock::now() < deadline) {
            dispatcher.RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return calls >= expected;
    };
    provider.RequestIcon(IconRequest{L"ghost", ghost, 32}, callback);
    provider.RequestIcon(IconRequest{L"ghost", ghost, 32}, callback);
    REQUIRE(pumpUntil(2));
    CHECK_EQ(nonNull, 0);

    provider.RequestIcon(IconRequest{L"ghost", ghost, 32}, callback);  // failures are not cached
    REQUIRE(pumpUntil(3));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    dispatcher.RunPending();
    CHECK_EQ(calls, 3);  // each callback exactly once
    CHECK_EQ(nonNull, 0);
}

AETHER_TEST("icon provider: CancelAll and destruction drop pending callbacks") {
    ManualDispatcher dispatcher;
    int calls = 0;
    {
        ShellIconProvider provider(dispatcher);
        provider.RequestIcon(IconRequest{L"", {}, 32}, [&](const std::wstring&, std::shared_ptr<const IconImage>) { ++calls; });
        provider.CancelAll();
        dispatcher.RunPending();
        CHECK_EQ(calls, 0);

        provider.RequestIcon(IconRequest{L"", {}, 32}, [&](const std::wstring&, std::shared_ptr<const IconImage>) { ++calls; });
    }  // destroyed with a completion still queued on the dispatcher
    CHECK_EQ(dispatcher.PendingCount(), std::size_t{1});
    dispatcher.RunPending();
    CHECK_EQ(calls, 0);
}
