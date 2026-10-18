// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// A deliberately small unit-test framework. It avoids an external dependency
// while still giving per-case reporting through the CTest runner.
//
//   AETHER_TEST("json parses nested arrays") {
//       auto value = aether::json::Parse("[[1]]");
//       REQUIRE(value.ok());
//       CHECK_EQ(value.value().AsArray()->size(), 1u);
//   }
//
// CHECK* records a failure and continues; REQUIRE* records a failure and stops
// the current test case.
#pragma once

#include <concepts>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace aether::test {

using TestFunction = void (*)();

struct TestCase {
    const char* name;
    TestFunction function;
    const char* file;
    int line;
};

std::vector<TestCase>& Registry();

struct Registrar {
    Registrar(const char* name, TestFunction function, const char* file, int line);
};

// Thrown by REQUIRE* to abort the current test case.
struct RequireFailure {};

void ReportFailure(const char* file, int line, const std::string& message);
void Note(const std::string& message);
std::string NarrowForDisplay(std::wstring_view text);

// Creates a fresh, empty directory under %TEMP% for the current test and returns it.
std::filesystem::path MakeTempDirectory(std::string_view prefix);

template <class T>
concept Streamable = requires(std::ostream& os, const T& value) { os << value; };

template <class T>
std::string ToDisplay(const T& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, std::wstring> || std::is_same_v<U, std::wstring_view>) {
        return "L\"" + NarrowForDisplay(value) + "\"";
    } else if constexpr (std::is_same_v<U, const wchar_t*> || std::is_same_v<U, wchar_t*>) {
        return value ? "L\"" + NarrowForDisplay(value) + "\"" : std::string("nullptr");
    } else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, std::string_view>) {
        return "\"" + std::string(value) + "\"";
    } else if constexpr (std::is_same_v<U, const char*> || std::is_same_v<U, char*>) {
        return value ? "\"" + std::string(value) + "\"" : std::string("nullptr");
    } else if constexpr (std::is_same_v<U, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_same_v<U, std::nullptr_t>) {
        return "nullptr";
    } else if constexpr (std::is_enum_v<U>) {
        return "enum(" + std::to_string(static_cast<long long>(value)) + ")";
    } else if constexpr (std::is_arithmetic_v<U>) {
        std::ostringstream stream;
        stream << +value;
        return stream.str();
    } else if constexpr (Streamable<U>) {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    } else {
        return "<unprintable>";
    }
}

template <class A, class B>
void CheckEqual(const A& actual, const B& expected, const char* actualText, const char* expectedText,
                const char* file, int line, bool require) {
    if (!(actual == expected)) {
        ReportFailure(file, line,
                      std::string("CHECK_EQ(") + actualText + ", " + expectedText + ")\n      actual:   " +
                          ToDisplay(actual) + "\n      expected: " + ToDisplay(expected));
        if (require) throw RequireFailure{};
    }
}

template <class A, class B>
void CheckNotEqual(const A& actual, const B& unexpected, const char* actualText, const char* unexpectedText,
                   const char* file, int line, bool require) {
    if (actual == unexpected) {
        ReportFailure(file, line,
                      std::string("CHECK_NE(") + actualText + ", " + unexpectedText + ")\n      both: " +
                          ToDisplay(actual));
        if (require) throw RequireFailure{};
    }
}

}  // namespace aether::test

#define AETHER_TEST_CAT_INNER(a, b) a##b
#define AETHER_TEST_CAT(a, b) AETHER_TEST_CAT_INNER(a, b)

#define AETHER_TEST_IMPL(name, id)                                                                  \
    static void id();                                                                               \
    static const ::aether::test::Registrar AETHER_TEST_CAT(id, _registrar){name, &id, __FILE__,     \
                                                                          __LINE__};                \
    static void id()

#define AETHER_TEST(name) AETHER_TEST_IMPL(name, AETHER_TEST_CAT(aether_test_case_, __COUNTER__))

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) ::aether::test::ReportFailure(__FILE__, __LINE__, "CHECK(" #expr ")"); \
    } while (false)

#define REQUIRE(expr)                                                                        \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            ::aether::test::ReportFailure(__FILE__, __LINE__, "REQUIRE(" #expr ")");         \
            throw ::aether::test::RequireFailure{};                                          \
        }                                                                                    \
    } while (false)

#define CHECK_FALSE(expr) CHECK(!(expr))
#define REQUIRE_FALSE(expr) REQUIRE(!(expr))

#define CHECK_EQ(actual, expected) \
    ::aether::test::CheckEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__, false)
#define REQUIRE_EQ(actual, expected) \
    ::aether::test::CheckEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__, true)
#define CHECK_NE(actual, unexpected) \
    ::aether::test::CheckNotEqual((actual), (unexpected), #actual, #unexpected, __FILE__, __LINE__, false)

#define CHECK_NEAR(actual, expected, tolerance)                                                   \
    do {                                                                                          \
        const double aether_a_ = static_cast<double>(actual);                                     \
        const double aether_e_ = static_cast<double>(expected);                                   \
        if (!(aether_a_ - aether_e_ <= (tolerance) && aether_e_ - aether_a_ <= (tolerance))) {     \
            ::aether::test::ReportFailure(                                                        \
                __FILE__, __LINE__,                                                               \
                std::string("CHECK_NEAR(" #actual ", " #expected ")\n      actual:   ") +         \
                    ::aether::test::ToDisplay(aether_a_) +                                        \
                    "\n      expected: " + ::aether::test::ToDisplay(aether_e_));                 \
        }                                                                                         \
    } while (false)

#define CHECK_THROWS(expr)                                                                         \
    do {                                                                                           \
        bool aether_threw_ = false;                                                                \
        try {                                                                                      \
            (void)(expr);                                                                          \
        } catch (...) {                                                                            \
            aether_threw_ = true;                                                                  \
        }                                                                                          \
        if (!aether_threw_)                                                                        \
            ::aether::test::ReportFailure(__FILE__, __LINE__, "CHECK_THROWS(" #expr ") did not throw"); \
    } while (false)

#define TEST_NOTE(message) ::aether::test::Note(message)
