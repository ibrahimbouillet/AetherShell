// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/UpdateDownloader.h"

#include "core/FileSystem.h"
#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "platform/windows/Handles.h"
#include "platform/windows/Win32Error.h"
#include "updates/AssetNaming.h"
#include "updates/GitHubReleaseClient.h"
#include "updates/UpdateManifest.h"

#include <windows.h>

#include <condition_variable>
#include <format>
#include <limits>
#include <mutex>
#include <optional>

namespace aether::updates {
namespace {

constexpr std::uint64_t kMiB = 1024 * 1024;

// The .partial file of one attempt. Deleted on destruction unless the caller keeps it (Keep()).
class PartialFile {
public:
    explicit PartialFile(std::filesystem::path path) : path_(std::move(path)) {}
    PartialFile(const PartialFile&) = delete;
    PartialFile& operator=(const PartialFile&) = delete;
    ~PartialFile() { Discard(); }

    Result<void> Open() {
        // CREATE_ALWAYS: every attempt restarts from byte zero. Readers may look, nobody may write.
        handle_.Reset(::CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!handle_) return platform::LastWin32Error("cannot create the partial download file");
        return Ok();
    }

    Result<void> Write(std::span<const std::byte> data) {
        while (!data.empty()) {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(data.size(), 1u << 20));
            DWORD written = 0;
            if (!::WriteFile(handle_.Get(), data.data(), chunk, &written, nullptr) || written == 0) {
                return platform::LastWin32Error("cannot write the partial download file");
            }
            data = data.subspan(written);
        }
        return Ok();
    }

    Result<void> FlushAndClose() {
        if (!::FlushFileBuffers(handle_.Get())) return platform::LastWin32Error("cannot flush the download");
        handle_.Reset();
        return Ok();
    }

    void Keep() noexcept { keep_ = true; }

    void Discard() noexcept {
        handle_.Reset();
        if (!keep_) ::DeleteFileW(path_.c_str());
    }

private:
    std::filesystem::path path_;
    platform::UniqueFileHandle handle_;
    bool keep_ = false;
};

bool IsRetryable(const Error& error) noexcept {
    return error.code == ErrorCode::Network || error.code == ErrorCode::Timeout || error.code == ErrorCode::Unavailable;
}

std::string Megabytes(std::uint64_t bytes) { return std::format("{:.1f} MiB", static_cast<double>(bytes) / kMiB); }

Result<void> CheckFinalUrl(const net::HttpResponse& response) {
    if (!response.finalUrl.empty() && !IsHttpsUrlOnHost(response.finalUrl, kAssetRedirectHosts)) {
        // The transport enforces allowedRedirectHosts; reaching this means it did not.
        return MakeError(ErrorCode::Validation, "the release asset was served by an untrusted host");
    }
    return Ok();
}

// Ok(true) when `file` already holds the expected bytes and passes the Authenticode policy. Ok(false)
// when there is nothing reusable; a stale file is deleted so the download can take its place.
Result<bool> ReuseExisting(const std::filesystem::path& file, const AssetDownloadRequest& request,
                           AuthenticodePolicy policy, const CancellationToken& cancel) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(file, ec);
    if (status.type() == std::filesystem::file_type::not_found) return false;
    if (ec) return MakeError(ErrorCode::Io, "cannot inspect an existing download", ec.value());
    if (status.type() == std::filesystem::file_type::regular) {
        const auto size = std::filesystem::file_size(file, ec);
        if (!ec && size == request.expectedSize) {
            auto digest = Sha256::HashFile(file, cancel);
            if (!digest && digest.error().code == ErrorCode::Cancelled) return digest.error();
            if (digest && *digest == request.expectedSha256 && EnforceAuthenticodePolicy(file, policy).ok()) {
                return true;
            }
        }
    }
    if (!std::filesystem::remove(file, ec) || ec) {
        return MakeError(ErrorCode::Io, "cannot remove a stale download", ec.value());
    }
    return false;
}

}  // namespace

std::uint64_t RequiredDiskSpace(std::uint64_t assetSize) noexcept {
    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
    if (assetSize > (kMax - kDiskSpaceMarginBytes) / 3) return kMax;
    return assetSize * 3 + kDiskSpaceMarginBytes;
}

bool WaitCancellable(std::chrono::milliseconds delay, const CancellationToken& cancel) {
    if (cancel.IsCancelled()) return false;
    std::mutex mutex;
    std::condition_variable condition;
    bool cancelled = false;
    auto registration = cancel.OnCancel([&] {
        std::lock_guard lock(mutex);
        cancelled = true;
        condition.notify_all();
    });
    std::unique_lock lock(mutex);
    condition.wait_for(lock, delay, [&] { return cancelled; });
    return !cancelled;
}

Error AssetHttpError(int status) {
    if (status == 404) return MakeError(ErrorCode::NotFound, "the release asset was not found (HTTP 404)", status);
    if (status == 403 || status == 429) {
        return MakeError(ErrorCode::RateLimited, std::format("GitHub refused the download (HTTP {})", status), status);
    }
    if (status >= 500 && status <= 599) {
        return MakeError(ErrorCode::Unavailable, std::format("the download server is unavailable (HTTP {})", status),
                         status);
    }
    return MakeError(ErrorCode::Http, std::format("unexpected HTTP status {} for a release asset", status), status);
}

