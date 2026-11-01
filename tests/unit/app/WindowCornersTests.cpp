// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/WindowCorners.h"

using namespace aether;
using namespace aether::app;
namespace {
class FakeCorners final : public IWindowCornerBackend {
public:
    CornerWindowObservation observed{12, 34, {-8, -8, 1008, 708}, {0, 0, 1000, 700}, {12, 60, 988, 650}, 96, true};
    std::optional<CornerRegion> region;
    std::uintptr_t owner = 0;
    unsigned writes = 0;
    bool denyWrite = false;
    bool denyClaim = false;
    bool denyObserve = false;
    bool denyRead = false;
    std::optional<DWORD> backdrop;
    unsigned backdropWrites = 0;
    bool denyBackdropRead = false;
    bool denyBackdropWrite = false;
    bool replaceBackdropOnWrite = false;
    // AS4-03: a target that stops answering, and a write that outlives its bounded wait.
    bool responsive = true;
    bool stallWrites = false;
    std::optional<std::optional<CornerRegion>> stalled;
    unsigned probes = 0;
    bool Responsive(HWND) noexcept override { ++probes; return responsive; }
    Result<CornerWindowObservation> Observe(HWND) override {
        if (denyObserve) return MakeError(ErrorCode::Unavailable, "synthetic observation failure");
        return observed;
    }
    Result<std::optional<CornerRegion>> ReadRegion(HWND) override {
        if (denyRead) return MakeError(ErrorCode::Platform, "synthetic region read failure");
        return region;
    }
    Result<void> WriteRegion(HWND, const std::optional<CornerRegion>& value) override {
        if (denyWrite) return MakeError(ErrorCode::Platform, "synthetic region failure");
        if (stallWrites) { stalled = value; return MakeError(ErrorCode::Timeout, "synthetic pending write"); }
        region = value; ++writes; return Ok();
    }
    Result<std::optional<DWORD>> ReadBackdrop(HWND) override {
        if (denyBackdropRead) return MakeError(ErrorCode::Platform, "synthetic backdrop read failure");
        return backdrop;
    }
    Result<void> WriteBackdrop(HWND, DWORD value) override {
        if (denyBackdropWrite) return MakeError(ErrorCode::Platform, "synthetic backdrop write failure");
        backdrop = replaceBackdropOnWrite ? 4u : value; ++backdropWrites; return Ok();
    }
    bool Owns(HWND, std::uintptr_t token) noexcept override { return owner == token; }
    Result<void> Claim(HWND, std::uintptr_t token) override {
        if (denyClaim || owner) return MakeError(ErrorCode::AccessDenied, "synthetic claim failure");
        owner = token; return Ok();
    }
    void Release(HWND, std::uintptr_t token) noexcept override { if (owner == token) owner = 0; }
    bool abandoned = false;
    bool ClearAbandonedClaim(HWND) noexcept override { const bool was = abandoned; if (was) owner = 0; abandoned = false; return was; }
};
const auto window = reinterpret_cast<HWND>(std::uintptr_t{17});
}

