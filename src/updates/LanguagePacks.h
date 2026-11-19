// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D83: every language but English is a pack attached to the version's GitHub release
// (https://github.com/<owner>/<name>/releases/download/v<version>/AetherShell-language-<code>.json), downloaded when a
// person chooses the language (or Windows' language is followed) and kept in <data>/languages. The download follows
// the release-asset rules of the updater (official repository only, redirects to GitHub's download hosts only, bounded
// size); the pack is used and written only when its size and SHA-256 are this version's (i18n::UsePack).
#pragma once

#include "aether/BuildInfo.h"
#include "core/Cancellation.h"
#include "core/Http.h"
#include "core/Localization.h"
#include "core/Result.h"
#include "updates/UpdateTypes.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace aether::updates {

// Where this version's pack of `language` is published; empty when the language has none (English).
[[nodiscard]] std::string LanguagePackUrl(i18n::Language language, std::string_view version = build::kVersion,
                                          const GitHubRepository& repository = GitHubRepository::Official());

// Downloads `language`'s pack, checks it and puts it in use (i18n::UsePack), then writes it atomically to
// <languagesDir>/<pack file> for the next starts. Blocking: call from a worker thread.
//   Unavailable  the language has no pack in this version
//   Integrity    the bytes are not this version's pack (size or SHA-256)
//   Network, Timeout, Http, Cancelled  from the transfer
[[nodiscard]] Result<void> InstallLanguagePack(net::IHttpClient& http, i18n::Language language,
                                               const std::filesystem::path& languagesDir, const CancellationToken& cancel,
                                               std::string_view version = build::kVersion,
                                               const GitHubRepository& repository = GitHubRepository::Official());

}  // namespace aether::updates
