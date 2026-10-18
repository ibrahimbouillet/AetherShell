// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace aether::fs {

// Reads a whole file. Fails with ErrorCode::Integrity if it is larger than `maxBytes`,
// ErrorCode::NotFound if it does not exist.
[[nodiscard]] Result<std::string> ReadFile(const std::filesystem::path& path, std::uint64_t maxBytes);

// Crash-safe replacement of a file: writes a temporary sibling, flushes it to disk, then atomically
// renames it over the destination. Readers observe either the old or the new content, never a mix.
[[nodiscard]] Result<void> WriteFileAtomic(const std::filesystem::path& path, std::string_view contents);

// Creates the directory and its parents if needed.
[[nodiscard]] Result<void> EnsureDirectory(const std::filesystem::path& path);

// Free bytes available to the current user on the volume containing `path`.
[[nodiscard]] Result<std::uint64_t> AvailableDiskSpace(const std::filesystem::path& path);

// True when `candidate` (after normalization) is inside `root`. Used to reject path traversal.
[[nodiscard]] bool IsPathWithin(const std::filesystem::path& root, const std::filesystem::path& candidate);

}  // namespace aether::fs
