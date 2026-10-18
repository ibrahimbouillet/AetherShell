// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "UpdateTestSupport.h"

#include "core/Json.h"
#include "updates/AssetNaming.h"
#include "updates/Sha256.h"
#include "updates/UpdateManifest.h"

#include <functional>
#include <string>

using namespace aether;
using namespace aether::updates;
using namespace aether::updates::testing;

namespace {

ManifestExpectations Expect(std::string_view version, bool prerelease = false) {
    auto parsed = SemanticVersion::Parse(version);
    if (!parsed) throw test::RequireFailure{};
    return ManifestExpectations{std::move(parsed).value(), prerelease, kTestRepository};
}

ManifestExpectations StableExpectations() { return Expect("1.2.0"); }

// The 1.2.0 fixture manifest as a JSON document, edited by `mutate`, serialized again. The result is
// unsigned: these tests exercise UpdateManifest::Parse, which runs after signature verification.
std::string Edited(const std::function<void(json::Value& root, json::Value& asset)>& mutate) {
    auto root = json::Parse(fixtures::kStable_1_2_0.manifest);
    if (!root) throw test::RequireFailure{};
    auto& asset = (*root->Find("assets")->AsArray())[0];
    mutate(*root, asset);
    return json::Serialize(*root);
}

ErrorCode ParseCode(std::string_view bytes, const ManifestExpectations& expected = StableExpectations()) {
    auto parsed = UpdateManifest::Parse(bytes, expected);
    return parsed ? ErrorCode::Unknown : parsed.error().code;
}

std::string Digest(const fixtures::FixtureAsset& asset) {
    auto digest = Sha256::Hash(TestAssetBytes(asset.size, asset.seed));
    return digest ? ToHex(*digest) : std::string();
}

}  // namespace

// --------------------------------------------------------------------------------- asset naming

AETHER_TEST("asset naming: follows the release convention") {
    const auto version = SemanticVersion::Parse("1.2.0-rc.1+build.5").value();
    CHECK_EQ(InstallerAssetName(version, Architecture::X64), std::string("AetherShell-1.2.0-rc.1+build.5-win-x64-setup.exe"));
    CHECK_EQ(InstallerAssetName(version, Architecture::Arm64),
             std::string("AetherShell-1.2.0-rc.1+build.5-win-arm64-setup.exe"));
    CHECK_EQ(ManifestAssetName(version), std::string("AetherShell-1.2.0-rc.1+build.5-manifest.json"));
    CHECK_EQ(SignatureAssetName(version), std::string("AetherShell-1.2.0-rc.1+build.5-manifest.json.sig"));
    CHECK_EQ(ChecksumsAssetName(version), std::string("AetherShell-1.2.0-rc.1+build.5-checksums.txt"));

    const auto plain = SemanticVersion::Parse("1.2.0").value();
    CHECK(MatchInstallerAssetName("AetherShell-1.2.0-win-arm64-setup.exe", plain) == Architecture::Arm64);
    CHECK(MatchInstallerAssetName("AetherShell-1.2.0-win-x64-setup.exe", plain) == Architecture::X64);
    CHECK_FALSE(MatchInstallerAssetName("AetherShell-1.2.0-win-x86-setup.exe", plain).has_value());
    CHECK_FALSE(MatchInstallerAssetName("AetherShell-1.2.1-win-x64-setup.exe", plain).has_value());
    CHECK_FALSE(MatchInstallerAssetName("aethershell-1.2.0-win-x64-setup.exe", plain).has_value());
}

