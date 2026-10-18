// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/SemanticVersion.h"

#include <algorithm>
#include <limits>

namespace aether::updates {
namespace {

bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }
bool IsIdentifierChar(char c) noexcept {
    return IsDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
}
bool IsNumeric(std::string_view identifier) noexcept {
    return !identifier.empty() && std::all_of(identifier.begin(), identifier.end(), IsDigit);
}

Error ParseError(std::string_view text, std::string_view reason) {
    // The input may be an untrusted tag; it is quoted only when short and printable.
    std::string shown;
    if (text.size() <= 64 && std::all_of(text.begin(), text.end(), [](char c) { return c >= 0x20 && c < 0x7F; })) {
        shown = "'" + std::string(text) + "': ";
    }
    return MakeError(ErrorCode::Parse, "invalid semantic version " + shown + std::string(reason));
}

// Splits on '.'; empty identifiers are kept so callers can reject them.
std::vector<std::string_view> SplitDots(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const auto dot = text.find('.', start);
        parts.push_back(text.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        if (dot == std::string_view::npos) return parts;
        start = dot + 1;
    }
}

Result<std::uint64_t> ParseNumericIdentifier(std::string_view full, std::string_view part, std::string_view what) {
    if (part.empty()) return ParseError(full, std::string(what) + " is missing");
    if (!IsNumeric(part)) return ParseError(full, std::string(what) + " must contain only digits");
    if (part.size() > 1 && part.front() == '0') return ParseError(full, std::string(what) + " has a leading zero");
    std::uint64_t value = 0;
    for (const char c : part) {
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return ParseError(full, std::string(what) + " does not fit in 64 bits");
        }
        value = value * 10 + digit;
    }
    return value;
}

std::weak_ordering CompareIdentifiers(const std::string& a, const std::string& b) noexcept {
    const bool aNumeric = IsNumeric(a);
    const bool bNumeric = IsNumeric(b);
    if (aNumeric && bNumeric) {
        // No leading zeros, so a longer numeral is a larger number; equal lengths compare digit-wise.
        // This works for numerals of any size without overflow.
        if (a.size() != b.size()) return a.size() < b.size() ? std::weak_ordering::less : std::weak_ordering::greater;
        const int c = a.compare(b);
        return c < 0 ? std::weak_ordering::less : (c > 0 ? std::weak_ordering::greater : std::weak_ordering::equivalent);
    }
    if (aNumeric) return std::weak_ordering::less;  // numeric identifiers have lower precedence
    if (bNumeric) return std::weak_ordering::greater;
    const int c = a.compare(b);  // ASCII order
    return c < 0 ? std::weak_ordering::less : (c > 0 ? std::weak_ordering::greater : std::weak_ordering::equivalent);
}

std::string Join(const std::vector<std::string>& parts) {
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty()) result += '.';
        result += part;
    }
    return result;
}

}  // namespace

Result<SemanticVersion> SemanticVersion::Parse(std::string_view text) {
    if (text.empty()) return ParseError(text, "empty text");
    if (text.size() > kMaxSemanticVersionLength) return ParseError(text, "text is too long");

    std::string_view rest = text;
    std::string_view build;
    bool hasBuild = false;
    if (const auto plus = rest.find('+'); plus != std::string_view::npos) {
        build = rest.substr(plus + 1);
        rest = rest.substr(0, plus);
        hasBuild = true;
    }
    std::string_view prerelease;
    bool hasPrerelease = false;
    if (const auto dash = rest.find('-'); dash != std::string_view::npos) {
        prerelease = rest.substr(dash + 1);
        rest = rest.substr(0, dash);
        hasPrerelease = true;
    }

    const auto core = SplitDots(rest);
    if (core.size() != 3) return ParseError(text, "expected MAJOR.MINOR.PATCH");
    SemanticVersion version;
    auto major = ParseNumericIdentifier(text, core[0], "major version");
    if (!major) return major.error();
    auto minor = ParseNumericIdentifier(text, core[1], "minor version");
    if (!minor) return minor.error();
    auto patch = ParseNumericIdentifier(text, core[2], "patch version");
    if (!patch) return patch.error();
    version.major_ = major.value();
    version.minor_ = minor.value();
    version.patch_ = patch.value();

    if (hasPrerelease) {
        for (const auto identifier : SplitDots(prerelease)) {
            if (identifier.empty()) return ParseError(text, "empty prerelease identifier");
            if (!std::all_of(identifier.begin(), identifier.end(), IsIdentifierChar)) {
                return ParseError(text, "prerelease identifiers may contain only [0-9A-Za-z-]");
            }
            if (IsNumeric(identifier) && identifier.size() > 1 && identifier.front() == '0') {
                return ParseError(text, "numeric prerelease identifier has a leading zero");
            }
            version.prerelease_.emplace_back(identifier);
        }
    }
    if (hasBuild) {
        for (const auto identifier : SplitDots(build)) {
            if (identifier.empty()) return ParseError(text, "empty build metadata identifier");
            if (!std::all_of(identifier.begin(), identifier.end(), IsIdentifierChar)) {
                return ParseError(text, "build metadata may contain only [0-9A-Za-z-]");
            }
            version.build_.emplace_back(identifier);
        }
    }
    return version;
}

Result<SemanticVersion> SemanticVersion::ParseTag(std::string_view tag) {
    if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) tag.remove_prefix(1);
    return Parse(tag);
}

std::string SemanticVersion::ToString() const {
    std::string text = std::to_string(major_) + "." + std::to_string(minor_) + "." + std::to_string(patch_);
    if (!prerelease_.empty()) text += "-" + Join(prerelease_);
    if (!build_.empty()) text += "+" + Join(build_);
    return text;
}

bool SemanticVersion::IsIdenticalTo(const SemanticVersion& other) const noexcept {
    return *this == other && build_ == other.build_;
}

std::weak_ordering operator<=>(const SemanticVersion& a, const SemanticVersion& b) noexcept {
    if (a.major_ != b.major_) return a.major_ < b.major_ ? std::weak_ordering::less : std::weak_ordering::greater;
    if (a.minor_ != b.minor_) return a.minor_ < b.minor_ ? std::weak_ordering::less : std::weak_ordering::greater;
    if (a.patch_ != b.patch_) return a.patch_ < b.patch_ ? std::weak_ordering::less : std::weak_ordering::greater;
    // A version without prerelease identifiers has higher precedence than one with them.
    if (a.prerelease_.empty() || b.prerelease_.empty()) {
        if (a.prerelease_.empty() && b.prerelease_.empty()) return std::weak_ordering::equivalent;
        return a.prerelease_.empty() ? std::weak_ordering::greater : std::weak_ordering::less;
    }
    const auto common = std::min(a.prerelease_.size(), b.prerelease_.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (const auto order = CompareIdentifiers(a.prerelease_[i], b.prerelease_[i]); order != 0) return order;
    }
    // All shared identifiers are equal: the larger set of fields has higher precedence.
    if (a.prerelease_.size() == b.prerelease_.size()) return std::weak_ordering::equivalent;
    return a.prerelease_.size() < b.prerelease_.size() ? std::weak_ordering::less : std::weak_ordering::greater;
}

}  // namespace aether::updates
