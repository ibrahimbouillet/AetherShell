// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/ShellCursorCompatibility.h"
#include <array>
#include <limits>

using namespace aether::showcase;
namespace {
ShellCursorSnapshot ValidSnapshot() {
    ShellCursorSnapshot value;
    value.windowClass = L"UIHubMouseHiderWindowClass";
    value.processId = value.expectedShellProcessId = 17;
    value.shellImageVerified = value.shellProcessAlive = value.shellIsCurrent = true;
    value.boundsKnown = value.frameBoundsKnown = true;
    value.bounds = value.frameBounds = {100, 200, 105, 205};
    value.styleKnown = value.extendedStyleKnown = value.classStyleKnown = true;
    value.style = 0x94000000U; value.extendedStyle = 0x08080088U;
    value.layeredAttributesKnown = true; value.layeredFlags = 2; value.alpha = 1;
    return value;
}
bool Accepted(const ShellCursorSnapshot& snapshot, std::size_t count = 0) {
    auto result = IsCompatibleShellCursorHelper(snapshot, count);
    REQUIRE(result);
    return *result;
}
}

AETHER_TEST("showcase shell helper: only one bounded low-opacity observed helper is compatible") {
    auto value = ValidSnapshot(); CHECK(Accepted(value));
    value.bounds = value.frameBounds = {100, 200, 101, 201}; CHECK(Accepted(value));
    value.alpha = 0; CHECK(Accepted(value));
    CHECK(!Accepted(value, 1)); CHECK(!Accepted(value, std::numeric_limits<std::size_t>::max()));
}
AETHER_TEST("showcase shell helper: geometry rejects expansion inversion and integer overflow") {
    constexpr auto low = std::numeric_limits<std::int32_t>::min();
    constexpr auto high = std::numeric_limits<std::int32_t>::max();
    const std::array<ShellCursorBounds, 8> invalid{{
        {100,200,106,205}, {100,200,105,206}, {100,200,100,205}, {100,200,105,200},
        {105,200,100,205}, {100,205,105,200}, {low,low,high,high}, {high,high,low,low}}};
    for (const auto bounds : invalid) {
        auto value = ValidSnapshot(); value.bounds = value.frameBounds = bounds; CHECK(!Accepted(value));
    }
    auto value = ValidSnapshot(); value.bounds = value.frameBounds = {low,low,low+1,low+1}; CHECK(Accepted(value));
}
AETHER_TEST("showcase shell helper: DWM frame must be known bounded and contained") {
    auto value = ValidSnapshot(); value.frameBoundsKnown = false; CHECK(!Accepted(value));
    const std::array<ShellCursorBounds, 5> invalid{{
        {99,200,104,205}, {100,199,105,204}, {101,200,106,205}, {100,201,105,206}, {100,200,100,200}}};
    for (const auto bounds : invalid) { value = ValidSnapshot(); value.frameBounds = bounds; CHECK(!Accepted(value)); }
    value = ValidSnapshot(); value.frameBounds = {101,201,104,204}; CHECK(Accepted(value));
}
AETHER_TEST("showcase shell helper: missing or replaced shell identity fails closed") {
    for (int field = 0; field < 6; ++field) {
        auto value = ValidSnapshot();
        if (field == 0) value.processId = 0;
        if (field == 1) value.processId = value.expectedShellProcessId + 1;
        if (field == 2) value.expectedShellProcessId = 0;
        if (field == 3) value.shellImageVerified = false;
        if (field == 4) value.shellProcessAlive = false;
        if (field == 5) value.shellIsCurrent = false;
        CHECK(!Accepted(value));
    }
}
AETHER_TEST("showcase shell helper: unknown queries and different classes or styles fail closed") {
    for (int field = 0; field < 8; ++field) {
        auto value = ValidSnapshot();
        if (field == 0) value.windowClass = L"AnotherWindowClass";
        if (field == 1) value.boundsKnown = false;
        if (field == 2) value.styleKnown = false;
        if (field == 3) value.extendedStyleKnown = false;
        if (field == 4) value.classStyleKnown = false;
        if (field == 5) value.style ^= 1U;
        if (field == 6) value.extendedStyle ^= 1U;
        if (field == 7) value.classStyle = 0x00020000U;
        CHECK(!Accepted(value));
    }
}
AETHER_TEST("showcase shell helper: only successful alpha-only attributes at zero or one are compatible") {
    auto value = ValidSnapshot(); value.layeredAttributesKnown = false; CHECK(!Accepted(value));
    for (const std::uint32_t flags : {0U,1U,3U,4U}) {
        value = ValidSnapshot(); value.layeredFlags = flags; CHECK(!Accepted(value));
    }
    for (const std::uint8_t alpha : {std::uint8_t{2},std::uint8_t{255}}) {
        value = ValidSnapshot(); value.alpha = alpha; CHECK(!Accepted(value));
    }
    CHECK(!Accepted(ShellCursorSnapshot{}));
}
