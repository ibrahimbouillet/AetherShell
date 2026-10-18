// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/UpdateStateStore.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "updates/Encoding.h"

namespace aether::updates {
namespace {

Error Malformed(std::string message) { return MakeError(ErrorCode::Parse, "invalid update state: " + std::move(message)); }

}  // namespace

std::string SerializeUpdateState(const PersistedUpdateState& state) {
    auto root = json::Value::MakeObject();
    root.Set("schemaVersion", kUpdateStateSchemaVersion);
    if (state.lastCheckUtc) root.Set("lastCheckUtc", encoding::FormatUtcTimestamp(*state.lastCheckUtc));
    if (state.cache) {
        if (!state.cache->etag.empty()) root.Set("etag", state.cache->etag);
        root.Set("cachedChannel", std::string(ToString(state.cache->channel)));
        root.Set("cachedRelease", ReleaseToJson(state.cache->release));
    }
    if (state.skippedVersion) root.Set("skippedVersion", state.skippedVersion->ToString());
    return json::Serialize(root, json::SerializeOptions{true, 2}) + "\n";
}

Result<PersistedUpdateState> ParseUpdateState(std::string_view text, const GitHubRepository& repository) {
    auto document = json::Parse(text, json::ParseOptions{kMaxUpdateStateBytes, 16});
    if (!document) return Malformed(document.error().message);
    const auto& root = *document;
    if (!root.IsObject()) return Malformed("the root is not an object");
    const auto* schema = root.Find("schemaVersion");
    if (schema == nullptr || schema->AsInt64() != kUpdateStateSchemaVersion) {
        return MakeError(ErrorCode::Unsupported, "unsupported update state schema version");
    }

    PersistedUpdateState state;
    if (const auto* lastCheck = root.Find("lastCheckUtc")) {
        if (!lastCheck->IsString()) return Malformed("lastCheckUtc must be a string");
        auto time = encoding::ParseUtcTimestamp(*lastCheck->AsString());
        if (!time) return Malformed("lastCheckUtc is not a UTC timestamp");
        state.lastCheckUtc = *time;
    }

    const auto* release = root.Find("cachedRelease");
    const auto* channel = root.Find("cachedChannel");
    if (release != nullptr || channel != nullptr) {
        if (release == nullptr || channel == nullptr || !channel->IsString()) {
            return Malformed("cachedRelease and cachedChannel must appear together");
        }
        const auto parsedChannel = ParseChannel(*channel->AsString());
        if (!parsedChannel) return Malformed("cachedChannel is not stable or prerelease");
        auto parsedRelease = ParseRelease(*release, repository);
        if (!parsedRelease) return parsedRelease.error();
        ReleaseMetadataCache cache{*parsedChannel, {}, std::move(parsedRelease).value()};
        if (const auto* etag = root.Find("etag")) {
            if (!etag->IsString() || !IsAcceptableEtag(*etag->AsString())) return Malformed("etag is not acceptable");
            cache.etag = *etag->AsString();
        }
        state.cache = std::move(cache);
    }

    if (const auto* skipped = root.Find("skippedVersion")) {
        if (!skipped->IsString()) return Malformed("skippedVersion must be a string");
        auto version = SemanticVersion::Parse(*skipped->AsString());
        if (!version) return Malformed("skippedVersion is not a semantic version");
        state.skippedVersion = std::move(version).value();
    }
    return state;
}

Result<PersistedUpdateState> LoadUpdateState(const std::filesystem::path& file, const GitHubRepository& repository) {
    auto text = fs::ReadFile(file, kMaxUpdateStateBytes);
    if (!text) {
        if (text.error().code == ErrorCode::NotFound) return PersistedUpdateState{};
        return text.error();
    }
    return ParseUpdateState(*text, repository);
}

Result<void> SaveUpdateState(const std::filesystem::path& file, const PersistedUpdateState& state) {
    return fs::WriteFileAtomic(file, SerializeUpdateState(state));
}

}  // namespace aether::updates
