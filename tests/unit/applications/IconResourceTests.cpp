// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Pure parsing of relaunch icon resources and the window-property sanitizers. Synthetic paths only;
// nothing is read from the disk or from another window.
#include "AetherTest.h"

#include "applications/IconResource.h"
#include "applications/WindowProperties.h"

#include <string>

using namespace aether;
using namespace aether::apps;

AETHER_TEST("icon resource: the local .ico form with index 0 is accepted and normalized") {
    const auto parsed = ParseIconResource(L"C:\\Fixture\\Profile\\Avatar.ico,0");
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().path, std::wstring(L"C:\\Fixture\\Profile\\Avatar.ico"));
    CHECK_EQ(parsed.value().index, 0);

    const auto spaced = ParseIconResource(L"  c:/Fixture/Profile/../Profile/Avatar.ICO , 0  ");
    REQUIRE(spaced.ok());
    CHECK_EQ(spaced.value().path, std::wstring(L"C:\\Fixture\\Profile\\Avatar.ICO"));

    // The last comma separates the index, so commas inside the path are allowed.
    const auto comma = ParseIconResource(L"C:\\Fixture\\A, B\\Avatar.ico,0");
    REQUIRE(comma.ok());
    CHECK_EQ(comma.value().path, std::wstring(L"C:\\Fixture\\A, B\\Avatar.ico"));
}

AETHER_TEST("icon resource: index forms other than 0 are rejected") {
    for (const wchar_t* value : {L"C:\\Fixture\\Avatar.ico", L"C:\\Fixture\\Avatar.ico,", L"C:\\Fixture\\Avatar.ico,a",
                                 L"C:\\Fixture\\Avatar.ico,-1", L"C:\\Fixture\\Avatar.ico,1", L"C:\\Fixture\\Avatar.ico,0x0",
                                 L"C:\\Fixture\\Avatar.ico,0 0", L"C:\\Fixture\\Avatar.ico,0000001"}) {
        const auto parsed = ParseIconResource(value);
        CHECK_FALSE(parsed.ok());
        if (!parsed.ok()) CHECK_EQ(parsed.error().code, ErrorCode::InvalidArgument);
    }
}

AETHER_TEST("icon resource: network, device, relative and stream paths are rejected") {
    for (const wchar_t* value : {L"\\\\server\\share\\Avatar.ico,0", L"\\\\?\\GLOBALROOT\\Device\\Fixture\\Avatar.ico,0",
                                 L"\\\\.\\C:\\Fixture\\Avatar.ico,0", L"\\??\\C:\\Fixture\\Avatar.ico,0",
                                 L"Fixture\\Avatar.ico,0", L"C:Avatar.ico,0", L"\\Fixture\\Avatar.ico,0",
                                 L"C:\\Fixture\\Avatar.ico:stream,0", L"C:\\Fixture\\Avatar.txt:hidden.ico,0",
                                 L"C:\\Fixture\\*.ico,0"}) {
        CHECK_FALSE(ParseIconResource(value).ok());
    }
}

AETHER_TEST("icon resource: quoted, indirect, executable and malformed values are rejected") {
    for (const wchar_t* value : {L"\"C:\\Fixture\\Avatar.ico\",0", L"@C:\\Fixture\\Resources.dll,-101",
                                 L"ms-resource:Fixture/Avatar", L"MS-RESOURCE://Fixture/Avatar,0",
                                 L"C:\\Fixture\\Browser.exe,0", L"C:\\Fixture\\Icons.dll,0", L"C:\\Fixture\\Avatar.png,0",
                                 L"C:\\Fixture\\Ava\ttar.ico,0", L"C:\\Fixture\\Avatar.ico,0\n", L"", L"   ", L",0"}) {
        CHECK_FALSE(ParseIconResource(value).ok());
    }
    std::wstring longValue = L"C:\\" + std::wstring(kMaxIconResourceLength, L'a') + L".ico,0";
    CHECK_FALSE(ParseIconResource(longValue).ok());
    // Messages never echo the value: it can name a user-profile folder.
    const auto rejected = ParseIconResource(L"C:\\PrivateMarker\\Avatar.exe,0");
    REQUIRE_FALSE(rejected.ok());
    CHECK(rejected.error().message.find("PrivateMarker") == std::string::npos);
}

AETHER_TEST("window properties: relaunch display names are kept only as plain text") {
    CHECK_EQ(SanitizeRelaunchDisplayName(L"  Fixture Profile  "), std::wstring(L"Fixture Profile"));
    CHECK(SanitizeRelaunchDisplayName(L"@C:\\Fixture\\Resources.dll,-101").empty());
    CHECK(SanitizeRelaunchDisplayName(L"Line\nBreak").empty());
    CHECK(SanitizeRelaunchDisplayName(L"Tab\tName").empty());
    CHECK(SanitizeRelaunchDisplayName(L"").empty());
    CHECK(SanitizeRelaunchDisplayName(L"   ").empty());
    const std::wstring longest(kMaxRelaunchDisplayNameLength, L'n');
    CHECK_EQ(SanitizeRelaunchDisplayName(longest), longest);
    CHECK(SanitizeRelaunchDisplayName(longest + L"n").empty());
}

AETHER_TEST("window properties: relaunch icon resources are kept raw within bounds") {
    CHECK_EQ(SanitizeRelaunchIconResource(L" C:\\Fixture\\Avatar.ico,0 "), std::wstring(L"C:\\Fixture\\Avatar.ico,0"));
    // Kept raw: only icon extraction decides whether the form is supported.
    CHECK_EQ(SanitizeRelaunchIconResource(L"C:\\Fixture\\Browser.exe,-3"), std::wstring(L"C:\\Fixture\\Browser.exe,-3"));
    CHECK(SanitizeRelaunchIconResource(L"C:\\Fixture\\Ava\rtar.ico,0").empty());
    CHECK(SanitizeRelaunchIconResource(std::wstring(kMaxIconResourceLength + 1, L'a')).empty());
    CHECK(SanitizeRelaunchIconResource(L"").empty());
}

AETHER_TEST("window properties: a null or destroyed window is rejected without touching the shell") {
    const auto missing = ReadWindowShellProperties(nullptr);
    REQUIRE_FALSE(missing.ok());
    CHECK_EQ(missing.error().code, ErrorCode::InvalidArgument);
}
