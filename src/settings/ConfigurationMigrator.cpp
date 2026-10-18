// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/ConfigurationMigrator.h"

namespace aether::settings {

Result<MigrationReport> MigrateConfiguration(const json::Value& document) {
    try {
        if (!document.IsObject()) return MakeError(ErrorCode::Parse, "settings must be a JSON object");
        MigrationReport report;
        report.document = document;
        if (const auto* version = document.Find("schemaVersion")) {
            const auto number = version->AsInt64();
            if (!number && version->AsDouble() && *version->AsDouble() > kCurrentSchemaVersion) {
                return MakeError(ErrorCode::Validation, "settings schema version cannot be interpreted safely");
            }
            if (!number || *number < 0) {
                report.warnings.emplace_back("invalid schemaVersion; treating as version 0");
            } else report.sourceVersion = *number;
        }
        report.newerSchema = report.sourceVersion > kCurrentSchemaVersion;
        if (report.sourceVersion == 0) {
            // Version 0 used the same optional groups but had no explicit version marker.
            report.document.Set("schemaVersion", 1);
            report.warnings.emplace_back("migrated unversioned settings to schema version 1 in memory");
        }
        if (report.newerSchema) report.warnings.emplace_back("newer schema preserved until an explicit user update");
        return report;
    } catch (...) { return MakeError(ErrorCode::Internal, "settings migration failed"); }
}

}  // namespace aether::settings
