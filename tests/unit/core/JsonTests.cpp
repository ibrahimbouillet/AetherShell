// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/Json.h"

#include <string>

using namespace aether;
using namespace aether::json;

AETHER_TEST("json: parses scalars") {
    CHECK(Parse("null").value().IsNull());
    CHECK_EQ(*Parse("true").value().AsBool(), true);
    CHECK_EQ(*Parse("false").value().AsBool(), false);
    CHECK_EQ(*Parse("42").value().AsInt64(), 42);
    CHECK_EQ(*Parse("-7").value().AsInt64(), -7);
    CHECK_NEAR(*Parse("3.25").value().AsDouble(), 3.25, 1e-12);
    CHECK_NEAR(*Parse("-1.5e3").value().AsDouble(), -1500.0, 1e-9);
    CHECK_EQ(*Parse("\"hi\"").value().AsString(), std::string("hi"));
}

AETHER_TEST("json: parses nested structures and preserves member order") {
    auto parsed = Parse(R"({"b": [1, 2, {"c": null}], "a": {"x": "y"}})");
    REQUIRE(parsed.ok());
    const auto& root = parsed.value();
    REQUIRE(root.IsObject());
    REQUIRE_EQ(root.AsObject()->size(), 2u);
    CHECK_EQ(root.AsObject()->at(0).key, std::string("b"));
    CHECK_EQ(root.AsObject()->at(1).key, std::string("a"));
    CHECK_EQ(root.Find("b")->AsArray()->size(), 3u);
    CHECK(root.Find("b")->AsArray()->at(2).Find("c")->IsNull());
    CHECK_EQ(root.Find("a")->GetString("x"), std::string("y"));
    CHECK(root.Find("missing") == nullptr);
}

AETHER_TEST("json: decodes escapes and surrogate pairs") {
    auto parsed = Parse(R"("a\"b\\c\/d\n\t\u00e9\ud83d\ude00")");
    REQUIRE(parsed.ok());
    CHECK_EQ(*parsed.value().AsString(), std::string("a\"b\\c/d\n\t\xC3\xA9\xF0\x9F\x98\x80"));
}

AETHER_TEST("json: rejects malformed documents") {
    const char* invalid[] = {
        "",           "{",          "[1,]",         "{\"a\":1,}",  "01",         "1.",         ".5",
        "-",          "1e",         "tru",          "nul",         "\"abc",      "'single'",   "[1 2]",
        "{\"a\" 1}",  "NaN",        "Infinity",     "// c\n1",     "1 2",        "\"\\x\"",    "\"\\ud800\"",
        "\"\\udc00\"", "{\"a\":1,\"a\":2}", "1e999",
    };
    for (const char* text : invalid) {
        auto parsed = Parse(text);
        if (parsed.ok()) {
            ::aether::test::ReportFailure(__FILE__, __LINE__, std::string("accepted invalid JSON: ") + text);
        } else {
            CHECK_EQ(parsed.error().code, ErrorCode::Parse);
        }
    }
}

AETHER_TEST("json: rejects control characters and invalid UTF-8 in strings") {
    CHECK_FALSE(Parse(std::string("\"a\x01" "b\"")).ok());
    CHECK_FALSE(Parse(std::string("\"\xC3\"")).ok());           // truncated sequence
    CHECK_FALSE(Parse(std::string("\"\xC0\xAF\"")).ok());       // overlong encoding
    CHECK_FALSE(Parse(std::string("\"\xED\xA0\x80\"")).ok());   // encoded surrogate
    CHECK_FALSE(Parse(std::string("\"\xF4\x90\x80\x80\"")).ok()); // above U+10FFFF
    CHECK(Parse(std::string("\"\xE2\x82\xAC\"")).ok());         // euro sign
}

AETHER_TEST("json: enforces depth and size limits") {
    std::string deep(100, '[');
    deep += std::string(100, ']');
    CHECK_FALSE(Parse(deep).ok());
    ParseOptions relaxed;
    relaxed.maxDepth = 200;
    CHECK(Parse(deep, relaxed).ok());

    ParseOptions tiny;
    tiny.maxInputBytes = 4;
    CHECK_FALSE(Parse("[1,2,3]", tiny).ok());
}

AETHER_TEST("json: accepts a UTF-8 byte order mark") {
    CHECK(Parse("\xEF\xBB\xBF{\"a\":1}").ok());
}

AETHER_TEST("json: large integers keep exact values") {
    auto parsed = Parse("9007199254740993");
    REQUIRE(parsed.ok());
    CHECK_EQ(*parsed.value().AsInt64(), static_cast<std::int64_t>(9007199254740993LL));
    CHECK_FALSE(Parse("1.5").value().AsInt64().has_value());
    CHECK_EQ(*Parse("2.0").value().AsInt64(), 2);
}

AETHER_TEST("json: serialization round-trips") {
    Value root = Value::MakeObject();
    root.Set("name", "Aether Shell");
    root.Set("count", 3);
    root.Set("ratio", 0.5);
    root.Set("enabled", true);
    root.Set("nothing", nullptr);
    auto& list = root.Set("list", Value::MakeArray());
    list.Append("a\"b\n");
    list.Append(std::int64_t{-12});
    root.Set("count", 4);  // replaces the existing member

    for (bool pretty : {false, true}) {
        SerializeOptions options;
        options.pretty = pretty;
        const std::string text = Serialize(root, options);
        auto reparsed = Parse(text);
        REQUIRE(reparsed.ok());
        CHECK(reparsed.value() == root);
    }
    CHECK_EQ(Serialize(Parse(R"({"a":[1,true,null,"x"]})").value()), std::string(R"({"a":[1,true,null,"x"]})"));
    CHECK_EQ(root.GetInt64("count", 0), 4);
}

AETHER_TEST("json: serializer escapes control characters") {
    const std::string text = Serialize(Value(std::string("\x01\x1f\x7f")));
    CHECK_EQ(text, std::string("\"\\u0001\\u001f\\u007f\""));
}

AETHER_TEST("json: typed getters fall back on type mismatch") {
    auto value = Parse(R"({"s": 1, "b": "yes", "i": 1.5, "d": "x"})").value();
    CHECK_EQ(value.GetString("s", "fallback"), std::string("fallback"));
    CHECK_EQ(value.GetBool("b", false), false);
    CHECK_EQ(value.GetInt64("i", -1), -1);
    CHECK_NEAR(value.GetDouble("d", 2.5), 2.5, 0.0);
}

AETHER_TEST("json: non-finite doubles serialize as null") {
    CHECK(Value(std::numeric_limits<double>::infinity()).IsNull());
}
