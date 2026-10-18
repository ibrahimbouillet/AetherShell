// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "updates/SemanticVersion.h"

#include <string>
#include <string_view>
#include <vector>

using namespace aether;
using namespace aether::updates;

namespace {

SemanticVersion V(std::string_view text) {
    auto parsed = SemanticVersion::Parse(text);
    if (!parsed) {
        test::ReportFailure(__FILE__, __LINE__, "unexpected parse failure for '" + std::string(text) + "'");
        throw test::RequireFailure{};
    }
    return std::move(parsed).value();
}

}  // namespace

AETHER_TEST("semver: accepts every valid example from semver.org and round-trips it") {
    const std::vector<std::string_view> valid{
        "0.0.4", "1.2.3", "10.20.30", "1.1.2-prerelease+meta", "1.1.2+meta", "1.1.2+meta-valid", "1.0.0-alpha",
        "1.0.0-beta", "1.0.0-alpha.beta", "1.0.0-alpha.beta.1", "1.0.0-alpha.1", "1.0.0-alpha0.valid",
        "1.0.0-alpha.0valid", "1.0.0-alpha-a.b-c-somethinglong+build.1-aef.1-its-okay", "1.0.0-rc.1+build.1",
        "2.0.0-rc.1+build.123", "1.2.3-beta", "10.2.3-DEV-SNAPSHOT", "1.2.3-SNAPSHOT-123", "1.0.0", "2.0.0",
        "1.1.7", "2.0.0+build.1848", "2.0.1-alpha.1227", "1.0.0-alpha+beta", "1.2.3----RC-SNAPSHOT.12.9.1--.12+788",
        "1.2.3----R-S.12.9.1--.12+meta", "1.2.3----RC-SNAPSHOT.12.9.1--.12", "1.0.0+0.build.1-rc.10000aaa-kk-0.1",
        "1.0.0-0A.is.legal", "18446744073709551615.18446744073709551615.18446744073709551615",
        // Build metadata may have leading zeros; prerelease identifiers mixing letters may too.
        "1.0.0+001", "1.0.0-0abc", "1.0.0-99999999999999999999999999"};
    for (const auto text : valid) {
        auto parsed = SemanticVersion::Parse(text);
        CHECK(parsed.ok());
        if (parsed) CHECK_EQ(parsed->ToString(), std::string(text));
    }
}

AETHER_TEST("semver: rejects every invalid example from semver.org and other malformed input") {
    const std::vector<std::string_view> invalid{
        "1", "1.2", "1.2.3-0123", "1.2.3-0123.0123", "1.1.2+.123", "+invalid", "-invalid", "-invalid+invalid",
        "-invalid.01", "alpha", "alpha.beta", "alpha.beta.1", "alpha.1", "alpha+beta", "alpha_beta", "alpha.",
        "alpha..", "beta", "1.0.0-alpha_beta", "-alpha.", "1.0.0-alpha..", "1.0.0-alpha..1", "1.0.0-alpha...1",
        "1.0.0-alpha....1", "01.1.1", "1.01.1", "1.1.01", "1.2.3.DEV", "1.2-SNAPSHOT",
        "1.2.31.2.3----RC-SNAPSHOT.12.09.1--..12+788", "1.2-RC-SNAPSHOT", "-1.0.3-gamma+b7718", "+justmeta",
        "9.8.7+meta+meta", "9.8.7-whatever+meta+meta",
        "99999999999999999999999.999999999999999999.99999999999999999",  // valid SemVer, but overflows 64 bits
        "18446744073709551616.0.0", "0.18446744073709551616.0", "0.0.18446744073709551616",
        "", " 1.0.0", "1.0.0 ", "v1.0.0", "1.0.0-", "1.0.0+", "1.0.0-a+", "1.0.0-a.", "1.0.0-.a", "1..0",
        "1.0.0.0", "1.0.0-a b", "1.0.0-\xC3\xA9", "a.b.c", "1.0.x", "00.0.0", "1.0.0-00", "1.0.0-01.a", "1.-1.0",
        "1.+1.0", std::string_view("1.0.0\0", 6)};
    for (const auto text : invalid) {
        auto parsed = SemanticVersion::Parse(text);
        CHECK_FALSE(parsed.ok());
        if (!parsed) CHECK_EQ(parsed.error().code, ErrorCode::Parse);
    }
    const std::string tooLong = "1.0.0-" + std::string(300, 'a');
    CHECK_FALSE(SemanticVersion::Parse(tooLong).ok());
}