AETHER_TEST("asset naming: only plain portable file names are safe") {
    for (const std::string_view good : {"AetherShell-1.2.0-win-x64-setup.exe", "a", "a.b", "A_b-c+d.txt"}) {
        CHECK(IsSafeAssetFileName(good));
    }
    for (const std::string_view bad :
         {"", ".", "..", ".hidden", "trailing.", "a..b", "..\\evil.exe", "../evil.exe", "dir/file.exe",
          "dir\\file.exe", "C:file.exe", "file.exe:stream", "file name.exe", "CON", "con.txt", "Nul", "COM1.log",
          "lpt9", "file\x01.exe", "caf\xC3\xA9.exe", "a*b", "a?b", "a\"b", "a<b", "a|b"}) {
        CHECK_FALSE(IsSafeAssetFileName(bad));
    }
    CHECK(IsSafeAssetFileName("COM10"));  // only COM0-COM9 are devices
    CHECK_FALSE(IsSafeAssetFileName(std::string(kMaxAssetFileNameLength + 1, 'a')));
    CHECK(IsSafeAssetFileName(std::string(kMaxAssetFileNameLength, 'a')));
}

// --------------------------------------------------------------------------------------- parsing

AETHER_TEST("manifest: the signed fixtures parse and describe the real payloads") {
    auto manifest = UpdateManifest::Parse(fixtures::kStable_1_2_0.manifest, StableExpectations());
    REQUIRE(manifest.ok());
    CHECK_EQ(manifest->schemaVersion, 1);
    CHECK_EQ(manifest->product, std::string("AetherShell"));
    CHECK_EQ(manifest->version.ToString(), std::string("1.2.0"));
    CHECK(manifest->channel == UpdateChannel::Stable);
    CHECK_EQ(manifest->minimumWindowsBuild, 19041u);
    CHECK_EQ(manifest->releaseNotesUrl, std::string("https://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0"));
    REQUIRE_EQ(manifest->assets.size(), 1u);
    const auto& asset = manifest->assets[0];
    CHECK_EQ(asset.fileName, std::string("AetherShell-1.2.0-win-x64-setup.exe"));
    CHECK(asset.architecture == Architecture::X64);
    CHECK_EQ(asset.format, std::string("exe"));
    CHECK_EQ(asset.size, fixtures::kStable_1_2_0.assets[0].size);
    CHECK_EQ(ToHex(asset.sha256), Digest(fixtures::kStable_1_2_0.assets[0]));

    auto prerelease =
        UpdateManifest::Parse(fixtures::kPrerelease_2_0_0_beta_2.manifest, Expect("2.0.0-beta.2", true));
    REQUIRE(prerelease.ok());
    CHECK(prerelease->channel == UpdateChannel::Prerelease);

    auto multi = UpdateManifest::Parse(fixtures::kStable_1_3_0_MultiArch.manifest, Expect("1.3.0"));
    REQUIRE(multi.ok());
    REQUIRE_EQ(multi->assets.size(), 2u);
    for (const auto& fixtureAsset : fixtures::kStable_1_3_0_MultiArch.assets) {
        const auto architecture = ParseArchitecture(fixtureAsset.architecture).value();
        auto selected = multi->SelectAsset(architecture, 22631);
        REQUIRE(selected.ok());
        CHECK_EQ(selected->fileName, std::string(fixtureAsset.fileName));
        CHECK_EQ(ToHex(selected->sha256), Digest(fixtureAsset));
    }
}

AETHER_TEST("manifest: the version must equal the release tag version") {
    CHECK_EQ(ParseCode(fixtures::kStable_1_2_0.manifest, Expect("1.2.1")), ErrorCode::Validation);
    CHECK_EQ(ParseCode(fixtures::kStable_1_2_0.manifest, Expect("1.1.0")), ErrorCode::Validation);
    // Build metadata is part of the identity of a release even though it does not affect precedence.
    CHECK_EQ(ParseCode(fixtures::kStable_1_2_0.manifest, Expect("1.2.0+hotfix")), ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("version", "v1.2.0"); })),
             ErrorCode::Validation);
}

