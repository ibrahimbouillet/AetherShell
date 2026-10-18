// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/UpdateManifest.h"

#include "core/Json.h"
#include "updates/AssetNaming.h"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <limits>

namespace aether::updates {
namespace {

// SHA-256("") - a release asset is never empty.
constexpr std::string_view kEmptyInputDigest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

Error Invalid(std::string message) { return MakeError(ErrorCode::Validation, "invalid manifest: " + std::move(message)); }
Error Malformed(std::string message) { return MakeError(ErrorCode::Parse, "malformed manifest: " + std::move(message)); }

Result<void> RejectUnknownMembers(const json::Value& object, std::initializer_list<std::string_view> allowed,
                                  std::string_view where) {
    for (const auto& member : *object.AsObject()) {
        if (std::find(allowed.begin(), allowed.end(), member.key) == allowed.end()) {
            return Malformed(std::format("unknown member in {} (schema version 1 allows no extensions)", where));
        }
    }
    return Ok();
}

Result<std::string> RequireString(const json::Value& object, std::string_view key, std::string_view where) {
    const auto* value = object.Find(key);
    if (value == nullptr || !value->IsString()) return Malformed(std::format("{}.{} must be a string", where, key));
    return *value->AsString();
}

Result<std::int64_t> RequireInteger(const json::Value& object, std::string_view key, std::string_view where) {
    const auto* value = object.Find(key);
    const auto integer = value != nullptr ? value->AsInt64() : std::nullopt;
    if (!integer) return Malformed(std::format("{}.{} must be an integer", where, key));
    return *integer;
}

Result<ManifestAsset> ParseAsset(const json::Value& entry, const SemanticVersion& version) {
    if (!entry.IsObject()) return Malformed("every asset must be an object");
    if (auto known = RejectUnknownMembers(entry, {"filename", "architecture", "format", "sha256", "size"}, "asset");
        !known) {
        return known.error();
    }
    ManifestAsset asset;

    auto fileName = RequireString(entry, "filename", "asset");
    if (!fileName) return fileName.error();
    if (!IsSafeAssetFileName(*fileName)) return Invalid("an asset file name is not a plain file name");
    asset.fileName = std::move(fileName).value();

    auto architectureText = RequireString(entry, "architecture", "asset");
    if (!architectureText) return architectureText.error();
    const auto architecture = ParseArchitecture(*architectureText);
    if (!architecture) return Invalid("unknown architecture (expected x64 or arm64)");
    asset.architecture = *architecture;
    if (asset.fileName != InstallerAssetName(version, asset.architecture)) {
        return Invalid(std::format("asset file name does not follow the convention '{}'",
                                   InstallerAssetName(version, asset.architecture)));
    }

    auto format = RequireString(entry, "format", "asset");
    if (!format) return format.error();
    if (*format != kInstallerFormat) return Invalid("unsupported asset format (expected exe)");
    asset.format = std::move(format).value();

    auto digestText = RequireString(entry, "sha256", "asset");
    if (!digestText) return digestText.error();
    auto digest = ParseSha256Hex(*digestText);
    if (!digest) return Invalid("sha256 must be exactly 64 lowercase hex characters");
    if (IsPlaceholderDigest(*digestText)) return Invalid("sha256 is a placeholder, not a real digest");
    asset.sha256 = *digest;

    auto size = RequireInteger(entry, "size", "asset");
    if (!size) return size.error();
    if (*size <= 0 || static_cast<std::uint64_t>(*size) > kMaxInstallerBytes) {
        return Invalid(std::format("asset size must be between 1 and {} bytes", kMaxInstallerBytes));
    }
    asset.size = static_cast<std::uint64_t>(*size);
    return asset;
}

}  // namespace

bool IsPlaceholderDigest(std::string_view hex) noexcept {
    if (hex == kEmptyInputDigest) return true;
    // Periodic with a period of at most 16 characters ("000...", "abab...", "deadbeef...",
    // "0123456789abcdef..."). A real SHA-256 output has this property with probability ~2^-190.
    for (std::size_t period = 1; period <= 16; ++period) {
        bool periodic = true;
        for (std::size_t i = period; i < hex.size() && periodic; ++i) periodic = hex[i] == hex[i - period];
        if (periodic) return true;
    }
    return false;
}

Result<UpdateManifest> UpdateManifest::Parse(std::string_view bytes, const ManifestExpectations& expected) {
    if (bytes.size() > kMaxManifestBytes) return Invalid("the manifest is larger than 64 KiB");
    auto document = json::Parse(bytes, json::ParseOptions{kMaxManifestBytes, 8});
    if (!document) return Malformed(document.error().message);
    const auto& root = *document;
    if (!root.IsObject()) return Malformed("the root must be an object");
    if (auto known = RejectUnknownMembers(root,
                                          {"schemaVersion", "product", "version", "channel", "minimumWindowsBuild",
                                           "releaseNotesUrl", "assets"},
                                          "manifest");
        !known) {
        return known.error();
    }

    UpdateManifest manifest;
    auto schema = RequireInteger(root, "schemaVersion", "manifest");
    if (!schema) return schema.error();
    if (*schema != kManifestSchemaVersion) {
        return MakeError(ErrorCode::Unsupported, std::format("unsupported manifest schema version {}", *schema));
    }
    manifest.schemaVersion = kManifestSchemaVersion;

    auto product = RequireString(root, "product", "manifest");
    if (!product) return product.error();
    if (*product != kManifestProduct) return Invalid("the product is not AetherShell");
    manifest.product = std::move(product).value();

    auto versionText = RequireString(root, "version", "manifest");
    if (!versionText) return versionText.error();
    auto version = SemanticVersion::Parse(*versionText);
    if (!version) return Invalid("the version is not a valid semantic version");
    if (!version->IsIdenticalTo(expected.releaseVersion)) {
        return Invalid(std::format("the manifest version {} does not match the release tag version {}",
                                   version->ToString(), expected.releaseVersion.ToString()));
    }
    manifest.version = std::move(version).value();

    auto channelText = RequireString(root, "channel", "manifest");
    if (!channelText) return channelText.error();
    const auto channel = ParseChannel(*channelText);
    if (!channel) return Invalid("unknown channel (expected stable or prerelease)");
    const auto releaseChannel = expected.prerelease ? UpdateChannel::Prerelease : UpdateChannel::Stable;
    if (*channel != releaseChannel) {
        return Invalid(std::format("the manifest channel '{}' contradicts the release's prerelease flag",
                                   ToString(*channel)));
    }
    if (*channel == UpdateChannel::Stable && manifest.version.IsPrerelease()) {
        return Invalid("a prerelease version cannot be published on the stable channel");
    }
    manifest.channel = *channel;

    auto minimumBuild = RequireInteger(root, "minimumWindowsBuild", "manifest");
    if (!minimumBuild) return minimumBuild.error();
    if (*minimumBuild <= 0 || *minimumBuild > std::numeric_limits<std::uint32_t>::max()) {
        return Invalid("minimumWindowsBuild must be a positive build number");
    }
    manifest.minimumWindowsBuild = static_cast<std::uint32_t>(*minimumBuild);

    auto notesUrl = RequireString(root, "releaseNotesUrl", "manifest");
    if (!notesUrl) return notesUrl.error();
    if (!IsRepositoryWebUrl(*notesUrl, expected.repository)) {
        return Invalid("releaseNotesUrl must be a page of the official repository on https://github.com/");
    }
    manifest.releaseNotesUrl = std::move(notesUrl).value();

    const auto* assets = root.Find("assets");
    if (assets == nullptr || !assets->IsArray()) return Malformed("manifest.assets must be an array");
    const auto& entries = *assets->AsArray();
    if (entries.empty()) return Invalid("the manifest lists no installer");
    if (entries.size() > kMaxManifestAssets) return Invalid("the manifest lists too many assets");
    for (const auto& entry : entries) {
        auto asset = ParseAsset(entry, manifest.version);
        if (!asset) return asset.error();
        const bool duplicate = std::any_of(manifest.assets.begin(), manifest.assets.end(), [&](const ManifestAsset& a) {
            return a.fileName == asset->fileName;
        });
        if (duplicate) return Invalid("an asset file name is listed twice");
        manifest.assets.push_back(std::move(asset).value());
    }
    return manifest;
}

Result<ManifestAsset> UpdateManifest::SelectAsset(Architecture architecture, std::uint32_t osBuild) const {
    if (osBuild < minimumWindowsBuild) {
        return MakeError(ErrorCode::Unsupported,
                         std::format("version {} requires Windows build {} or later (this system: {})",
                                     version.ToString(), minimumWindowsBuild, osBuild));
    }
    for (const auto& asset : assets) {
        if (asset.architecture == architecture) return asset;
    }
    return MakeError(ErrorCode::Unsupported, std::format("version {} has no installer for {}", version.ToString(),
                                                         ToString(architecture)));
}

}  // namespace aether::updates
