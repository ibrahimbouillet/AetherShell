// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <limits>

using namespace aether::ui::accessibility;

AETHER_TEST("accessibility: root child navigation and empty fragments") {
    CHECK(NavigateIndex(-1, 3, NavigateDirection_FirstChild) == std::optional<int>(0));
    CHECK(NavigateIndex(-1, 3, NavigateDirection_LastChild) == std::optional<int>(2));
    CHECK_FALSE(NavigateIndex(-1, 0, NavigateDirection_FirstChild).has_value());
    CHECK_FALSE(NavigateIndex(-1, 0, NavigateDirection_LastChild).has_value());
    CHECK_FALSE(NavigateIndex(-1, 3, NavigateDirection_Parent).has_value());
    CHECK_FALSE(NavigateIndex(-1, 3, NavigateDirection_NextSibling).has_value());
    CHECK_FALSE(NavigateIndex(-1, 3, NavigateDirection_PreviousSibling).has_value());
}

AETHER_TEST("accessibility: flat children have bounded siblings and one parent") {
    CHECK(NavigateIndex(0, 3, NavigateDirection_NextSibling) == std::optional<int>(1));
    CHECK(NavigateIndex(2, 3, NavigateDirection_PreviousSibling) == std::optional<int>(1));
    CHECK(NavigateIndex(2, 3, NavigateDirection_Parent) == std::optional<int>(-1));
    CHECK_FALSE(NavigateIndex(0, 3, NavigateDirection_PreviousSibling).has_value());
    CHECK_FALSE(NavigateIndex(2, 3, NavigateDirection_NextSibling).has_value());
    CHECK_FALSE(NavigateIndex(1, 3, NavigateDirection_FirstChild).has_value());
    CHECK_FALSE(NavigateIndex(1, 3, NavigateDirection_LastChild).has_value());
    CHECK_FALSE(NavigateIndex(3, 3, NavigateDirection_Parent).has_value());
    CHECK_FALSE(NavigateIndex(-2, 3, NavigateDirection_Parent).has_value());
    CHECK_FALSE(NavigateIndex(-1, -1, NavigateDirection_FirstChild).has_value());
}

AETHER_TEST("accessibility: runtime identity uses stable IDs rather than visual positions") {
    const auto first = RuntimeIdForItem(41);
    const auto second = RuntimeIdForItem(42);
    REQUIRE(first.ok()); REQUIRE(second.ok());
    CHECK_EQ(first.value()[0], UiaAppendRuntimeId);
    CHECK_EQ(first.value()[1], 41);
    CHECK(first.value() != second.value());
    CHECK(RuntimeIdForItem((std::numeric_limits<int>::max)()).ok());
    CHECK_FALSE(RuntimeIdForItem(0).ok());
    CHECK_FALSE(RuntimeIdForItem(-1).ok());
}