AETHER_TEST("window corners: lease clips genuine observed geometry and skips unchanged writes") {
    FakeCorners backend;
    auto lease = WindowCornerLease::Acquire(backend, window, 1); REQUIRE(lease.ok());
    REQUIRE(backend.region.has_value()); CHECK_EQ(backend.writes, 1u);
    auto refreshed = (*lease)->Refresh(); REQUIRE(refreshed.ok()); CHECK(*refreshed);
    CHECK_EQ(backend.writes, 1u);
    backend.observed.dpi = 144;
    REQUIRE((*lease)->Refresh().ok()); CHECK_EQ(backend.writes, 2u);
    REQUIRE((*lease)->Restore().ok()); CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.owner, 0u);
    CHECK_EQ(backend.writes, 3u);
}
AETHER_TEST("window corners: restore minimize or ineligibility removes only the leased region") {
    FakeCorners backend;
    auto lease = WindowCornerLease::Acquire(backend, window, 2); REQUIRE(lease.ok());
    backend.observed.eligible = false;
    auto refreshed = (*lease)->Refresh(); REQUIRE(refreshed.ok()); CHECK_FALSE(*refreshed);
    CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.owner, 0u);
}
AETHER_TEST("window corners: custom existing and replaced application regions are preserved") {
    FakeCorners backend;
    const CornerRegion custom{std::byte{1}, std::byte{2}};
    backend.region = custom;
    CHECK_FALSE(WindowCornerLease::Acquire(backend, window, 3).ok()); CHECK_EQ(backend.writes, 0u);
    backend.region.reset();
    auto lease = WindowCornerLease::Acquire(backend, window, 3); REQUIRE(lease.ok());
    backend.region = custom;
    CHECK_FALSE((*lease)->Refresh().ok()); CHECK_FALSE((*lease)->HasOwnership());
    REQUIRE((*lease)->Restore().ok());
    CHECK(backend.region == custom); CHECK_EQ(backend.writes, 1u); CHECK_EQ(backend.owner, 0u);
}
// AS4-05: the helper's memory of the original region dies with it. Windows must not stay stranded.
AETHER_TEST("window corners: a dead guard's clip is taken over and removed; anything else is left alone") {
    FakeCorners backend;
    {
        auto first = WindowCornerLease::Acquire(backend, window, 3);
        REQUIRE(first.ok());
        REQUIRE(backend.region.has_value());
        const auto clip = backend.region;
        // The guard dies here: its lease never restores, and its claim stays on the window.
        const auto surviving = backend.owner;
        CHECK(surviving != 0u);
        backend.abandoned = true;
        auto second = WindowCornerLease::Acquire(backend, window, 4);
        REQUIRE(second.ok());
        CHECK(backend.region == clip);          // no flicker: the clip stays as it is
        CHECK_EQ(backend.owner, 4u);
        REQUIRE((*second)->Restore().ok());
        CHECK_FALSE(backend.region.has_value()); // and is gone when the new guard lets go
        CHECK_EQ(backend.owner, 0u);
    }
    // A claim of a dead guard over a shape that is not its clip: that shape is the application's.
    FakeCorners other;
    const CornerRegion custom{std::byte{1}, std::byte{2}};
    other.region = custom;
    other.owner = 9;
    other.abandoned = true;
    CHECK_FALSE(WindowCornerLease::Acquire(other, window, 5).ok());
    CHECK(other.region == custom);
    CHECK_EQ(other.writes, 0u);
}
AETHER_TEST("window corners: lost lifetime and changed process identity never restore another window") {
    for (bool lifetimeLost : {false, true}) {
        FakeCorners backend;
        auto lease = WindowCornerLease::Acquire(backend, window, 4); REQUIRE(lease.ok());
        if (lifetimeLost) backend.owner = 99;
        else ++backend.observed.process;
        CHECK_FALSE((*lease)->Refresh().ok()); CHECK_FALSE((*lease)->HasOwnership());
        REQUIRE((*lease)->Restore().ok());
        CHECK_EQ(backend.writes, 1u);
    }
}
AETHER_TEST("window corners: refused application and invalid geometry leave no armed property") {
    FakeCorners backend;
    backend.denyClaim = true;
    CHECK_FALSE(WindowCornerLease::Acquire(backend, window, 5).ok()); CHECK_EQ(backend.writes, 0u);
    backend.denyClaim = false; backend.denyWrite = true;
    CHECK_FALSE(WindowCornerLease::Acquire(backend, window, 5).ok()); CHECK_EQ(backend.owner, 0u);
    backend.denyWrite = false; backend.observed.frame = {};
    CHECK_FALSE(WindowCornerLease::Acquire(backend, window, 5).ok()); CHECK_EQ(backend.owner, 0u);
    backend.observed.eligible = false;
    CHECK_FALSE(WindowCornerLease::Acquire(backend, window, 5).ok());
    CHECK_FALSE(WindowCornerLease::Acquire(backend, nullptr, 5).ok());
    CHECK_FALSE(WindowCornerLease::Acquire(backend, window, 0).ok());
}
AETHER_TEST("window corners: destructor restores a region while its identity remains owned") {
    FakeCorners backend;
    { auto lease = WindowCornerLease::Acquire(backend, window, 6); REQUIRE(lease.ok()); }
    CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.writes, 2u); CHECK_EQ(backend.owner, 0u);
}
AETHER_TEST("window corners: temporary observation read and write failures retain exact recovery ownership") {
    for (int kind : {0, 1, 2}) {
        FakeCorners backend;
        auto lease = WindowCornerLease::Acquire(backend, window, 7); REQUIRE(lease.ok());
        const auto clipped = backend.region;
        backend.observed.dpi = 144; // force a real geometry write in the third case
        if (kind == 0) backend.denyObserve = true;
        if (kind == 1) backend.denyRead = true;
        if (kind == 2) backend.denyWrite = true;
        CHECK_FALSE((*lease)->Refresh().ok()); CHECK((*lease)->HasOwnership());
        CHECK_FALSE((*lease)->Restore().ok()); CHECK((*lease)->HasOwnership());
        CHECK(backend.region == clipped); CHECK_EQ(backend.owner, 7u);
        backend.denyObserve = backend.denyRead = backend.denyWrite = false;
        REQUIRE((*lease)->Restore().ok()); CHECK_FALSE((*lease)->HasOwnership());
        CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.owner, 0u);
    }
}
AETHER_TEST("window corners: internal dispatch rejects malformed handles and preserves normal arguments") {
    auto normal = DispatchWindowCornerGuard({L"--settings"}); REQUIRE(normal.ok()); CHECK_FALSE(normal->has_value());
    CHECK_FALSE(DispatchWindowCornerGuard({L"--window-corner-guard"}).ok());
    CHECK_FALSE(DispatchWindowCornerGuard({L"--window-corner-guard", L"0", L"1", L"2"}).ok());
    CHECK_FALSE(DispatchWindowCornerGuard({L"--window-corner-guard", L"1x", L"1", L"2"}).ok());
    CHECK_FALSE(DispatchWindowCornerGuard({L"--window-corner-guard", L"9999999999999999999999", L"1", L"2"}).ok());
}
AETHER_TEST("window corners: isolated controller refuses native access without spawning a child") {
    WindowCorners controller(false, L"not-an-executable");
    CHECK_FALSE(controller.Request(true).ok()); REQUIRE(controller.Request(false).ok());
    controller.Stop(); CHECK_FALSE(controller.Request(true).ok());
}

