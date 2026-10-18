// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Extracts real icons of applications discovered on this machine.
#include "AetherTest.h"

#include "applications/IconPixels.h"
#include "applications/ShellApplicationCatalog.h"
#include "applications/ShellIconProvider.h"
#include "core/Dispatcher.h"
#include "core/Strings.h"
#include "platform/windows/ComApartment.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

const std::vector<ApplicationInfo>& DiscoveredApps() {
    static const std::vector<ApplicationInfo> apps = [] {
        auto result = ShellApplicationCatalog().DiscoverApplications(CancellationToken{});
        return result.ok() ? std::move(result).value() : std::vector<ApplicationInfo>{};
    }();
    return apps;
}

// One packaged and two desktop applications when available, otherwise the first three.
std::vector<ApplicationInfo> PickSample() {
    std::vector<ApplicationInfo> sample;
    const auto& apps = DiscoveredApps();
    for (const auto& app : apps) {
        if (app.kind == ApplicationKind::Packaged) {
            sample.push_back(app);
            break;
        }
    }
    for (const auto& app : apps) {
        if (sample.size() >= 3) break;
        if (app.kind == ApplicationKind::Desktop) sample.push_back(app);
    }
    for (const auto& app : apps) {
        if (sample.size() >= 3) break;
        if (std::none_of(sample.begin(), sample.end(), [&](const ApplicationInfo& s) { return s.id == app.id; })) {
            sample.push_back(app);
        }
    }
    return sample;
}

// Runs the manual dispatcher until `done` holds or the timeout expires.
bool PumpUntil(ManualDispatcher& dispatcher, const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        dispatcher.RunPending();
        if (done()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::string FormatName(AlphaFormat format) {
    switch (format) {
        case AlphaFormat::NoAlpha: return "no-alpha";
        case AlphaFormat::Transparent: return "transparent";
        case AlphaFormat::Opaque: return "opaque";
        case AlphaFormat::Ambiguous: return "ambiguous";
        case AlphaFormat::Straight: return "straight";
    }
    return "?";
}

std::string KindName(ApplicationKind kind) {
    switch (kind) {
        case ApplicationKind::Desktop: return "Desktop";
        case ApplicationKind::Packaged: return "Packaged";
        case ApplicationKind::Other: return "Other";
    }
    return "?";
}

bool HasVisiblePixels(const IconImage& image) {
    for (std::size_t i = 3; i < image.pixels.size(); i += 4) {
        if (image.pixels[i] != 0) return true;
    }
    return false;
}

}  // namespace

AETHER_TEST("icons: provider extracts premultiplied icons for discovered applications") {
    const auto sample = PickSample();
    REQUIRE(sample.size() == 3);

    ManualDispatcher dispatcher;
    ShellIconProvider provider(dispatcher);
    std::map<std::wstring, std::shared_ptr<const IconImage>> results;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& app : sample) {
        provider.RequestIcon(IconRequest{app.id, app, 48},
                             [&](const std::wstring& key, std::shared_ptr<const IconImage> image) {
                                 CHECK(dispatcher.IsUiThread());
                                 results[key] = std::move(image);
                             });
    }
    REQUIRE(PumpUntil(dispatcher, [&] { return results.size() == sample.size(); }, std::chrono::seconds(30)));
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    TEST_NOTE("3 icons in " + std::to_string(elapsed) + " ms");

    for (const auto& app : sample) {
        const auto& image = results[app.id];
        if (!image) TEST_NOTE("no icon for " + text::Narrow(app.displayName));
        REQUIRE(image != nullptr);
        CHECK(image->width >= 48);  // SIIGBF_BIGGERSIZEOK: may be larger, never smaller here
        CHECK(image->height >= 48);
        CHECK_EQ(image->pixels.size(), std::size_t{image->width} * image->height * 4u);
        CHECK(IsPremultipliedBgra(image->pixels));  // every colour component <= alpha
        CHECK(HasVisiblePixels(*image));
        TEST_NOTE(text::Narrow(app.displayName) + ": " + std::to_string(image->width) + "x" +
                  std::to_string(image->height));
    }
}

