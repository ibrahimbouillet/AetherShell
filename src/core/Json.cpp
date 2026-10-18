// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Json.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace aether::json {

bool Number::operator==(const Number& other) const noexcept {
    if (isInteger && other.isInteger) return integer == other.integer;
    return value == other.value;
}

Value::Value(double value) {
    if (!std::isfinite(value)) {
        data_ = nullptr;  // JSON has no representation for NaN/Infinity
        return;
    }
    Number number{value, false, 0};
    data_ = number;
}

Value::Value(std::uint64_t value) {
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        data_ = Number{static_cast<double>(value), true, static_cast<std::int64_t>(value)};
    } else {
        data_ = Number{static_cast<double>(value), false, 0};
    }
}

Value::Value(Array value) : data_(std::move(value)) {}
Value::Value(Object value) : data_(std::move(value)) {}

Value Value::MakeArray() { return Value(Array{}); }
Value Value::MakeObject() { return Value(Object{}); }

Type Value::GetType() const noexcept { return static_cast<Type>(data_.index()); }

std::optional<bool> Value::AsBool() const noexcept {
    if (auto p = std::get_if<bool>(&data_)) return *p;
    return std::nullopt;
}

std::optional<double> Value::AsDouble() const noexcept {
    if (auto p = std::get_if<Number>(&data_)) return p->value;
    return std::nullopt;
}

std::optional<std::int64_t> Value::AsInt64() const noexcept {
    auto p = std::get_if<Number>(&data_);
    if (!p) return std::nullopt;
    if (p->isInteger) return p->integer;
    const double v = p->value;
    if (!std::isfinite(v) || std::trunc(v) != v) return std::nullopt;
    // 2^63 is exactly representable; anything >= it does not fit.
    if (v < -9223372036854775808.0 || v >= 9223372036854775808.0) return std::nullopt;
    return static_cast<std::int64_t>(v);
}

const std::string* Value::AsString() const noexcept { return std::get_if<std::string>(&data_); }
const Array* Value::AsArray() const noexcept { return std::get_if<Array>(&data_); }
Array* Value::AsArray() noexcept { return std::get_if<Array>(&data_); }
const Object* Value::AsObject() const noexcept { return std::get_if<Object>(&data_); }
Object* Value::AsObject() noexcept { return std::get_if<Object>(&data_); }

const Value* Value::Find(std::string_view key) const noexcept {
    if (auto object = AsObject()) {
        for (const auto& member : *object) {
            if (member.key == key) return &member.value;
        }
    }
    return nullptr;
}

Value* Value::Find(std::string_view key) noexcept {
    if (auto object = AsObject()) {
        for (auto& member : *object) {
            if (member.key == key) return &member.value;
        }
    }
    return nullptr;
}

std::string Value::GetString(std::string_view key, std::string_view fallback) const {
    if (auto v = Find(key)) {
        if (auto s = v->AsString()) return *s;
    }
    return std::string(fallback);
}

bool Value::GetBool(std::string_view key, bool fallback) const noexcept {
    if (auto v = Find(key)) {
        if (auto b = v->AsBool()) return *b;
    }
    return fallback;
}

std::int64_t Value::GetInt64(std::string_view key, std::int64_t fallback) const noexcept {
    if (auto v = Find(key)) {
        if (auto i = v->AsInt64()) return *i;
    }
    return fallback;
}

double Value::GetDouble(std::string_view key, double fallback) const noexcept {
    if (auto v = Find(key)) {
        if (auto d = v->AsDouble()) return *d;
    }
    return fallback;
}

Value& Value::Set(std::string key, Value value) {
    if (IsNull()) data_ = Object{};
    auto object = AsObject();
    if (!object) {
        data_ = Object{};
        object = AsObject();
    }
    for (auto& member : *object) {
        if (member.key == key) {
            member.value = std::move(value);
            return member.value;
        }
    }
    object->push_back(Member{std::move(key), std::move(value)});
    return object->back().value;
}

Value& Value::Append(Value value) {
    if (!IsArray()) data_ = Array{};
    auto array = AsArray();
    array->push_back(std::move(value));
    return array->back();
}

bool Value::operator==(const Value& other) const { return data_ == other.data_; }

// ---------------------------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------------------------
namespace {

class Parser {
public:
    Parser(std::string_view text, const ParseOptions& options) : text_(text), options_(options) {}

