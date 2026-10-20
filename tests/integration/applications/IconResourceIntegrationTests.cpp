// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Extracts a synthetic .ico written to a temporary folder through ApplicationInfo::iconResource, the
// route used for relaunch icons that windows declare (for example a browser profile's avatar). No
// real profile, browser or user file is read.
#include "AetherTest.h"

#include "applications/ShellIconProvider.h"
#include "platform/windows/ComApartment.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

constexpr std::uint8_t kBlue = 0x20, kGreen = 0x80, kRed = 0xE0;
constexpr int kSide = 32;

void Put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void Put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
}

// A 32x32, 32-bpp icon: the visual top half opaque, the bottom half 50 % transparent, one colour,
// straight (not premultiplied) alpha as the .ico format stores it.
std::vector<std::uint8_t> SyntheticIcon() {
    const std::uint32_t xorBytes = kSide * kSide * 4;
    const std::uint32_t andBytes = kSide * 4;  // 1 bpp rows padded to 32 bits
    const std::uint32_t imageBytes = 40 + xorBytes + andBytes;
    std::vector<std::uint8_t> file;
    Put16(file, 0);  // ICONDIR
    Put16(file, 1);
    Put16(file, 1);
    file.push_back(kSide);  // ICONDIRENTRY
    file.push_back(kSide);
    file.push_back(0);
    file.push_back(0);
    Put16(file, 1);
    Put16(file, 32);
    Put32(file, imageBytes);
    Put32(file, 6 + 16);
    Put32(file, 40);  // BITMAPINFOHEADER
    Put32(file, kSide);
    Put32(file, kSide * 2);  // colour and mask heights
    Put16(file, 1);
    Put16(file, 32);
    for (int i = 0; i < 6; ++i) Put32(file, 0);
    for (int row = 0; row < kSide; ++row) {  // bottom-up: the first rows are the visual bottom
        const std::uint8_t alpha = row < kSide / 2 ? 0x80 : 0xFF;
        for (int column = 0; column < kSide; ++column) {
            file.push_back(kBlue);
            file.push_back(kGreen);
            file.push_back(kRed);
            file.push_back(alpha);
        }
    }
    file.insert(file.end(), andBytes, 0);
    return file;
}

const std::uint8_t* PixelAt(const IconImage& image, double x, double y) {
    const auto column = static_cast<std::size_t>(x * image.width);
    const auto row = static_cast<std::size_t>(y * image.height);
    return &image.pixels[(row * image.width + column) * 4];
}

}  // namespace

AETHER_TEST("icon resource: a declared local .ico is extracted with premultiplied alpha") {
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    const auto root = test::MakeTempDirectory("apps-iconresource");
    const auto icon = root / L"Fixture Avatar.ico";
    {
        const auto bytes = SyntheticIcon();
        std::ofstream file(icon, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    ApplicationInfo app;
    app.id = L"aumid:aether.fixture.iconresource";
    app.displayName = L"Fixture Icon Resource";
    app.kind = ApplicationKind::Other;
    app.iconResource = icon.native() + L",0";
    const auto extracted = ShellIconProvider::ExtractIcon(app, kSide);
    REQUIRE(extracted.ok());
    const auto& image = extracted.value().image;
    TEST_NOTE("extracted " + std::to_string(image.width) + "x" + std::to_string(image.height));
    REQUIRE(image.width >= static_cast<std::uint32_t>(kSide));
    REQUIRE(image.height >= static_cast<std::uint32_t>(kSide));
    CHECK(IsPremultipliedBgra(image.pixels));

    // Measured on Windows 11 build 26300: the shell hands the icon back as straight alpha with small
    // value shifts (alpha 254 instead of 255, colour channels within a few units), so the comparison
    // allows a tolerance. The distinct colour and the two alpha levels prove the file's own image was
    // used, and premultiplied.
    constexpr int kTolerance = 6;
    const auto within = [](int actual, int expected) { return std::abs(actual - expected) <= kTolerance; };
    const auto* opaque = PixelAt(image, 0.5, 0.25);
    TEST_NOTE("opaque sample " + std::to_string(opaque[0]) + " " + std::to_string(opaque[1]) + " " +
              std::to_string(opaque[2]) + " " + std::to_string(opaque[3]));
    CHECK(within(opaque[3], 0xFF));
    CHECK(within(opaque[0], kBlue));
    CHECK(within(opaque[1], kGreen));
    CHECK(within(opaque[2], kRed));

    const auto* translucent = PixelAt(image, 0.5, 0.75);
    TEST_NOTE("translucent sample " + std::to_string(translucent[0]) + " " + std::to_string(translucent[1]) + " " +
              std::to_string(translucent[2]) + " " + std::to_string(translucent[3]));
    CHECK(within(translucent[3], 0x80));
    CHECK(within(translucent[0], kBlue * 0x80 / 0xFF));
    CHECK(within(translucent[1], kGreen * 0x80 / 0xFF));
    CHECK(within(translucent[2], kRed * 0x80 / 0xFF));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

AETHER_TEST("icon resource: unusable declared icons fall back to the normal chain") {
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    const auto root = test::MakeTempDirectory("apps-iconresource-missing");
    std::filesystem::create_directory(root / L"Folder.ico");

    ApplicationInfo app;
    app.id = L"shell:aether-fixture-no-source";
    app.displayName = L"Fixture Without Source";
    app.kind = ApplicationKind::Other;
    for (const auto& resource : {(root / L"Missing.ico").native() + L",0", (root / L"Folder.ico").native() + L",0",
                                 (root / L"Missing.ico").native() + L",1", std::wstring(L"@fixture,-1")}) {
        app.iconResource = resource;
        const auto extracted = ShellIconProvider::ExtractIcon(app, kSide);
        REQUIRE_FALSE(extracted.ok());
        CHECK_EQ(extracted.error().code, ErrorCode::NotFound);  // nothing else to show: the caller's fallback icon
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