AETHER_TEST("window corners: full-window system materials are leased and restored exactly") {
    for (const DWORD material : {2u, 3u, 4u}) {
        FakeCorners backend; backend.backdrop = material;
        auto lease = WindowCornerLease::Acquire(backend, window, 8); REQUIRE(lease.ok());
        CHECK(backend.backdrop == 1u); CHECK_EQ(backend.backdropWrites, 1u);
        REQUIRE((*lease)->Refresh().ok()); CHECK_EQ(backend.backdropWrites, 1u);
        REQUIRE((*lease)->Restore().ok()); CHECK(backend.backdrop == material);
        CHECK_EQ(backend.backdropWrites, 2u); CHECK_FALSE(backend.region.has_value());
        CHECK_FALSE((*lease)->HasOwnership());
    }
}
AETHER_TEST("window corners: absent automatic and absent-material policies are never rewritten") {
    for (const auto material : {std::optional<DWORD>{}, std::optional<DWORD>{0}, std::optional<DWORD>{1}}) {
        FakeCorners backend; backend.backdrop = material;
        { auto lease = WindowCornerLease::Acquire(backend, window, 9); REQUIRE(lease.ok()); }
        CHECK(backend.backdrop == material); CHECK_EQ(backend.backdropWrites, 0u);
        CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.owner, 0u);
    }
}
AETHER_TEST("window corners: app region replacement still restores the separately leased material") {
    FakeCorners backend; backend.backdrop = 2;
    auto lease = WindowCornerLease::Acquire(backend, window, 10); REQUIRE(lease.ok());
    const CornerRegion replacement{std::byte{5}}; backend.region = replacement;
    CHECK_FALSE((*lease)->Refresh().ok()); CHECK_FALSE((*lease)->HasOwnership());
    CHECK(backend.region == replacement); CHECK(backend.backdrop == 2u);
    CHECK_EQ(backend.writes, 1u); CHECK_EQ(backend.backdropWrites, 2u);
}
AETHER_TEST("window corners: app material replacement wins and releases the leased region") {
    FakeCorners backend; backend.backdrop = 2;
    auto lease = WindowCornerLease::Acquire(backend, window, 11); REQUIRE(lease.ok());
    backend.backdrop = 4;
    CHECK_FALSE((*lease)->Refresh().ok()); CHECK_FALSE((*lease)->HasOwnership());
    CHECK_FALSE(backend.region.has_value()); CHECK(backend.backdrop == 4u);
    CHECK_EQ(backend.backdropWrites, 1u);
}
AETHER_TEST("window corners: partial first application retains the material recovery snapshot") {
    FakeCorners backend; backend.backdrop = 3; backend.denyWrite = true;
    auto lease = WindowCornerLease::Acquire(backend, window, 12); REQUIRE(lease.ok());
    CHECK((*lease)->HasOwnership()); CHECK(backend.backdrop == 1u); CHECK_FALSE(backend.region.has_value());
    backend.denyBackdropWrite = true;
    CHECK_FALSE((*lease)->Restore().ok()); CHECK((*lease)->HasOwnership());
    backend.denyBackdropWrite = false;
    REQUIRE((*lease)->Restore().ok()); CHECK(backend.backdrop == 3u); CHECK_EQ(backend.owner, 0u);
}
AETHER_TEST("window corners: material read and restore failures retain recovery independently of region") {
    for (const bool failRead : {false, true}) {
        FakeCorners backend; backend.backdrop = 2;
        auto lease = WindowCornerLease::Acquire(backend, window, 13); REQUIRE(lease.ok());
        if (failRead) backend.denyBackdropRead = true;
        else backend.denyBackdropWrite = true;
        CHECK_FALSE((*lease)->Restore().ok()); CHECK((*lease)->HasOwnership());
        CHECK_FALSE(backend.region.has_value()); CHECK(backend.backdrop == 1u);
        backend.denyBackdropRead = backend.denyBackdropWrite = false;
        REQUIRE((*lease)->Restore().ok()); CHECK(backend.backdrop == 2u); CHECK_EQ(backend.owner, 0u);
    }
}
AETHER_TEST("window corners: material acquisition failures never leave untracked mutations") {
    for (const bool failRead : {false, true}) {
        FakeCorners backend; backend.backdrop = 2;
        if (failRead) backend.denyBackdropRead = true;
        else backend.denyBackdropWrite = true;
        CHECK_FALSE(WindowCornerLease::Acquire(backend, window, 14).ok());
        CHECK(backend.backdrop == 2u); CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.owner, 0u);
    }
}
AETHER_TEST("window corners: synchronous application material reassertion is not overwritten or clipped") {
    FakeCorners backend; backend.backdrop = 2; backend.replaceBackdropOnWrite = true;
    auto lease = WindowCornerLease::Acquire(backend, window, 15);
    REQUIRE(!lease.ok()); CHECK(lease.error().code == ErrorCode::InvalidState);
    CHECK(backend.backdrop == 4u); CHECK_FALSE(backend.region.has_value());
    CHECK_EQ(backend.writes, 0u); CHECK_EQ(backend.backdropWrites, 1u); CHECK_EQ(backend.owner, 0u);
}
AETHER_TEST("window corners: later application material opt-in relinquishes an automatic-policy clip") {
    FakeCorners backend; backend.backdrop = 0;
    auto lease = WindowCornerLease::Acquire(backend, window, 16); REQUIRE(lease.ok());
    backend.backdrop = 2;
    CHECK_FALSE((*lease)->Refresh().ok()); CHECK_FALSE((*lease)->HasOwnership());
    CHECK(backend.backdrop == 2u); CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.backdropWrites, 0u);
}
AETHER_TEST("window corners: a give-back records whether the app re-asserted its own material, chose another or its own region") {
    // D46: Chromium re-applying the very Mica it had before the clip is distinguishable from a new choice.
    FakeCorners reasserted; reasserted.backdrop = 2;
    auto lease = WindowCornerLease::Acquire(reasserted, window, 30); REQUIRE(lease.ok());
    CHECK((*lease)->Replacement() == CornerReplacement::None);
    reasserted.backdrop = 2;
    CHECK_FALSE((*lease)->Refresh().ok());
    CHECK((*lease)->Replacement() == CornerReplacement::MaterialReasserted);
    CHECK(reasserted.backdrop == 2u); CHECK_FALSE(reasserted.region.has_value());  // still given back at once

    FakeCorners other; other.backdrop = 2;
    lease = WindowCornerLease::Acquire(other, window, 31); REQUIRE(lease.ok());
    other.backdrop = 4;
    CHECK_FALSE((*lease)->Refresh().ok());
    CHECK((*lease)->Replacement() == CornerReplacement::Material);

    FakeCorners region;
    lease = WindowCornerLease::Acquire(region, window, 32); REQUIRE(lease.ok());
    region.region = CornerRegion{std::byte{1}, std::byte{2}};
    CHECK_FALSE((*lease)->Refresh().ok());
    CHECK((*lease)->Replacement() == CornerReplacement::Region);
}
AETHER_TEST("window corners: only a re-asserted previous material is clipped again, later each time and a bounded number of times") {
    using std::chrono::milliseconds;
    CHECK(CornerRetryDelay(CornerReplacement::MaterialReasserted, 0) == milliseconds{250});
    CHECK(CornerRetryDelay(CornerReplacement::MaterialReasserted, 1) == milliseconds{500});
    CHECK(CornerRetryDelay(CornerReplacement::MaterialReasserted, 4) == milliseconds{4000});
    CHECK(CornerRetryDelay(CornerReplacement::MaterialReasserted, 7) == milliseconds{8000});
    CHECK_FALSE(CornerRetryDelay(CornerReplacement::MaterialReasserted, kCornerMaterialRetries).has_value());
    CHECK_FALSE(CornerRetryDelay(CornerReplacement::MaterialReasserted, -1).has_value());
    for (const auto other : {CornerReplacement::None, CornerReplacement::Region, CornerReplacement::Material})
        CHECK_FALSE(CornerRetryDelay(other, 0).has_value());  // the application's own choice wins
}

