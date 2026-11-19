// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D68: files and folders for a search, from Windows' own search index (the one Start uses), by their names only,
// never their contents. Read-only: the typed text becomes a query of the index through Windows' query helper,
// nothing is executed, and neither the text nor any path is logged. Runs on a worker thread with COM initialized.
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Cancellation.h"
#include "core/Result.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::search {

// What a file is, by its extension: "Video", "Picture", "Music", "Document", "Archive" or "File"; "Folder" for one.
[[nodiscard]] std::wstring_view FileKindOf(std::wstring_view path, bool folder) noexcept;
// Shortcuts and programs are left to the applications (they would be found twice).
[[nodiscard]] bool IsApplicationFile(std::wstring_view path) noexcept;
// A found file as a search result: kind File, its absolute path as the parsing name, "Video in Downloads" as detail.
[[nodiscard]] apps::ApplicationInfo FileResult(std::wstring path, std::wstring name, bool folder, std::wstring_view parent);

// Up to `maxResults` files and folders of the index whose name matches `query`. Unavailable when Windows Search is
// off or the index cannot be read; the shell then simply shows no files.
[[nodiscard]] Result<std::vector<apps::ApplicationInfo>> FindFiles(std::wstring_view query, std::size_t maxResults, const CancellationToken& cancel);
// Pure (tests): the index query's restriction to `folders` (Windows paths), or nothing when there is none: then no
// file is searched at all, never the whole index (A06).
[[nodiscard]] std::wstring ScopeRestriction(std::span<const std::wstring> folders);

}  // namespace aether::search