    Result<Value> Run() {
        if (text_.size() > options_.maxInputBytes) {
            return MakeError(ErrorCode::Parse, "JSON input exceeds the size limit");
        }
        // Tolerate a UTF-8 byte order mark written by some editors.
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB && static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        }
        SkipWhitespace();
        Value value;
        if (!ParseValue(value, 0)) return Failure();
        SkipWhitespace();
        if (pos_ != text_.size()) {
            Fail("unexpected trailing characters");
            return Failure();
        }
        return value;
    }

private:
    Error Failure() const {
        return MakeError(ErrorCode::Parse, error_ + " at offset " + std::to_string(errorPos_));
    }

    bool Fail(const char* message) {
        if (error_.empty()) {
            error_ = message;
            errorPos_ = pos_;
        }
        return false;
    }

    void SkipWhitespace() noexcept {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Consume(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) == literal) {
            pos_ += literal.size();
            return true;
        }
        return false;
    }

    bool ParseValue(Value& out, std::size_t depth) {
        if (depth > options_.maxDepth) return Fail("maximum nesting depth exceeded");
        if (pos_ >= text_.size()) return Fail("unexpected end of input");
        switch (text_[pos_]) {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"': {
                std::string s;
                if (!ParseString(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't':
                if (Consume("true")) {
                    out = Value(true);
                    return true;
                }
                return Fail("invalid literal");
            case 'f':
                if (Consume("false")) {
                    out = Value(false);
                    return true;
                }
                return Fail("invalid literal");
            case 'n':
                if (Consume("null")) {
                    out = Value(nullptr);
                    return true;
                }
                return Fail("invalid literal");
            default: return ParseNumber(out);
        }
    }

    bool ParseObject(Value& out, std::size_t depth) {
        ++pos_;  // '{'
        Object object;
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            out = Value(std::move(object));
            return true;
        }
        for (;;) {
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("expected object key");
            std::string key;
            if (!ParseString(key)) return false;
            for (const auto& member : object) {
                if (member.key == key) return Fail("duplicate object key");
            }
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') return Fail("expected ':'");
            ++pos_;
            SkipWhitespace();
            Value value;
            if (!ParseValue(value, depth + 1)) return false;
            object.push_back(Member{std::move(key), std::move(value)});
            SkipWhitespace();
            if (pos_ >= text_.size()) return Fail("unterminated object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                out = Value(std::move(object));
                return true;
            }
            return Fail("expected ',' or '}'");
        }
    }

    bool ParseArray(Value& out, std::size_t depth) {
        ++pos_;  // '['
        Array array;
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            out = Value(std::move(array));
            return true;
        }
        for (;;) {
            SkipWhitespace();
            Value value;
            if (!ParseValue(value, depth + 1)) return false;
            array.push_back(std::move(value));
            SkipWhitespace();
            if (pos_ >= text_.size()) return Fail("unterminated array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                out = Value(std::move(array));
                return true;
            }
            return Fail("expected ',' or ']'");
        }
    }

    static void AppendUtf8(std::string& out, char32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool ParseHex4(char32_t& out) {
        if (pos_ + 4 > text_.size()) return Fail("truncated \\u escape");
        char32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<char32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<char32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<char32_t>(c - 'A' + 10);
            } else {
                return Fail("invalid hex digit in \\u escape");
            }
        }
        out = value;
        return true;
    }

    // Validates one raw UTF-8 sequence starting at pos_ and appends it.
    bool CopyUtf8Sequence(std::string& out) {
        const auto lead = static_cast<unsigned char>(text_[pos_]);
        int extra = 0;
        char32_t cp = 0;
        char32_t minimum = 0;
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            cp = lead & 0x1F;
            minimum = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            cp = lead & 0x0F;
            minimum = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            cp = lead & 0x07;
            minimum = 0x10000;
        } else {
            return Fail("invalid UTF-8 lead byte");
        }
        if (pos_ + static_cast<std::size_t>(extra) >= text_.size()) return Fail("truncated UTF-8 sequence");
        for (int k = 1; k <= extra; ++k) {
            const auto c = static_cast<unsigned char>(text_[pos_ + static_cast<std::size_t>(k)]);
            if ((c & 0xC0) != 0x80) return Fail("invalid UTF-8 continuation byte");
            cp = (cp << 6) | (c & 0x3F);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return Fail("invalid UTF-8 code point");
        }
        out.append(text_.substr(pos_, static_cast<std::size_t>(extra) + 1));
        pos_ += static_cast<std::size_t>(extra) + 1;
        return true;
    }

    bool ParseString(std::string& out) {
        ++pos_;  // opening quote
        for (;;) {
            if (pos_ >= text_.size()) return Fail("unterminated string");
            const auto c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c < 0x20) return Fail("control character in string");
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) return Fail("unterminated escape");
                const char e = text_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        char32_t cp = 0;
                        if (!ParseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (!(pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u')) {
                                return Fail("unpaired high surrogate");
                            }
                            pos_ += 2;
                            char32_t low = 0;
                            if (!ParseHex4(low)) return false;
                            if (low < 0xDC00 || low > 0xDFFF) return Fail("invalid low surrogate");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            return Fail("unpaired low surrogate");
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default: return Fail("invalid escape sequence");
                }
                continue;
            }
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++pos_;
            } else if (!CopyUtf8Sequence(out)) {
                return false;
            }
        }
    }

    bool ParseNumber(Value& out) {
        const std::size_t start = pos_;
        bool isInteger = true;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) return Fail("invalid number");
        if (text_[pos_] == '0') {
            ++pos_;
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        } else {
            return Fail("unexpected character");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            isInteger = false;
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') return Fail("invalid fraction");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            isInteger = false;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') return Fail("invalid exponent");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;

        Number number;
        const auto doubleResult = std::from_chars(first, last, number.value);
        if (doubleResult.ec != std::errc{} || doubleResult.ptr != last || !std::isfinite(number.value)) {
            pos_ = start;
            return Fail("number out of range");
        }
        if (isInteger) {
            std::int64_t integer = 0;
            const auto intResult = std::from_chars(first, last, integer);
            if (intResult.ec == std::errc{} && intResult.ptr == last) {
                number.isInteger = true;
                number.integer = integer;
            }
        }
        out = Value(nullptr);
        out = MakeNumberValue(number);
        return true;
    }

    static Value MakeNumberValue(const Number& number) {
        if (number.isInteger) return Value(number.integer);
        return Value(number.value);
    }

    std::string_view text_;
    const ParseOptions& options_;
    std::size_t pos_ = 0;
    std::string error_;
    std::size_t errorPos_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Serializer
