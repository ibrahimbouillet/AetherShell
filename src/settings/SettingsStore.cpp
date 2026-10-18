// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/SettingsStore.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "settings/ConfigurationMigrator.h"
#include "settings/SettingsSerialization.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <optional>
#include <utility>

namespace aether::settings {
namespace {

constexpr std::uint64_t kMaxSettingsBytes = 1024 * 1024;

Error FileError(std::string message, const std::error_code& error) {
    return MakeError(ErrorCode::Io, std::move(message), error.value());
}

std::wstring UtcSuffix() {
    SYSTEMTIME now{};
    ::GetSystemTime(&now);
    static std::atomic<unsigned> sequence{0};
    wchar_t text[80]{};
    std::swprintf(text, std::size(text), L"%04u%02u%02uT%02u%02u%02u%03uZ-%08u", now.wYear, now.wMonth,
        now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, sequence.fetch_add(1));
    return text;
}

class FileSettingsStore final : public ISettingsStore {
public:
    explicit FileSettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

    Result<LoadedSettings> Load() override {
        try {
            safeToOverwrite_ = false;
            originalNewer_.reset();
            auto input = fs::ReadFile(path_, kMaxSettingsBytes);
            if (!input && input.error().code == ErrorCode::NotFound) {
                safeToOverwrite_ = true;
                return LoadedSettings{};
            }
            if (!input) {
                // Even an unreadable file is preserved through a rename before defaults may replace
                // it. A failed preservation leaves saving blocked until Load succeeds.
                return Recover();
            }
            json::ParseOptions options;
            options.maxInputBytes = kMaxSettingsBytes;
            options.maxDepth = 16;
            auto parsed = json::Parse(*input, options);
            if (!parsed || !parsed->IsObject()) return Recover();
            auto migrated = MigrateConfiguration(*parsed);
            if (!migrated) return migrated.error();
            auto settings = FromJson(migrated->document);
            if (!settings) return settings.error();
            LoadedSettings loaded;
            loaded.value = std::move(settings->value);
            loaded.report.warnings = std::move(migrated->warnings);
            loaded.report.warnings.insert(loaded.report.warnings.end(), settings->warnings.begin(), settings->warnings.end());
            loaded.report.status = migrated->newerSchema ? LoadStatus::NewerSchema : LoadStatus::Loaded;
            if (migrated->newerSchema) {
                originalNewer_ = *input;
                newerVersion_ = migrated->sourceVersion;
            }
            safeToOverwrite_ = true;
            return loaded;
        } catch (...) { return MakeError(ErrorCode::Io, "settings could not be loaded safely"); }
    }

    Result<void> Save(const Settings& settings, bool reset) override {
        try {
            if (!safeToOverwrite_) return MakeError(ErrorCode::InvalidState, "settings must be loaded and preserved before saving");
            if (originalNewer_) {
                const auto backup = path_.parent_path() / (L"settings.v" + std::to_wstring(newerVersion_) + L".backup.json");
                // Preserve an earlier backup too; a later rollback must not erase its only copy.
                auto existing = fs::ReadFile(backup, kMaxSettingsBytes);
                if (existing && *existing != *originalNewer_) {
                    const auto extra = path_.parent_path() / (L"settings.v" + std::to_wstring(newerVersion_) +
                        L".backup-" + UtcSuffix() + L".json");
                    if (auto kept = fs::WriteFileAtomic(extra, *existing); !kept) return kept;
                } else if (!existing && existing.error().code != ErrorCode::NotFound) return existing.error();
                if (auto kept = fs::WriteFileAtomic(backup, *originalNewer_); !kept) return kept;
            }
            if (reset) {
                auto original = fs::ReadFile(path_, kMaxSettingsBytes);
                if (original) {
                    auto backup = path_;
                    backup += L".bak";
                    if (auto kept = fs::WriteFileAtomic(backup, *original); !kept) return kept;
                } else if (original.error().code != ErrorCode::NotFound) return original.error();
            }
            auto document = ToJson(settings);
            if (!document) return document.error();
            const auto bytes = json::Serialize(*document, json::SerializeOptions{true, 2}) + "\n";
            if (bytes.size() > kMaxSettingsBytes) return MakeError(ErrorCode::Integrity, "serialized settings exceed the one MiB file limit");
            auto saved = fs::WriteFileAtomic(path_, bytes);
            if (saved) originalNewer_.reset();
            return saved;
        } catch (...) { return MakeError(ErrorCode::Io, "settings could not be persisted safely"); }
    }

private:
    Result<LoadedSettings> Recover() {
        std::error_code error;
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path_, error)) || error) {
            return MakeError(ErrorCode::Io, "unreadable settings are not a regular file; left unchanged");
        }
        auto backup = path_;
        backup += L".corrupt-" + UtcSuffix();
        std::filesystem::rename(path_, backup, error);
        if (error) return FileError("unreadable settings could not be preserved", error);
        safeToOverwrite_ = true;
        LoadedSettings loaded;
        loaded.report.status = LoadStatus::RecoveredFromCorruption;
        loaded.report.warnings.emplace_back("unreadable or corrupt settings preserved; using defaults");
        std::vector<std::filesystem::path> backups;
        const auto prefix = path_.filename().wstring() + L".corrupt-";
        for (std::filesystem::directory_iterator entry(path_.parent_path(), error), end; !error && entry != end; entry.increment(error)) {
            const auto name = entry->path().filename().wstring();
            if (name.starts_with(prefix) && entry->is_regular_file(error)) backups.push_back(entry->path());
        }
        if (error) loaded.report.warnings.emplace_back("could not enumerate corruption backups");
        std::sort(backups.begin(), backups.end());
        while (backups.size() > 3) {
            std::filesystem::remove(backups.front(), error);
            if (error) { loaded.report.warnings.emplace_back("could not prune an old corruption backup"); break; }
            backups.erase(backups.begin());
        }
        return loaded;
    }

    std::filesystem::path path_;
    bool safeToOverwrite_ = false;
    std::int64_t newerVersion_ = 0;
    std::optional<std::string> originalNewer_;
};

}  // namespace

Result<std::shared_ptr<ISettingsStore>> MakeFileSettingsStore(std::filesystem::path path) {
    try {
        if (path.empty() || !path.is_absolute() || path.filename().empty()) {
            return MakeError(ErrorCode::InvalidArgument, "settings file must have an absolute path");
        }
        return std::shared_ptr<ISettingsStore>(std::make_shared<FileSettingsStore>(std::move(path)));
    } catch (...) { return MakeError(ErrorCode::Internal, "settings store could not be created"); }
}

}  // namespace aether::settings