UpdateDownloader::UpdateDownloader(net::IHttpClient& http, UpdateDownloaderOptions options)
    : http_(http), options_(std::move(options)) {
    if (options_.userAgent.empty()) options_.userAgent = DefaultUserAgent();
    if (!options_.diskSpace) {
        options_.diskSpace = [](const std::filesystem::path& path) { return fs::AvailableDiskSpace(path); };
    }
    if (!options_.retryWait) options_.retryWait = WaitCancellable;
}

std::filesystem::path UpdateDownloader::DownloadsRoot() const {
    return (options_.updatesDir / L"downloads").lexically_normal();
}

Result<std::filesystem::path> UpdateDownloader::VersionDirectory(const SemanticVersion& version) const {
    if (!options_.updatesDir.is_absolute()) {
        return MakeError(ErrorCode::InvalidArgument, "the updates directory must be an absolute path");
    }
    const auto versionText = version.ToString();
    if (!IsSafeAssetFileName(versionText)) return MakeError(ErrorCode::InvalidArgument, "unusable version text");
    auto directory = DownloadsRoot() / text::Widen(versionText);
    if (!fs::IsPathWithin(DownloadsRoot(), directory)) {
        return MakeError(ErrorCode::InvalidArgument, "the version directory escapes the downloads directory");
    }
    return directory;
}

net::HttpRequest UpdateDownloader::MakeRequest(std::string_view url, std::uint64_t maxBytes) const {
    net::HttpRequest request;
    request.url = std::string(url);
    request.headers = {{"Accept", "application/octet-stream"}, {"User-Agent", options_.userAgent}};
    request.maxResponseBytes = maxBytes;
    for (const auto host : kAssetRedirectHosts) request.allowedRedirectHosts.emplace_back(host);
    return request;
}

Result<std::string> UpdateDownloader::FetchSmallAsset(std::string_view url, std::uint64_t maxBytes,
                                                     const CancellationToken& cancel) {
    if (!IsReleaseDownloadUrl(url, options_.repository)) {
        return MakeError(ErrorCode::InvalidArgument, "not a release download URL of the official repository");
    }
    auto response = http_.Send(MakeRequest(url, maxBytes), cancel);
    if (!response) return response.error();
    if (auto origin = CheckFinalUrl(*response); !origin) return origin.error();
    if (response->status != 200) return AssetHttpError(response->status);
    if (response->body.size() > maxBytes) return MakeError(ErrorCode::Integrity, "the release asset is too large");
    return std::move(response->body);
}

std::size_t UpdateDownloader::PruneOtherVersions(const SemanticVersion& keep) const {
    const auto root = DownloadsRoot();
    std::error_code ec;
    std::filesystem::directory_iterator it(root, ec);
    if (ec) return 0;  // no downloads directory yet
    std::size_t removed = 0;
    for (const std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;
        // symlink_status: a junction or symbolic link is never followed or removed.
        if (entry.symlink_status(ec).type() != std::filesystem::file_type::directory || ec) continue;
        const auto name = text::Narrow(entry.path().filename().native());
        auto version = SemanticVersion::Parse(name);
        if (!version || version->ToString() != name || version->IsIdenticalTo(keep)) continue;
        if (!fs::IsPathWithin(root, entry.path())) continue;
        std::error_code removeError;
        std::filesystem::remove_all(entry.path(), removeError);
        if (removeError) {
            AETHER_LOG_WARN(kLogComponent, "cannot remove the old download directory {}: {}", name,
                            removeError.message());
        } else {
            ++removed;
        }
    }
    if (removed > 0) AETHER_LOG_INFO(kLogComponent, "removed {} superseded download director(ies)", removed);
    return removed;
}