AETHER_TEST("icons: survey of the pixel format returned by the shell") {
    // Documents what IShellItemImageFactory::GetImage hands back before normalization. The test thread
    // hosts one STA for the whole survey (ExtractIcon then runs inline instead of per-call COM setup).
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    // Straight alpha is provable (a colour component above its alpha); premultiplied alpha is not, so
    // "ambiguous" images are listed with their translucent pixel counts.
    const auto& apps = DiscoveredApps();
    REQUIRE(apps.size() > 10);
    std::map<std::string, int> formats;  // "<kind> <format>" -> count
    int failures = 0;
    int examined = 0;
    int straight = 0;     // provably straight alpha
    int translucent = 0;  // images with translucent pixels (straight or ambiguous)
    for (const auto& app : apps) {
        if (examined >= 48) break;
        ++examined;
        auto extracted = ShellIconProvider::ExtractIcon(app, 48);
        if (!extracted.ok()) {
            ++failures;
            TEST_NOTE("no icon for " + text::Narrow(app.displayName) + ": " + extracted.error().Describe());
            continue;
        }
        const auto& source = extracted.value().source;
        ++formats[KindName(app.kind) + " " + FormatName(source.format)];
        if (source.format == AlphaFormat::Straight) ++straight;
        if (source.translucentPixels > 0) ++translucent;
        if (source.format == AlphaFormat::Ambiguous) {
            TEST_NOTE("  ambiguous: " + text::Narrow(app.displayName) + " (" + KindName(app.kind) + "), " +
                      std::to_string(source.translucentPixels) + " translucent pixels");
        }
        CHECK(IsPremultipliedBgra(extracted.value().image.pixels));
    }
    std::string summary = "source formats over " + std::to_string(examined) + " applications:";
    for (const auto& [name, count] : formats) summary += " [" + name + "]=" + std::to_string(count);
    summary += " failures=" + std::to_string(failures);
    TEST_NOTE(summary);
    CHECK(failures < examined);
    // The provider premultiplies every translucent image (AmbiguousAlpha::TreatAsStraight) because the
    // shell returns straight alpha. Should a future shell return premultiplied DIBs, no image could be
    // proven straight any more and this check flags the decision for review.
    if (translucent > 0) CHECK(straight > 0);
}

AETHER_TEST("icons: duplicate requests are coalesced and repeated ones hit the cache") {
    const auto sample = PickSample();
    REQUIRE_FALSE(sample.empty());
    const auto& app = sample.front();

    ManualDispatcher dispatcher;
    ShellIconProvider provider(dispatcher);
    std::vector<std::shared_ptr<const IconImage>> images;
    const auto collect = [&](const std::wstring&, std::shared_ptr<const IconImage> image) { images.push_back(image); };
    provider.RequestIcon(IconRequest{L"dup", app, 32}, collect);
    provider.RequestIcon(IconRequest{L"dup", app, 32}, collect);
    REQUIRE(PumpUntil(dispatcher, [&] { return images.size() == 2; }, std::chrono::seconds(30)));
    REQUIRE(images[0] != nullptr);
    CHECK(images[0] == images[1]);  // one extraction served both callers

    provider.RequestIcon(IconRequest{L"dup", app, 32}, collect);
    CHECK_EQ(dispatcher.PendingCount(), std::size_t{1});  // served from the cache: already queued
    dispatcher.RunPending();
    REQUIRE_EQ(images.size(), std::size_t{3});
    CHECK(images[2] == images[0]);
}

AETHER_TEST("icons: CancelAll drops callbacks of requests in flight") {
    const auto sample = PickSample();
    REQUIRE_FALSE(sample.empty());
    ManualDispatcher dispatcher;
    ShellIconProvider provider(dispatcher);
    int calls = 0;
    for (const auto& app : sample) {
        provider.RequestIcon(IconRequest{L"cancel:" + app.id, app, 40},
                             [&](const std::wstring&, std::shared_ptr<const IconImage>) { ++calls; });
    }
    provider.CancelAll();
    PumpUntil(dispatcher, [] { return false; }, std::chrono::milliseconds(1500));
    CHECK_EQ(calls, 0);

    // The provider keeps working after a cancellation.
    std::shared_ptr<const IconImage> after;
    bool done = false;
    provider.RequestIcon(IconRequest{L"after", sample.front(), 40}, [&](const std::wstring&, std::shared_ptr<const IconImage> image) {
        after = std::move(image);
        done = true;
    });
    REQUIRE(PumpUntil(dispatcher, [&] { return done; }, std::chrono::seconds(30)));
    CHECK(after != nullptr);
}
