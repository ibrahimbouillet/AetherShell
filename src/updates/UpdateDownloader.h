// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Downloads release assets of the official repository.
//
// Installer downloads (Download):
//  * the URL must be an official release-download URL (IsReleaseDownloadUrl); redirects are allowed
//    to github.com, objects.githubusercontent.com and release-assets.githubusercontent.com only;
//  * free space is checked first: size * 3 + 50 MiB (download, staging and extraction), else
//    ErrorCode::InsufficientSpace. The query is injectable for tests;
//  * the body is streamed (IHttpClient::SendStreaming) to
//    <updatesDir>/downloads/<version>/<file>.partial while SHA-256 is computed, and the transfer is
//    aborted as soon as it exceeds the expected size. A body whose announced length differs from the
//    signed size is not written at all; it is judged once the status is known (error status first,
//    otherwise ErrorCode::Integrity), because transports stream error pages through the sink too;
//  * up to kMaxDownloadAttempts attempts (transport errors, 5xx, truncated bodies), each restarting
//    from byte zero after a cancellable back-off (2 s, then 8 s);
//  * on success size and digest are checked against the signed manifest, the optional Authenticode
//    policy is applied to the still-partial file, and only then is it renamed atomically to its
//    final name; on a size or digest mismatch the partial file is deleted and ErrorCode::Integrity
//    is returned (an Authenticode failure deletes it too and returns ErrorCode::Signature);
//  * a response whose finalUrl is not on an allowed host is rejected (ErrorCode::Validation);
//  * the final path is verified to stay inside <updatesDir>/downloads (fs::IsPathWithin);
//  * nothing is ever executed.
// An already present final file is reused only after the same size, digest and Authenticode checks;
// otherwise it is deleted and downloaded again.
//
// Blocking: call from worker threads only. Progress callbacks run on the calling thread.
#pragma once

#include "core/Cancellation.h"
#include "core/Http.h"
#include "core/Result.h"
#include "updates/Authenticode.h"
#include "updates/SemanticVersion.h"
#include "updates/Sha256.h"
#include "updates/UpdateTypes.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace aether::updates {

inline constexpr int kMaxDownloadAttempts = 3;
inline constexpr std::array<std::chrono::milliseconds, 2> kDownloadRetryDelays{std::chrono::seconds{2},
                                                                              std::chrono::seconds{8}};
inline constexpr std::uint64_t kDiskSpaceMarginBytes = 50ull * 1024 * 1024;
inline constexpr std::array<std::string_view, 3> kAssetRedirectHosts{
    "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"};
inline constexpr std::string_view kPartialSuffix = ".partial";

// Free bytes available on the volume that holds `path`.
using DiskSpaceQuery = std::function<Result<std::uint64_t>(const std::filesystem::path& path)>;
// Waits `delay`; returns false as soon as `cancel` is signalled.
using RetryWait = std::function<bool(std::chrono::milliseconds delay, const CancellationToken& cancel)>;
// Bytes received in the current attempt and the expected total.
using DownloadProgressCallback = std::function<void(std::uint64_t received, std::uint64_t total)>;

struct AssetDownloadRequest {
    std::string url;       // browser_download_url of the release asset
    std::string fileName;  // final file name (manifest file name)
    SemanticVersion version;
    std::uint64_t expectedSize = 0;
    Sha256Digest expectedSha256{};
};

struct UpdateDownloaderOptions {
    std::filesystem::path updatesDir;  // absolute; downloads go to <updatesDir>/downloads/<version>/
    std::string userAgent;             // empty: DefaultUserAgent()
    GitHubRepository repository = GitHubRepository::Official();
    DiskSpaceQuery diskSpace;          // empty: fs::AvailableDiskSpace
    RetryWait retryWait;               // empty: WaitCancellable
    AuthenticodePolicy authenticode = AuthenticodePolicy::NotRequired;
};

class UpdateDownloader {
public:
    // `http` must outlive the downloader.
    UpdateDownloader(net::IHttpClient& http, UpdateDownloaderOptions options);

    [[nodiscard]] std::filesystem::path DownloadsRoot() const;
    // <updatesDir>/downloads/<version>; InvalidArgument if it would leave the downloads root.
    [[nodiscard]] Result<std::filesystem::path> VersionDirectory(const SemanticVersion& version) const;

    // Returns the verified final path.
    [[nodiscard]] Result<std::filesystem::path> Download(const AssetDownloadRequest& request,
                                                         const DownloadProgressCallback& progress,
                                                         const CancellationToken& cancel);

    // Small asset (manifest, signature) into memory: same URL and redirect rules, HTTP 200 only,
    // at most `maxBytes`. No retries (the scheduler retries the whole check).
    [[nodiscard]] Result<std::string> FetchSmallAsset(std::string_view url, std::uint64_t maxBytes,
                                                      const CancellationToken& cancel);

    // Deletes <updatesDir>/downloads/<v> for every v other than `keep` (superseded or abandoned
    // updates). Only plain directories whose names are canonical semantic versions are touched;
    // links, junctions and other entries are left alone. Returns how many directories were removed.
    std::size_t PruneOtherVersions(const SemanticVersion& keep) const;

private:
    [[nodiscard]] net::HttpRequest MakeRequest(std::string_view url, std::uint64_t maxBytes) const;

    net::IHttpClient& http_;
    UpdateDownloaderOptions options_;
};

// size * 3 + kDiskSpaceMarginBytes (saturating).
[[nodiscard]] std::uint64_t RequiredDiskSpace(std::uint64_t assetSize) noexcept;

// Sleeps for `delay` unless `cancel` is signalled first; returns false when cancelled.
bool WaitCancellable(std::chrono::milliseconds delay, const CancellationToken& cancel);

// Maps a non-200 status of an asset download to an error (404 NotFound, 403/429 RateLimited,
// 5xx Unavailable, other Http).
[[nodiscard]] Error AssetHttpError(int status);

}  // namespace aether::updates