AETHER_TEST("manifest: the channel must agree with the release's prerelease flag") {
    // A stable manifest attached to a release marked as prerelease, and vice versa.
    CHECK_EQ(ParseCode(fixtures::kStable_1_2_0.manifest, Expect("1.2.0", true)), ErrorCode::Validation);
    CHECK_EQ(ParseCode(fixtures::kPrerelease_2_0_0_beta_2.manifest, Expect("2.0.0-beta.2", false)),
             ErrorCode::Validation);
    // A prerelease version can never be offered as stable, even when release and manifest agree.
    auto stableBeta = json::Parse(fixtures::kPrerelease_2_0_0_beta_2.manifest).value();
    stableBeta.Set("channel", "stable");
    CHECK_EQ(ParseCode(json::Serialize(stableBeta), Expect("2.0.0-beta.2", false)), ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("channel", "beta"); })),
             ErrorCode::Validation);
}

AETHER_TEST("manifest: placeholder or malformed digests are rejected") {
    for (const std::string& digest :
         {std::string(64, '0'), std::string("<release-generated-digest>"), std::string(64, 'f'),
          std::string("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"),
          std::string("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
          std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),  // empty file
          std::string("29E1DF1DF260628E2863A0F69BCA2BD2D94C6D2F81F45888D069CCC541A378BD"),  // uppercase
          std::string(63, 'a'), std::string("TODO"), std::string()}) {
        CHECK_EQ(ParseCode(Edited([&](json::Value&, json::Value& asset) { asset.Set("sha256", digest); })),
                 ErrorCode::Validation);
    }
    CHECK_EQ(ParseCode(Edited([](json::Value&, json::Value& asset) { asset.Set("sha256", 12345); })),
             ErrorCode::Parse);
}

AETHER_TEST("manifest: placeholder detection is precise") {
    CHECK(IsPlaceholderDigest(std::string(64, '0')));
    CHECK(IsPlaceholderDigest("abababababababababababababababababababababababababababababababab"));
    CHECK(IsPlaceholderDigest("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    CHECK(IsPlaceholderDigest("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_FALSE(IsPlaceholderDigest("29e1df1df260628e2863a0f69bca2bd2d94c6d2f81f45888d069ccc541a378bd"));
    CHECK_FALSE(IsPlaceholderDigest("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

AETHER_TEST("manifest: sizes must be positive and at most 512 MiB") {
    for (const json::Value& size : {json::Value(0), json::Value(-1), json::Value(std::int64_t{512} * 1024 * 1024 + 1),
                                    json::Value(std::int64_t{1} << 40)}) {
        CHECK_EQ(ParseCode(Edited([&](json::Value&, json::Value& asset) { asset.Set("size", size); })),
                 ErrorCode::Validation);
    }
    CHECK_EQ(ParseCode(Edited([](json::Value&, json::Value& asset) { asset.Set("size", 1.5); })), ErrorCode::Parse);
    CHECK_EQ(ParseCode(Edited([](json::Value&, json::Value& asset) { asset.Set("size", "300000"); })),
             ErrorCode::Parse);
    auto maximum = UpdateManifest::Parse(
        Edited([](json::Value&, json::Value& asset) { asset.Set("size", std::int64_t{512} * 1024 * 1024); }),
        StableExpectations());
    CHECK(maximum.ok());
}

AETHER_TEST("manifest: file names must follow the convention and contain no path") {
    for (const std::string_view name :
         {"..\\AetherShell-1.2.0-win-x64-setup.exe", "../AetherShell-1.2.0-win-x64-setup.exe",
          "sub/AetherShell-1.2.0-win-x64-setup.exe", "C:\\AetherShell-1.2.0-win-x64-setup.exe",
          "AetherShell-1.2.0-win-x64-setup.exe:payload", "AetherShell-1.2.1-win-x64-setup.exe",
          "AetherShell-1.2.0-win-arm64-setup.exe", "AetherShell-1.2.0-win-x64-setup.msi", "setup.exe", "",
          "AetherShell-1.2.0-win-x64-setup.exe."}) {
        CHECK_EQ(ParseCode(Edited([&](json::Value&, json::Value& asset) { asset.Set("filename", name); })),
                 ErrorCode::Validation);
    }
}

AETHER_TEST("manifest: architectures, formats and duplicates are validated") {
    CHECK_EQ(ParseCode(Edited([](json::Value&, json::Value& asset) { asset.Set("architecture", "x86"); })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value&, json::Value& asset) { asset.Set("architecture", "X64"); })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value&, json::Value& asset) { asset.Set("format", "msi"); })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value& asset) {
                 const auto copy = asset;
                 root.Find("assets")->Append(copy);
             })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("assets", json::Value::MakeArray()); })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("assets", "none"); })), ErrorCode::Parse);
}

