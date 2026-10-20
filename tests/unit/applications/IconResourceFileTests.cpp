// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Worker file policy on owned temporary fixtures only; no shell, browser or profile is read.
#include "AetherTest.h"

#include "applications/IconResource.h"
#include "platform/windows/Handles.h"

#include <windows.h>
#include <winioctl.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

class Fixture {
public:
    Fixture() : root(test::MakeTempDirectory("apps-icon-file-policy")) {}
    ~Fixture() {
        // Remove the junction itself before recursively removing the fixture's ordinary directories.
        (void)::RemoveDirectoryW((root / L"Linked").c_str());
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::filesystem::path root;
};

Result<void> CreateJunction(const std::filesystem::path& junction, const std::filesystem::path& target) {
    // A directory junction needs no symlink privilege. Both paths belong to this fixture; the target
    // contains only a synthetic marker file, so even an erroneous traversal cannot read user data.
    if (!::CreateDirectoryW(junction.c_str(), nullptr)) {
        return MakeError(ErrorCode::Platform, "fixture junction directory");
    }
    platform::UniqueFileHandle handle(::CreateFileW(junction.c_str(), GENERIC_WRITE,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                                nullptr));
    if (!handle) return MakeError(ErrorCode::Platform, "fixture junction handle");

    struct Header {
        DWORD tag;
        WORD dataLength;
        WORD reserved;
        WORD substituteOffset;
        WORD substituteLength;
        WORD printOffset;
        WORD printLength;
    };
    static_assert(sizeof(Header) == 16);
    const auto substitute = L"\\??\\" + target.native();
    const auto print = target.native();
    const auto substituteBytes = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    const auto printBytes = static_cast<WORD>(print.size() * sizeof(wchar_t));
    const auto printOffset = static_cast<WORD>(substituteBytes + sizeof(wchar_t));
    const auto dataBytes = static_cast<WORD>(8 + printOffset + printBytes + sizeof(wchar_t));
    const Header header{IO_REPARSE_TAG_MOUNT_POINT, dataBytes, 0, 0, substituteBytes, printOffset, printBytes};
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(dataBytes) + 8, 0);
    std::memcpy(buffer.data(), &header, sizeof(header));
    std::memcpy(buffer.data() + sizeof(header), substitute.data(), substituteBytes);
    std::memcpy(buffer.data() + sizeof(header) + printOffset, print.data(), printBytes);
    DWORD returned = 0;
    if (!::DeviceIoControl(handle.Get(), FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()),
                           nullptr, 0, &returned, nullptr)) {
        return MakeError(ErrorCode::Platform, "fixture junction definition");
    }
    return Ok();
}

}  // namespace

AETHER_TEST("icon file policy: ordinary owned files pass and missing or directory leaves fail") {
    Fixture fixture;
    const auto folder = fixture.root / L"Icons";
    std::filesystem::create_directory(folder);
    const auto icon = folder / L"Fixture Avatar.ico";
    { std::ofstream file(icon, std::ios::binary); file << "synthetic fixture, not decoded"; }
    CHECK(ValidateIconResourceFile(IconResource{icon.native(), 0}).ok());

    const auto missing = ValidateIconResourceFile(IconResource{(folder / L"Missing.ico").native(), 0});
    REQUIRE_FALSE(missing.ok());
    CHECK_EQ(missing.error().code, ErrorCode::NotFound);
    std::filesystem::create_directory(folder / L"Directory.ico");
    CHECK_FALSE(ValidateIconResourceFile(IconResource{(folder / L"Directory.ico").native(), 0}).ok());
    CHECK_FALSE(ValidateIconResourceFile(IconResource{icon.native(), 1}).ok());
    CHECK_FALSE(ValidateIconResourceFile(IconResource{L"relative.ico", 0}).ok());
}

AETHER_TEST("icon file policy: an ancestor junction is rejected before following it to an ordinary leaf") {
    Fixture fixture;
    const auto target = fixture.root / L"Owned Target";
    std::filesystem::create_directory(target);
    const auto icon = target / L"Fixture Avatar.ico";
    { std::ofstream file(icon, std::ios::binary); file << "synthetic fixture, not decoded"; }
    REQUIRE(ValidateIconResourceFile(IconResource{icon.native(), 0}).ok());
    const auto junction = fixture.root / L"Linked";
    REQUIRE(CreateJunction(junction, target).ok());

    const auto rejected = ValidateIconResourceFile(IconResource{(junction / icon.filename()).native(), 0});
    REQUIRE_FALSE(rejected.ok());
    CHECK_EQ(rejected.error().code, ErrorCode::Unsupported);
    // All failure output stays generic; the owned temporary path is never echoed.
    CHECK(rejected.error().message.find("Fixture Avatar") == std::string::npos);
}
