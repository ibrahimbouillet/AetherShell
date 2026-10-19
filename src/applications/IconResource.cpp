// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/IconResource.h"

#include "applications/ApplicationIdentity.h"
#include "core/Strings.h"

#include <algorithm>

namespace aether::apps {
namespace {

Error Rejected(const char* why) { return MakeError(ErrorCode::InvalidArgument, std::string("icon resource ") + why); }

bool StartsWithIgnoreCase(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && text::EqualsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

}  // namespace

Result<IconResource> ParseIconResource(std::wstring_view value) {
    // Messages never contain the value itself: it can name a user-profile folder.
    if (value.size() > kMaxIconResourceLength) return Rejected("is too long");
    const auto trimmed = text::Trim(value);
    if (trimmed.empty()) return Rejected("is empty");
    if (std::any_of(trimmed.begin(), trimmed.end(), [](wchar_t c) { return c < 0x20 || c == 0x7F; })) {
        return Rejected("contains control characters");
    }
    if (trimmed.find(L'"') != std::wstring_view::npos) return Rejected("is quoted");
    if (trimmed.front() == L'@' || StartsWithIgnoreCase(trimmed, L"ms-resource:")) {
        return Rejected("is an indirect string");
    }
    const auto comma = trimmed.find_last_of(L',');
    if (comma == std::wstring_view::npos) return Rejected("has no index");
    const auto indexText = text::Trim(trimmed.substr(comma + 1));
    if (indexText.empty() || indexText.size() > 6 ||
        !std::all_of(indexText.begin(), indexText.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        return Rejected("has an unsupported index");
    }
    int index = 0;
    for (const wchar_t digit : indexText) index = index * 10 + (digit - L'0');
    if (index != 0) return Rejected("has an unsupported index");

    auto path = NormalizeAbsoluteFilePath(text::Trim(trimmed.substr(0, comma)), L".ico");
    if (!path) return Rejected("does not name a local .ico file");
    return IconResource{std::move(path).value(), index};
}

}  // namespace aether::apps
