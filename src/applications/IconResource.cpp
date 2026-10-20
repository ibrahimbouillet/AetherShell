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
    // Checked before trimming: a line break or tab anywhere is not part of a declared resource.
    if (std::any_of(value.begin(), value.end(), [](wchar_t c) { return c < 0x20 || c == 0x7F; })) {
        return Rejected("contains control characters");
    }
    const auto trimmed = text::Trim(value);
    if (trimmed.empty()) return Rejected("is empty");
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

Result<void> ValidateIconResourceFile(const IconResource& resource) {
    try {
        if (resource.index != 0 || resource.path.size() > kMaxIconResourceLength) {
            return Rejected("has an unsupported file definition");
        }
        auto normalized = NormalizeAbsoluteFilePath(resource.path, L".ico");
        if (!normalized) return Rejected("does not name a local .ico file");
        const auto& path = normalized.value();
        const auto root = path.substr(0, 3);
        const UINT drive = ::GetDriveTypeW(root.c_str());
        if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_CDROM && drive != DRIVE_RAMDISK) {
            return MakeError(ErrorCode::Unsupported, "icon resource does not use an ordinary local drive");
        }

        // Check parents before children: querying only the leaf follows an ancestor junction and can
        // touch a remote target even though the leaf itself has no reparse attribute.
        std::size_t length = root.size();
        for (;;) {
            const bool leaf = length == path.size();
            const auto component = path.substr(0, length);
            const DWORD attributes = ::GetFileAttributesW(component.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                return MakeError(ErrorCode::NotFound, "icon resource file is unavailable");
            }
            if ((attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_OFFLINE |
                               FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) != 0) {
                return MakeError(ErrorCode::Unsupported, "icon resource file needs unsupported filesystem access");
            }
            const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (leaf) {
                if (directory) return MakeError(ErrorCode::Unsupported, "icon resource does not name a file");
                return Ok();
            }
            if (!directory) return MakeError(ErrorCode::NotFound, "icon resource parent is unavailable");
            const auto separator = path.find(L'\\', length == root.size() ? length : length + 1);
            length = separator == std::wstring::npos ? path.size() : separator;
        }
    } catch (...) {
        return MakeError(ErrorCode::Internal, "icon resource file could not be checked");
    }
}

}  // namespace aether::apps