AETHER_TEST("semver: exposes components and identifiers") {
    const auto version = V("1.20.300-rc.1+build.7");
    CHECK_EQ(version.Major(), 1u);
    CHECK_EQ(version.Minor(), 20u);
    CHECK_EQ(version.Patch(), 300u);
    CHECK(version.IsPrerelease());
    REQUIRE_EQ(version.PrereleaseIdentifiers().size(), 2u);
    CHECK_EQ(version.PrereleaseIdentifiers()[0], std::string("rc"));
    CHECK_EQ(version.PrereleaseIdentifiers()[1], std::string("1"));
    REQUIRE_EQ(version.BuildMetadata().size(), 2u);
    CHECK_EQ(version.BuildMetadata()[1], std::string("7"));
    CHECK_FALSE(V("1.0.0").IsPrerelease());
    CHECK_EQ(SemanticVersion(4, 5, 6).ToString(), std::string("4.5.6"));
    CHECK_EQ(SemanticVersion().ToString(), std::string("0.0.0"));
}

AETHER_TEST("semver: precedence follows the specification chain") {
    const std::vector<std::string_view> chain{"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                                              "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0",
                                              "1.0.1", "1.1.0", "1.9.0", "1.10.0", "2.0.0", "2.1.0", "2.1.1"};
    for (std::size_t i = 0; i < chain.size(); ++i) {
        for (std::size_t j = 0; j < chain.size(); ++j) {
            const auto a = V(chain[i]);
            const auto b = V(chain[j]);
            if (i < j) {
                CHECK(a < b);
            } else if (i > j) {
                CHECK(a > b);
            } else {
                CHECK(a == b);
            }
        }
    }
    CHECK(V("1.10.0") > V("1.9.0"));
    CHECK(V("0.10.0") > V("0.9.99"));
}

AETHER_TEST("semver: numeric identifiers rank below alphanumeric ones and compare numerically") {
    CHECK(V("1.0.0-1") < V("1.0.0-a"));
    CHECK(V("1.0.0-2") < V("1.0.0-10"));
    CHECK(V("1.0.0-alpha.2") < V("1.0.0-alpha.10"));
    CHECK(V("1.0.0-a10") < V("1.0.0-a9"));  // alphanumeric: ASCII order
    CHECK(V("1.0.0-A") < V("1.0.0-a"));     // ASCII: uppercase sorts first
    CHECK(V("1.0.0-9999999999999999999") < V("1.0.0-99999999999999999999"));
    CHECK(V("1.0.0-99999999999999999998") < V("1.0.0-99999999999999999999"));
    CHECK(V("1.0.0-alpha") < V("1.0.0-alpha.0"));
    CHECK(V("1.0.0-0") < V("1.0.0-0.0"));
    CHECK(V("18446744073709551614.0.0") < V("18446744073709551615.0.0"));
}

AETHER_TEST("semver: build metadata is ignored for precedence but kept for identity") {
    const auto a = V("1.0.0+a");
    const auto b = V("1.0.0+b");
    CHECK(a == b);
    CHECK_FALSE(a < b);
    CHECK_FALSE(b < a);
    CHECK_FALSE(a.IsIdenticalTo(b));
    CHECK(a.IsIdenticalTo(V("1.0.0+a")));
    CHECK(V("1.0.0-alpha+zzz") < V("1.0.0-alpha.1+aaa"));
    CHECK(V("1.0.0+999") < V("1.0.1"));
    CHECK(V("1.0.0-rc.1+build.1") == V("1.0.0-rc.1"));
}

AETHER_TEST("semver: tag normalization accepts exactly one leading v or V") {
    auto lower = SemanticVersion::ParseTag("v1.2.3");
    REQUIRE(lower.ok());
    CHECK_EQ(lower->ToString(), std::string("1.2.3"));
    auto upper = SemanticVersion::ParseTag("V2.0.0-beta.1+exp.sha.5114f85");
    REQUIRE(upper.ok());
    CHECK_EQ(upper->ToString(), std::string("2.0.0-beta.1+exp.sha.5114f85"));
    auto bare = SemanticVersion::ParseTag("1.2.3");
    REQUIRE(bare.ok());
    CHECK(*bare == *lower);

    for (const std::string_view tag : {"vv1.2.3", "Vv1.2.3", " v1.2.3", "v 1.2.3", "release-1.2.3", "v", "", "v01.2.3",
                                       "version1.2.3", "v1.2", "v1.2.3 ", "x1.2.3", "-v1.2.3"}) {
        CHECK_FALSE(SemanticVersion::ParseTag(tag).ok());
    }
}