// ---------------------------------------------------------------------------------------------
void WriteString(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20 || c == 0x7F) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void WriteNewline(std::string& out, const SerializeOptions& options, int level) {
    if (!options.pretty) return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(level * options.indent), ' ');
}

void WriteValue(std::string& out, const Value& value, const SerializeOptions& options, int level) {
    switch (value.GetType()) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += *value.AsBool() ? "true" : "false"; break;
        case Type::Number: {
            char buffer[64];
            if (auto exact = value.AsInt64(); exact && std::abs(*value.AsDouble()) < 9007199254740992.0) {
                const auto r = std::to_chars(buffer, buffer + sizeof(buffer), *exact);
                out.append(buffer, r.ptr);
            } else {
                const auto r = std::to_chars(buffer, buffer + sizeof(buffer), *value.AsDouble());
                out.append(buffer, r.ptr);
            }
            break;
        }
        case Type::String: WriteString(out, *value.AsString()); break;
        case Type::Array: {
            const auto& array = *value.AsArray();
            out.push_back('[');
            for (std::size_t i = 0; i < array.size(); ++i) {
                if (i > 0) out.push_back(',');
                WriteNewline(out, options, level + 1);
                WriteValue(out, array[i], options, level + 1);
            }
            if (!array.empty()) WriteNewline(out, options, level);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            const auto& object = *value.AsObject();
            out.push_back('{');
            for (std::size_t i = 0; i < object.size(); ++i) {
                if (i > 0) out.push_back(',');
                WriteNewline(out, options, level + 1);
                WriteString(out, object[i].key);
                out += options.pretty ? ": " : ":";
                WriteValue(out, object[i].value, options, level + 1);
            }
            if (!object.empty()) WriteNewline(out, options, level);
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

Result<Value> Parse(std::string_view text, const ParseOptions& options) {
    Parser parser(text, options);
    return parser.Run();
}

std::string Serialize(const Value& value, const SerializeOptions& options) {
    std::string out;
    WriteValue(out, value, options, 0);
    if (options.pretty) out.push_back('\n');
    return out;
}

}  // namespace aether::json
