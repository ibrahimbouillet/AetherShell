// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "UpdateTestSupport.h"

#include "core/FileSystem.h"
#include "core/Strings.h"
#include "updates/Encoding.h"
#include "updates/ReleaseNotes.h"
#include "updates/UpdateStateStore.h"

#include <chrono>
#include <string>

using namespace aether;
using namespace aether::updates;
using namespace aether::updates::testing;

// --------------------------------------------------------------------------------- release notes

AETHER_TEST("release notes: line breaks are normalized and blank runs collapse") {
    CHECK_EQ(SanitizeReleaseNotes("a\r\nb\rc\nd"), std::string("a\nb\nc\nd"));
    CHECK_EQ(SanitizeReleaseNotes("\r\n\r\n  \nTitle\r\n\r\n\r\n\r\nBody  \t\r\n\r\n\r\n"),
             std::string("Title\n\nBody"));
    CHECK_EQ(SanitizeReleaseNotes("a\n \t \nb"), std::string("a\n\nb"));
    CHECK_EQ(SanitizeReleaseNotes("a\xE2\x80\xA8" "b\xE2\x80\xA9" "c"), std::string("a\nb\nc"));  // U+2028/U+2029
    CHECK_EQ(SanitizeReleaseNotes(""), std::string());
    CHECK_EQ(SanitizeReleaseNotes("\n\n\n"), std::string());
}

AETHER_TEST("release notes: control and invisible formatting characters are removed") {
    CHECK_EQ(SanitizeReleaseNotes("a\x01" "b\x07" "c\x1B[31md\x7F" "e"), std::string("abc[31mde"));
    CHECK_EQ(SanitizeReleaseNotes("keep\ttabs"), std::string("keep\ttabs"));
    CHECK_EQ(SanitizeReleaseNotes("c1\xC2\x85" "x\xC2\x9B" "y"), std::string("c1xy"));               // NEL, CSI
    CHECK_EQ(SanitizeReleaseNotes("evil\xE2\x80\xAE" "txt.exe"), std::string("eviltxt.exe"));         // RLO
    CHECK_EQ(SanitizeReleaseNotes("\xE2\x81\xA6" "iso\xE2\x81\xA9"), std::string("iso"));               // LRI..PDI
    CHECK_EQ(SanitizeReleaseNotes("\xEF\xBB\xBF" "bom\xE2\x80\x8F"), std::string("bom"));              // BOM, RLM
    CHECK_EQ(SanitizeReleaseNotes(std::string("nul\0byte", 8)), std::string("nulbyte"));
    // Markup is not interpreted, only carried as text (the UI renders plain text).
    CHECK_EQ(SanitizeReleaseNotes("<script>alert(1)</script> **bold** [x](https://e.example)"),
             std::string("<script>alert(1)</script> **bold** [x](https://e.example)"));
    // Ordinary non-ASCII text survives.
    CHECK_EQ(SanitizeReleaseNotes("Caf\xC3\xA9 \xF0\x9F\x8C\x8C \xE4\xB8\xAD"),
             std::string("Caf\xC3\xA9 \xF0\x9F\x8C\x8C \xE4\xB8\xAD"));
}

AETHER_TEST("release notes: malformed UTF-8 becomes U+FFFD") {
    const std::string replacement = "\xEF\xBF\xBD";
    CHECK_EQ(SanitizeReleaseNotes("a\xFF" "b"), "a" + replacement + "b");
    CHECK_EQ(SanitizeReleaseNotes("\xC0\xAF"), replacement + replacement);              // overlong '/'
    CHECK_EQ(SanitizeReleaseNotes("\xED\xA0\x80"), replacement + replacement + replacement);  // surrogate
    CHECK_EQ(SanitizeReleaseNotes("x\xE2\x82"), "x" + replacement + replacement);       // truncated
    CHECK_EQ(SanitizeReleaseNotes("\xF4\x90\x80\x80"), replacement + replacement + replacement + replacement);
    CHECK(text::IsValidUtf8(SanitizeReleaseNotes("\x80\xBF\xF8\xFE garbage \xC3")));
}

