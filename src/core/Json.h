// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Small, strict JSON (RFC 8259) document model used for configuration files,
// GitHub API responses, release manifests and weather data.
//
// Parsing is defensive because much of the input is untrusted network data:
//   * input size and nesting depth are bounded,
//   * strings must be valid UTF-8 and lone surrogates are rejected,
//   * duplicate object keys are rejected,
//   * trailing content, comments and NaN/Infinity are rejected.
#pragma once

#include "core/Result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace aether::json {

class Value;
struct Member;

using Array = std::vector<Value>;
using Object = std::vector<Member>;  // insertion order preserved; keys unique

enum class Type { Null, Bool, Number, String, Array, Object };

struct Number {
    double value = 0.0;
    bool isInteger = false;     // true when the literal had no fraction/exponent and fits in int64
    std::int64_t integer = 0;   // exact value when isInteger
    bool operator==(const Number& other) const noexcept;
};

class Value {
public:
    Value() noexcept = default;
    Value(std::nullptr_t) noexcept {}
    Value(bool value) : data_(value) {}
    Value(int value) : data_(Number{static_cast<double>(value), true, value}) {}
    Value(std::int64_t value) : data_(Number{static_cast<double>(value), true, value}) {}
    Value(std::uint32_t value) : data_(Number{static_cast<double>(value), true, static_cast<std::int64_t>(value)}) {}
    Value(std::uint64_t value);  // values above INT64_MAX are stored as (inexact) doubles
    Value(double value);
    Value(const char* value) : data_(std::string(value ? value : "")) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(std::string_view value) : data_(std::string(value)) {}
    Value(Array value);
    Value(Object value);

    [[nodiscard]] static Value MakeArray();
    [[nodiscard]] static Value MakeObject();

    [[nodiscard]] Type GetType() const noexcept;
    [[nodiscard]] bool IsNull() const noexcept { return GetType() == Type::Null; }
    [[nodiscard]] bool IsBool() const noexcept { return GetType() == Type::Bool; }
    [[nodiscard]] bool IsNumber() const noexcept { return GetType() == Type::Number; }
    [[nodiscard]] bool IsString() const noexcept { return GetType() == Type::String; }
    [[nodiscard]] bool IsArray() const noexcept { return GetType() == Type::Array; }
    [[nodiscard]] bool IsObject() const noexcept { return GetType() == Type::Object; }

    [[nodiscard]] std::optional<bool> AsBool() const noexcept;
    [[nodiscard]] std::optional<double> AsDouble() const noexcept;
    // Exact integers only: integer literals, or finite doubles with no fractional part within int64 range.
    [[nodiscard]] std::optional<std::int64_t> AsInt64() const noexcept;
    [[nodiscard]] const std::string* AsString() const noexcept;
    [[nodiscard]] const Array* AsArray() const noexcept;
    [[nodiscard]] Array* AsArray() noexcept;
    [[nodiscard]] const Object* AsObject() const noexcept;
    [[nodiscard]] Object* AsObject() noexcept;

    // Object member lookup; nullptr when this is not an object or the key is absent.
    [[nodiscard]] const Value* Find(std::string_view key) const noexcept;
    [[nodiscard]] Value* Find(std::string_view key) noexcept;

    // Typed member lookups with fallbacks (fallback also used when the member has the wrong type).
    [[nodiscard]] std::string GetString(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] bool GetBool(std::string_view key, bool fallback) const noexcept;
    [[nodiscard]] std::int64_t GetInt64(std::string_view key, std::int64_t fallback) const noexcept;
    [[nodiscard]] double GetDouble(std::string_view key, double fallback) const noexcept;

    // Converts null to an object if needed, then inserts or replaces `key`. Returns the stored value.
    Value& Set(std::string key, Value value);
    // Converts null to an array if needed, then appends.
    Value& Append(Value value);

    bool operator==(const Value& other) const;

private:
    std::variant<std::nullptr_t, bool, Number, std::string, Array, Object> data_{nullptr};
};

struct Member {
    std::string key;
    Value value;
    bool operator==(const Member& other) const { return key == other.key && value == other.value; }
};

struct ParseOptions {
    std::size_t maxInputBytes = 16u * 1024u * 1024u;
    std::size_t maxDepth = 64;
};

[[nodiscard]] Result<Value> Parse(std::string_view text, const ParseOptions& options = {});

struct SerializeOptions {
    bool pretty = false;
    int indent = 2;
};

[[nodiscard]] std::string Serialize(const Value& value, const SerializeOptions& options = {});

}  // namespace aether::json