AETHER_TEST("window corners: an unresponsive target keeps its clip and snapshot; changes wait (AS4-03)") {
    FakeCorners backend;
    auto lease = WindowCornerLease::Acquire(backend, window, 40); REQUIRE(lease.ok());
    const auto clipped = backend.region; REQUIRE(clipped.has_value());
    REQUIRE_EQ(backend.writes, 1u);
    backend.responsive = false;
    // A busy page is not given back: unchanged geometry needs no mutation and is not a deferral.
    auto steady = (*lease)->Refresh(); REQUIRE(steady.ok()); CHECK(*steady);
    // A geometry change waits instead of writing into the unresponsive target.
    backend.observed.dpi = 144;
    auto changed = (*lease)->Refresh(); REQUIRE_FALSE(changed.ok());
    CHECK_EQ(changed.error().code, ErrorCode::Timeout);
    CHECK((*lease)->HasOwnership()); CHECK_EQ(backend.writes, 1u); CHECK(backend.region == clipped);
    // Restoration (the window left its maximize while hung) waits too; the snapshot is retained.
    backend.observed.eligible = false;
    auto restore = (*lease)->Refresh(); REQUIRE_FALSE(restore.ok());
    CHECK_EQ(restore.error().code, ErrorCode::Timeout);
    CHECK((*lease)->HasOwnership()); CHECK_EQ(backend.writes, 1u); CHECK(backend.region == clipped);
    // Once it answers again the recovery completes exactly once, conditionally.
    backend.responsive = true;
    auto recovered = (*lease)->Refresh(); REQUIRE(recovered.ok()); CHECK_FALSE(*recovered);
    CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.owner, 0u); CHECK_EQ(backend.writes, 2u);
}
AETHER_TEST("window corners: first acquisition of an unresponsive target writes nothing and keeps no claim") {
    FakeCorners backend;
    backend.responsive = false;
    auto lease = WindowCornerLease::Acquire(backend, window, 41);
    REQUIRE_FALSE(lease.ok()); CHECK_EQ(lease.error().code, ErrorCode::Timeout);
    CHECK_EQ(backend.writes, 0u); CHECK_EQ(backend.owner, 0u); CHECK_FALSE(backend.region.has_value());
}
AETHER_TEST("window corners: a pending write is reconciled from what actually landed") {
    FakeCorners backend;
    auto lease = WindowCornerLease::Acquire(backend, window, 42); REQUIRE(lease.ok());
    const auto first = backend.region;
    backend.stallWrites = true;
    backend.observed.dpi = 144;
    auto pending = (*lease)->Refresh(); REQUIRE_FALSE(pending.ok());
    CHECK_EQ(pending.error().code, ErrorCode::Timeout); CHECK((*lease)->HasOwnership());
    REQUIRE(backend.stalled.has_value()); REQUIRE(backend.stalled->has_value());
    // The stalled write lands later: it is adopted, not mistaken for an application replacement.
    backend.region = *backend.stalled;
    backend.stallWrites = false;
    auto adopted = (*lease)->Refresh(); REQUIRE(adopted.ok()); CHECK(*adopted);
    CHECK_EQ(backend.writes, 1u);  // no further write: the landed region is the wanted one
    CHECK((*lease)->HasOwnership());
    // A pending write that never landed is retried from the unchanged region.
    backend.stallWrites = true;
    backend.observed.dpi = 192;
    REQUIRE_FALSE((*lease)->Refresh().ok());
    backend.stallWrites = false;
    auto retried = (*lease)->Refresh(); REQUIRE(retried.ok()); CHECK(*retried);
    CHECK_EQ(backend.writes, 2u); CHECK(backend.region != first);
    REQUIRE((*lease)->Restore().ok()); CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.owner, 0u);
}
AETHER_TEST("window corners: restoration with a write still pending keeps the claim until it is known") {
    FakeCorners backend;
    auto lease = WindowCornerLease::Acquire(backend, window, 43); REQUIRE(lease.ok());
    backend.stallWrites = true;
    auto restore = (*lease)->Restore(); REQUIRE_FALSE(restore.ok());
    CHECK_EQ(restore.error().code, ErrorCode::Timeout); CHECK((*lease)->HasOwnership());
    backend.responsive = false;
    CHECK_EQ((*lease)->Restore().error().code, ErrorCode::Timeout);
    backend.responsive = true;
    backend.region = *backend.stalled;  // the restoration landed meanwhile
    backend.stallWrites = false;
    REQUIRE((*lease)->Restore().ok());
    CHECK_FALSE(backend.region.has_value()); CHECK_EQ(backend.owner, 0u);
}
AETHER_TEST("window corners: helper start failures are bounded, reported and retried only with budget (AS4-04)") {
    WindowCornersOptions options; options.firstRetryDelay = std::chrono::milliseconds(5); options.maximumAttempts = 3;
    const auto missing = std::filesystem::temp_directory_path() / L"aether-missing-corner-helper" / L"AetherShellApp.exe";
    WindowCorners corners(true, missing, options);
    CHECK(corners.Health().state == CornerGuardState::Off);
    REQUIRE(corners.Request(true).ok());
    const auto wait = [&](auto predicate) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!predicate() && std::chrono::steady_clock::now() < end) { corners.Poll(); ::Sleep(5); }
        return predicate();
    };
    REQUIRE(wait([&] { return corners.Health().failures >= 3; }));
    const auto health = corners.Health();
    CHECK(health.wanted); CHECK(health.state == CornerGuardState::Failed); CHECK_EQ(health.failures, 3u);
    // The budget is spent: further supervision and identical requests do not start it again.
    for (int i = 0; i < 20; ++i) { corners.Poll(); REQUIRE(corners.Request(true).ok()); ::Sleep(2); }
    CHECK_EQ(corners.Health().failures, 3u);
    // A fresh request (off, then on) gets a fresh budget.
    REQUIRE(corners.Request(false).ok());
    REQUIRE(wait([&] { return corners.Health().state == CornerGuardState::Off; }));
    REQUIRE(corners.Request(true).ok());
    REQUIRE(wait([&] { return corners.Health().failures >= 1 && corners.Health().state == CornerGuardState::Failed; }));
    corners.Stop();
    CHECK(corners.Health().state == CornerGuardState::Off);
    CHECK_FALSE(corners.Request(true).ok());
}
AETHER_TEST("window corners: isolated sessions never start a helper") {
    WindowCorners corners(false, std::filesystem::temp_directory_path() / L"AetherShellApp.exe");
    CHECK_EQ(corners.Request(true).error().code, ErrorCode::AccessDenied);
    CHECK(corners.Health().state == CornerGuardState::Off);
}