Result<std::filesystem::path> UpdateDownloader::Download(const AssetDownloadRequest& request,
                                                         const DownloadProgressCallback& progress,
                                                         const CancellationToken& cancel) {
    if (!IsReleaseDownloadUrl(request.url, options_.repository)) {
        return MakeError(ErrorCode::InvalidArgument, "not a release download URL of the official repository");
    }
    if (!IsSafeAssetFileName(request.fileName)) return MakeError(ErrorCode::InvalidArgument, "unsafe file name");
    if (request.expectedSize == 0 || request.expectedSize > kMaxInstallerBytes) {
        return MakeError(ErrorCode::InvalidArgument, "the expected size is out of range");
    }
    auto directory = VersionDirectory(request.version);
    if (!directory) return directory.error();
    const auto finalPath = *directory / text::Widen(request.fileName);
    auto partialPath = finalPath;
    partialPath += text::Widen(kPartialSuffix);
    if (!fs::IsPathWithin(DownloadsRoot(), finalPath) || !fs::IsPathWithin(DownloadsRoot(), partialPath)) {
        return MakeError(ErrorCode::InvalidArgument, "the download path escapes the downloads directory");
    }
    if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "the download was cancelled");
    if (auto created = fs::EnsureDirectory(*directory); !created) return created.error();

    // A complete download from an earlier session is re-verified, never trusted as is.
    auto reused = ReuseExisting(finalPath, request, options_.authenticode, cancel);
    if (!reused) return reused.error();
    if (*reused) {
        AETHER_LOG_INFO(kLogComponent, "reusing verified download {}", request.fileName);
        if (progress) progress(request.expectedSize, request.expectedSize);
        return finalPath;
    }

    auto available = options_.diskSpace(*directory);
    if (!available) return available.error();
    const auto required = RequiredDiskSpace(request.expectedSize);
    if (*available < required) {
        return MakeError(ErrorCode::InsufficientSpace,
                         std::format("the update needs {} of free disk space, {} are available", Megabytes(required),
                                     Megabytes(*available)));
    }

    const auto httpRequest = MakeRequest(request.url, request.expectedSize);
    for (int attempt = 1;; ++attempt) {
        if (attempt > 1) {
            const auto delay = kDownloadRetryDelays[std::min<std::size_t>(static_cast<std::size_t>(attempt - 2),
                                                                          kDownloadRetryDelays.size() - 1)];
            if (!options_.retryWait(delay, cancel)) return MakeError(ErrorCode::Cancelled, "the download was cancelled");
        }

        PartialFile file(partialPath);
        if (auto opened = file.Open(); !opened) return opened.error();
        auto hasher = Sha256::Create();
        if (!hasher) return hasher.error();
        std::uint64_t received = 0;
        std::optional<std::uint64_t> announced;
        if (progress) progress(0, request.expectedSize);

        // The transport streams the body of every status (including error pages) into the sink, and the
        // status is only known once SendStreaming returns. A body whose announced length contradicts
        // the signed size is therefore discarded without being written, and judged afterwards: an error
        // status wins (a 503 page is "unavailable", not "corrupt"), a 200 is an integrity failure.
        const auto lengthContradicts = [&] { return announced && *announced != request.expectedSize; };
        const net::HttpChunkSink sink = [&](std::span<const std::byte> chunk) -> Result<void> {
            if (lengthContradicts()) return Ok();
            // Abort as soon as the body would exceed the signed size; never write the excess.
            if (chunk.size() > request.expectedSize - received) {
                return MakeError(ErrorCode::Integrity, "the download is larger than the size in the signed manifest");
            }
            if (auto written = file.Write(chunk); !written) return written.error();
            if (auto hashed = hasher->Update(chunk); !hashed) return hashed.error();
            received += chunk.size();
            if (progress) progress(received, request.expectedSize);
            return Ok();
        };
        const net::HttpProgress onProgress = [&](std::uint64_t, std::optional<std::uint64_t> total) {
            if (total) announced = total;
        };

        auto outcome = [&]() -> Result<void> {
            auto response = http_.SendStreaming(httpRequest, sink, onProgress, cancel);
            if (!response) return response.error();
            if (auto origin = CheckFinalUrl(*response); !origin) return origin.error();
            if (response->status != 200) return AssetHttpError(response->status);
            if (lengthContradicts()) {
                return MakeError(ErrorCode::Integrity,
                                 std::format("the server announced {} bytes but the manifest says {}", *announced,
                                             request.expectedSize));
            }
            if (received != request.expectedSize) {
                return MakeError(ErrorCode::Network, std::format("incomplete download: {} of {} bytes", received,
                                                                 request.expectedSize));
            }
            auto digest = hasher->Finish();
            if (!digest) return digest.error();
            if (*digest != request.expectedSha256) {
                return MakeError(ErrorCode::Integrity, "the SHA-256 of the download does not match the signed manifest");
            }
            return file.FlushAndClose();
        }();

        if (outcome) {
            file.Keep();
            break;
        }
        file.Discard();
        const auto& error = outcome.error();
        if (!IsRetryable(error) || attempt >= kMaxDownloadAttempts || cancel.IsCancelled()) {
            AETHER_LOG_WARN(kLogComponent, "download of {} failed: {}", request.fileName, error.Describe());
            return error;
        }
        AETHER_LOG_WARN(kLogComponent, "download attempt {} of {} failed, retrying: {}", attempt, kMaxDownloadAttempts,
                        error.Describe());
    }

    // Size and digest match the signed manifest. The optional Authenticode policy is applied while the
    // file still has its .partial name, so a file under the final name has always passed every check.
    // (WinVerifyTrust identifies a PE file by its content, not by its extension.)
    if (auto trusted = EnforceAuthenticodePolicy(partialPath, options_.authenticode); !trusted) {
        ::DeleteFileW(partialPath.c_str());
        AETHER_LOG_WARN(kLogComponent, "download of {} rejected: {}", request.fileName, trusted.error().Describe());
        return trusted.error();
    }
    if (!::MoveFileExW(partialPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        auto error = platform::LastWin32Error("cannot move the verified download into place");
        ::DeleteFileW(partialPath.c_str());
        return error;
    }
    AETHER_LOG_INFO(kLogComponent, "downloaded and verified {} ({} bytes)", request.fileName, request.expectedSize);
    return finalPath;
}

}  // namespace aether::updates
