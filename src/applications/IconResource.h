// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Icon resource strings ("<file>,<index>") declared by running windows through the shell's
// System.AppUserModel.RelaunchIconResource window property, and carried in
// ApplicationInfo::iconResource. Parsing is pure; file validation runs only on a worker.
//
// The values can contain user-profile paths: never log them or write them to test output.
#pragma once

#include "core/Result.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace aether::apps {

inline constexpr std::size_t kMaxIconResourceLength = 1024;

struct IconResource {
    std::wstring path;  // validated, normalized absolute local path of an .ico file
    int index = 0;
};

// Accepted form: "<drive-absolute path>.ico,0" (the form browsers use for profile icons). The value is
// split at its last comma; the index is a decimal number and must be 0; the path must pass
// NormalizeAbsoluteFilePath(path, L".ico") with the default policy (no UNC paths).
// Rejected (InvalidArgument): empty values, values over kMaxIconResourceLength code units, control
// characters, quotes, indirect strings ('@...') and "ms-resource:" references, a missing or
// non-numeric index, negative or non-zero indexes, relative, device, object-manager and UNC paths,
// alternate data streams, and anything that is not an .ico file (executable and DLL resource
// indexes are not supported: the caller falls back to the generic application icon).
[[nodiscard]] Result<IconResource> ParseIconResource(std::wstring_view value);

// Worker-thread only. Checks an existing ordinary local .ico and every ancestor from the drive root
// before inspecting a child. Reparse points, mapped network drives and offline/recall-on-access
// entries are rejected before their descendants are touched. No file content is read or executed.
// This is a pathname preflight, not a guarantee against a concurrently replaced filesystem entry.
// All errors omit the path. The caller falls back to the generic application icon on failure.
[[nodiscard]] Result<void> ValidateIconResourceFile(const IconResource& resource);

}  // namespace aether::apps
