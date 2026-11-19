// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/LanguagePacks.h"

#include "core/FileSystem.h"
#include "diagnostics/Logger.h"
#include "updates/AssetNaming.h"
#include "updates/UpdateDownloader.h"

namespace aether::updates {

std::string LanguagePackUrl(i18n::Language language, std::string_view version, const GitHubRepository& repository) {
    const auto& info = i18n::Info(language);
    if (info.packFile.empty() || !IsSafeAssetFileName(info.packFile) || version.empty()) return {};
    return repository.WebPrefix() + "releases/download/v" + std::string(version) + "/" + std::string(info.packFile);
}

Result<void> InstallLanguagePack(net::IHttpClient& http, i18n::Language language, const std::filesystem::path& languagesDir,
                                 const CancellationToken& cancel, std::string_view version, const GitHubRepository& repository) {
    const auto& info = i18n::Info(language);
    const auto url = LanguagePackUrl(language, version, repository);
    if (url.empty() || !i18n::Available(language)) return MakeError(ErrorCode::Unavailable, "no language pack in this version");
    UpdateDownloaderOptions options;
    options.updatesDir = languagesDir;   // FetchSmallAsset writes nothing there
    options.repository = repository;
    UpdateDownloader downloader(http, std::move(options));
    auto bytes = downloader.FetchSmallAsset(url, info.packBytes, cancel);
    if (!bytes) return bytes.error();
    // Size and SHA-256 first: nothing else of the download is read or written before they match this version's.
    if (auto used = i18n::UsePack(language, *bytes); !used) return used.error();
    if (auto created = fs::EnsureDirectory(languagesDir); !created) return created.error();
    const auto path = i18n::PackPath(language, languagesDir);
    if (path.empty() || !fs::IsPathWithin(languagesDir, path)) return MakeError(ErrorCode::InvalidArgument, "language pack path");
    if (auto written = fs::WriteFileAtomic(path, *bytes); !written) return written.error();
    AETHER_LOG_INFO("updates", "language pack {} installed ({} bytes)", info.code, bytes->size());
    return Ok();
}

}  // namespace aether::updates