AETHER_TEST("manifest: the document structure is strict") {
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("product", "AetherShellPro"); })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("schemaVersion", 2); })),
             ErrorCode::Unsupported);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("signature", "inline"); })),
             ErrorCode::Parse);
    CHECK_EQ(ParseCode(Edited([](json::Value&, json::Value& asset) { asset.Set("url", "https://example.com/x"); })),
             ErrorCode::Parse);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("minimumWindowsBuild", 0); })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) { root.Set("minimumWindowsBuild", "19041"); })),
             ErrorCode::Parse);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) {
                 root.Set("releaseNotesUrl", "https://evil.example/ibrahimbouillet/AetherShell/releases");
             })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) {
                 root.Set("releaseNotesUrl", "http://github.com/ibrahimbouillet/AetherShell/releases/tag/v1.2.0");
             })),
             ErrorCode::Validation);
    CHECK_EQ(ParseCode(Edited([](json::Value& root, json::Value&) {
                 root.Set("releaseNotesUrl", "https://github.com/someone-else/AetherShell/releases/tag/v1.2.0");
             })),
             ErrorCode::Validation);

    // Missing members.
    for (const std::string_view key :
         {"schemaVersion", "product", "version", "channel", "minimumWindowsBuild", "releaseNotesUrl", "assets"}) {
        auto root = json::Parse(fixtures::kStable_1_2_0.manifest).value();
        std::erase_if(*root.AsObject(), [&](const json::Member& member) { return member.key == key; });
        const auto code = ParseCode(json::Serialize(root));
        CHECK(code == ErrorCode::Parse);
    }

    CHECK_EQ(ParseCode("not json"), ErrorCode::Parse);
    CHECK_EQ(ParseCode("[]"), ErrorCode::Parse);
    CHECK_EQ(ParseCode(R"({"schemaVersion":1,"schemaVersion":1})"), ErrorCode::Parse);  // duplicate keys
    CHECK_EQ(ParseCode(std::string(kMaxManifestBytes + 1, ' ')), ErrorCode::Validation);
}

// ------------------------------------------------------------------------------- compatibility

AETHER_TEST("manifest: asset selection checks the architecture and the Windows build") {
    auto single = UpdateManifest::Parse(fixtures::kStable_1_2_0.manifest, StableExpectations());
    REQUIRE(single.ok());
    auto x64 = single->SelectAsset(Architecture::X64, 19041);
    CHECK(x64.ok());
    auto arm64 = single->SelectAsset(Architecture::Arm64, 26100);
    REQUIRE_FALSE(arm64.ok());
    CHECK_EQ(arm64.error().code, ErrorCode::Unsupported);
    auto oldWindows = single->SelectAsset(Architecture::X64, 19040);
    REQUIRE_FALSE(oldWindows.ok());
    CHECK_EQ(oldWindows.error().code, ErrorCode::Unsupported);

    auto multi = UpdateManifest::Parse(fixtures::kStable_1_3_0_MultiArch.manifest, Expect("1.3.0"));
    REQUIRE(multi.ok());
    CHECK_EQ(multi->minimumWindowsBuild, 22000u);
    auto windows10 = multi->SelectAsset(Architecture::X64, 19045);  // Windows 10 22H2
    REQUIRE_FALSE(windows10.ok());
    CHECK_EQ(windows10.error().code, ErrorCode::Unsupported);
    CHECK(multi->SelectAsset(Architecture::X64, 22000).ok());
    CHECK(multi->SelectAsset(Architecture::Arm64, 26100).ok());
}