AETHER_TEST("release notes: output is capped on a code-point boundary") {
    const std::string longText(100'000, 'x');
    const auto capped = SanitizeReleaseNotes(longText);
    CHECK(capped.size() <= kMaxSanitizedNotesBytes);
    CHECK(capped.ends_with("\xE2\x80\xA6"));

    std::string multibyte;
    for (int i = 0; i < 100; ++i) multibyte += "\xE4\xB8\xAD";  // 3-byte code points
    for (std::size_t limit = 3; limit < 40; ++limit) {
        const auto cut = SanitizeReleaseNotes(multibyte, limit);
        CHECK(cut.size() <= limit);
        CHECK(text::IsValidUtf8(cut));
    }
    CHECK_EQ(SanitizeReleaseNotes("short", 100), std::string("short"));
    CHECK_EQ(SanitizeReleaseNotes("line one\n\nline two", 12), std::string("line one\xE2\x80\xA6"));
    CHECK(SanitizeReleaseNotes("abcdef", 2).size() <= 2);
}

// ---------------------------------------------------------------------------------- state store

namespace {

ReleaseInfo SampleRelease() {
    auto parsed = ParseRelease(ReleaseJson(FixtureRelease(fixtures::kStable_1_2_0)), kTestRepository);
    if (!parsed) throw test::RequireFailure{};
    return std::move(parsed).value();
}

}  // namespace

AETHER_TEST("update state: serializes and parses every field") {
    PersistedUpdateState state;
    state.lastCheckUtc = encoding::ParseUtcTimestamp("2026-10-17T08:00:00Z").value();
    state.cache = ReleaseMetadataCache{UpdateChannel::Prerelease, "W/\"abc123\"", SampleRelease()};
    state.skippedVersion = SemanticVersion::Parse("1.2.0").value();

    const auto text = SerializeUpdateState(state);
    auto parsed = ParseUpdateState(text, kTestRepository);
    REQUIRE(parsed.ok());
    REQUIRE(parsed->lastCheckUtc.has_value());
    CHECK(*parsed->lastCheckUtc == *state.lastCheckUtc);
    REQUIRE(parsed->cache.has_value());
    CHECK(parsed->cache->channel == UpdateChannel::Prerelease);
    CHECK_EQ(parsed->cache->etag, std::string("W/\"abc123\""));
    CHECK_EQ(parsed->cache->release.tagName, std::string("v1.2.0"));
    CHECK_EQ(parsed->cache->release.assets.size(), state.cache->release.assets.size());
    CHECK_EQ(parsed->cache->release.body, state.cache->release.body);
    REQUIRE(parsed->skippedVersion.has_value());
    CHECK_EQ(parsed->skippedVersion->ToString(), std::string("1.2.0"));
    // Serializing again gives the same text.
    CHECK_EQ(SerializeUpdateState(*parsed), text);

    auto empty = ParseUpdateState(SerializeUpdateState({}), kTestRepository);
    REQUIRE(empty.ok());
    CHECK_FALSE(empty->lastCheckUtc.has_value());
    CHECK_FALSE(empty->cache.has_value());
    CHECK_FALSE(empty->skippedVersion.has_value());
}

AETHER_TEST("update state: invalid files are rejected, a missing file is a fresh state") {
    for (const std::string_view bad :
         {"", "{", "[]", R"({"schemaVersion":2})", R"({"lastCheckUtc":"2026-10-17T08:00:00Z"})",
          R"({"schemaVersion":1,"lastCheckUtc":"yesterday"})", R"({"schemaVersion":1,"lastCheckUtc":5})",
          R"({"schemaVersion":1,"skippedVersion":"v1.2.0"})", R"({"schemaVersion":1,"cachedChannel":"stable"})",
          R"({"schemaVersion":1,"cachedChannel":"nightly","cachedRelease":{}})"}) {
        CHECK_FALSE(ParseUpdateState(bad, kTestRepository).ok());
    }

    // Cached metadata is validated like a network response: another repository's URLs are refused.
    PersistedUpdateState state;
    state.cache = ReleaseMetadataCache{UpdateChannel::Stable, "\"x\"", SampleRelease()};
    const auto text = SerializeUpdateState(state);
    const GitHubRepository other{"someone-else", "AetherShell"};
    CHECK_FALSE(ParseUpdateState(text, other).ok());

    auto badEtag = json::Parse(text).value();
    badEtag.Set("etag", "line\nbreak");
    CHECK_FALSE(ParseUpdateState(json::Serialize(badEtag), kTestRepository).ok());

    const auto directory = test::MakeTempDirectory("updates-state");
    auto missing = LoadUpdateState(directory / L"update-state.json", kTestRepository);
    REQUIRE(missing.ok());
    CHECK_FALSE(missing->lastCheckUtc.has_value());

    REQUIRE(fs::WriteFileAtomic(directory / L"broken.json", "{ not json").ok());
    CHECK_FALSE(LoadUpdateState(directory / L"broken.json", kTestRepository).ok());
}

AETHER_TEST("update state: saving creates the directory and replaces the file atomically") {
    const auto directory = test::MakeTempDirectory("updates-state-save");
    const auto file = directory / L"config" / L"update-state.json";
    PersistedUpdateState state;
    state.lastCheckUtc = encoding::ParseUtcTimestamp("2026-10-17T09:30:00Z").value();
    REQUIRE(SaveUpdateState(file, state).ok());
    state.skippedVersion = SemanticVersion::Parse("2.0.0").value();
    REQUIRE(SaveUpdateState(file, state).ok());
    auto loaded = LoadUpdateState(file, kTestRepository);
    REQUIRE(loaded.ok());
    REQUIRE(loaded->skippedVersion.has_value());
    CHECK_EQ(loaded->skippedVersion->ToString(), std::string("2.0.0"));
    CHECK(*loaded->lastCheckUtc == *state.lastCheckUtc);
}
