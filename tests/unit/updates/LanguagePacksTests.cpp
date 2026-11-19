// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "UpdateTestSupport.h"

#include "core/FileSystem.h"
#include "core/Localization.h"
#include "updates/LanguagePacks.h"

#include <filesystem>
#include <string>

using namespace aether;
using namespace aether::updates;
using namespace aether::updates::testing;
using i18n::Language;

namespace {
constexpr std::string_view kVersion = "1.2.0";
}  // namespace

// D83: every language but English is an asset of the version's own release.
AETHER_TEST("language packs: each language's pack is an asset of this version's release") {
    CHECK_EQ(LanguagePackUrl(Language::German, "0.4.0-alpha.4"),
             std::string("https://github.com/ibrahimbouillet/AetherShell/releases/download/v0.4.0-alpha.4/AetherShell-language-de.json"));
    CHECK_EQ(LanguagePackUrl(Language::ChineseTraditional, "0.4.0-alpha.4"),
             std::string("https://github.com/ibrahimbouillet/AetherShell/releases/download/v0.4.0-alpha.4/AetherShell-language-zh-Hant.json"));
    CHECK(LanguagePackUrl(Language::English).empty());   // built in
    CHECK(LanguagePackUrl(Language::German, "").empty());
}

AETHER_TEST("language packs: a download that is not this version's pack is neither used nor written") {
    const auto root = test::MakeTempDirectory("language-packs-refused");
    const auto languages = root / L"languages";
    FakeHttpClient http;
    const auto url = LanguagePackUrl(Language::Swedish, kVersion, kTestRepository);
    // The right size, other bytes.
    http.Set(url, Ok200(std::string(static_cast<std::size_t>(i18n::Info(Language::Swedish).packBytes), ' ')));
    auto installed = InstallLanguagePack(http, Language::Swedish, languages, {}, kVersion, kTestRepository);
    REQUIRE_FALSE(installed.ok());
    CHECK(installed.error().code == ErrorCode::Integrity);
    CHECK_FALSE(std::filesystem::exists(i18n::PackPath(Language::Swedish, languages)));
    CHECK_FALSE(i18n::HasTexts(Language::Swedish));
    // Larger than this version's pack: refused by the transfer's own limit.
    http.Set(url, Ok200(std::string(static_cast<std::size_t>(i18n::Info(Language::Swedish).packBytes) + 1, ' ')));
    installed = InstallLanguagePack(http, Language::Swedish, languages, {}, kVersion, kTestRepository);
    REQUIRE_FALSE(installed.ok());
    CHECK(installed.error().code == ErrorCode::Integrity);
    // Not published (an older release without packs): NotFound, nothing written.
    http.Set(url, Status(404));
    installed = InstallLanguagePack(http, Language::Swedish, languages, {}, kVersion, kTestRepository);
    REQUIRE_FALSE(installed.ok());
    CHECK(installed.error().code == ErrorCode::NotFound);
    CHECK_FALSE(std::filesystem::exists(i18n::PackPath(Language::Swedish, languages)));
    // Redirected anywhere but GitHub's download hosts.
    FakeResponse foreign = Ok200("{}");
    foreign.finalUrl = "https://evil.example/AetherShell-language-sv.json";
    http.Set(url, foreign);
    CHECK(InstallLanguagePack(http, Language::Swedish, languages, {}, kVersion, kTestRepository).error().code == ErrorCode::Validation);
    // English has no pack.
    CHECK(InstallLanguagePack(http, Language::English, languages, {}, kVersion, kTestRepository).error().code == ErrorCode::Unavailable);
}

AETHER_TEST("language packs: this version's pack is downloaded, put in use and kept for the next start") {
#ifdef AETHER_TEST_LANGUAGE_PACKS
    const std::filesystem::path built(AETHER_TEST_LANGUAGE_PACKS);
    const auto bytes = fs::ReadFile(i18n::PackPath(Language::German, built), 4u * 1024 * 1024);
    REQUIRE(bytes.ok());
    const auto root = test::MakeTempDirectory("language-packs-installed");
    const auto languages = root / L"languages";
    FakeHttpClient http;
    const auto url = LanguagePackUrl(Language::German, kVersion, kTestRepository);
    http.Set(url, Ok200(*bytes));
    REQUIRE_FALSE(i18n::HasTexts(Language::German));
    const auto installed = InstallLanguagePack(http, Language::German, languages, {}, kVersion, kTestRepository);
    REQUIRE(installed.ok());
    const auto request = http.LastRequestFor(url);
    REQUIRE(request.has_value());
    CHECK_EQ(request->maxResponseBytes, i18n::Info(Language::German).packBytes);   // never more than the pack
    CHECK(i18n::HasTexts(Language::German));
    CHECK(i18n::Tr(L"Settings", Language::German) != std::wstring_view(L"Settings"));
    CHECK(i18n::PackInstalled(Language::German, languages));
    const auto written = fs::ReadFile(i18n::PackPath(Language::German, languages), 4u * 1024 * 1024);
    REQUIRE(written.ok());
    CHECK(*written == *bytes);
    CHECK(i18n::LoadInstalledPack(Language::German, languages).ok());
#else
    TEST_NOTE("no Python when this was configured: the packs were not built for this test");
#endif
}
