// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Text formats: family resolution with the Segoe UI fallback, the optical Small family and tabular
// figures. Uses a shared DirectWrite factory only (no device, no window).
#include "AetherTest.h"

#include "graphics/TextCache.h"

#include <string>

using namespace aether::graphics;

namespace {

winrt::com_ptr<IDWriteFactory> DWrite() {
    winrt::com_ptr<IDWriteFactory> factory;
    (void)::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown**>(factory.put()));
    return factory;
}

bool FamilyInstalled(IDWriteFactory* factory, const wchar_t* family) {
    winrt::com_ptr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(factory->GetSystemFontCollection(fonts.put(), FALSE)) && fonts &&
           SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
}

std::wstring FamilyOf(IDWriteTextFormat* format) {
    if (!format) return {};
    wchar_t name[128]{};
    if (FAILED(format->GetFontFamilyName(name, 128))) return {};
    return name;
}

}  // namespace

AETHER_TEST("text cache: families resolve to Segoe UI when the preferred family is missing") {
    const auto factory = DWrite();
    REQUIRE(factory);
    TextCache cache;
    cache.Initialize(factory.get(), L"Aether Missing Family Text", L"Aether Missing Family Display");
    TextStyle style;
    CHECK(FamilyOf(cache.Format(style)) == L"Segoe UI");
    style.display = true;
    CHECK(FamilyOf(cache.Format(style)) == L"Segoe UI");
    // A non-Variable family has no optical Small instance: small text keeps the text family.
    style.display = false;
    style.opticalSmall = true;
    CHECK(FamilyOf(cache.Format(style)) == L"Segoe UI");
}

AETHER_TEST("text cache: optical small text uses Segoe UI Variable Small when it is installed") {
    const auto factory = DWrite();
    REQUIRE(factory);
    TextCache cache;
    cache.Initialize(factory.get(), L"Segoe UI Variable Text", L"Segoe UI Variable Display");
    TextStyle style;
    style.size = 12.0f;
    const auto* regular = cache.Format(style);
    REQUIRE(regular != nullptr);
    CHECK(cache.Format(style) == regular);  // cached
    style.opticalSmall = true;
    const auto* small = cache.Format(style);
    REQUIRE(small != nullptr);
    CHECK(small != regular);
    const bool variable = FamilyInstalled(factory.get(), L"Segoe UI Variable Small");
    TEST_NOTE(std::string("Segoe UI Variable Small installed: ") + (variable ? "yes" : "no"));
    CHECK(FamilyOf(small) == (variable ? std::wstring(L"Segoe UI Variable Small") : FamilyOf(regular)));
    // The display family ignores the small flag.
    style.display = true;
    CHECK(FamilyOf(cache.Format(style)) == FamilyOf(cache.Format(TextStyle{.size = 12.0f, .display = true})));
}

AETHER_TEST("text cache: tabular figures give every digit string of one length the same width") {
    const auto factory = DWrite();
    REQUIRE(factory);
    TextCache cache;
    cache.Initialize(factory.get(), L"Segoe UI Variable Text", L"Segoe UI Variable Display");
    TextStyle style;
    style.size = 13.0f;
    style.ellipsis = false;
    style.tabularFigures = true;
    const float ones = cache.Measure(L"11:11", style).width;
    const float eights = cache.Measure(L"88:88", style).width;
    const float mixed = cache.Measure(L"10:47", style).width;
    CHECK(ones > 0.0f);
    CHECK_NEAR(ones, eights, 0.01);
    CHECK_NEAR(mixed, eights, 0.01);
    // The line height does not change with the feature.
    style.tabularFigures = false;
    CHECK_NEAR(cache.Measure(L"11:11", style).height, cache.Measure(L"11:11", TextStyle{.size = 13.0f, .ellipsis = false,
                                                                                          .tabularFigures = true}).height,
               0.01);
    // Empty text and an uninitialized cache are harmless.
    CHECK_EQ(cache.Measure(L"", style).width, 0.0f);
    TextCache empty;
    CHECK(empty.Format(style) == nullptr);
    CHECK_EQ(empty.Measure(L"12", style).width, 0.0f);
}
