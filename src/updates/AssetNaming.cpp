// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/AssetNaming.h"

#include "core/Strings.h"

#include <algorithm>
#include <array>

namespace aether::updates {
namespace {

constexpr std::array kArchitectures{Architecture::X64, Architecture::Arm64};

bool IsAllowedFileNameChar(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_' ||
           c == '-' || c == '+';
}

// CON, PRN, AUX, NUL, COM0-9, LPT0-9 (with or without an extension) name devices on Windows.
bool IsReservedDeviceName(std::string_view fileName) {
    const auto stem = text::ToLowerAscii(fileName.substr(0, fileName.find('.')));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '0' &&
           stem[3] <= '9';
}

}  // namespace

std::string InstallerAssetName(const SemanticVersion& version, Architecture architecture) {
    return std::string(kAssetProductPrefix) + "-" + version.ToString() + "-win-" + std::string(ToString(architecture)) +
           "-setup.exe";
}

std::string ManifestAssetName(const SemanticVersion& version) {
    return std::string(kAssetProductPrefix) + "-" + version.ToString() + "-manifest.json";
}

std::string SignatureAssetName(const SemanticVersion& version) { return ManifestAssetName(version) + ".sig"; }

std::string ChecksumsAssetName(const SemanticVersion& version) {
    return std::string(kAssetProductPrefix) + "-" + version.ToString() + "-checksums.txt";
}

std::optional<Architecture> MatchInstallerAssetName(std::string_view fileName, const SemanticVersion& version) {
    for (const auto architecture : kArchitectures) {
        if (fileName == InstallerAssetName(version, architecture)) return architecture;
    }
    return std::nullopt;
}

bool IsSafeAssetFileName(std::string_view fileName) noexcept {
    if (fileName.empty() || fileName.size() > kMaxAssetFileNameLength) return false;
    if (!std::all_of(fileName.begin(), fileName.end(), IsAllowedFileNameChar)) return false;
    if (fileName.front() == '.' || fileName.back() == '.') return false;  // ".", "..", hidden, trailing-dot aliasing
    if (fileName.find("..") != std::string_view::npos) return false;
    try {
        return !IsReservedDeviceName(fileName);
    } catch (...) {
        return false;  // allocation failure: refuse rather than guess
    }
}

}  // namespace aether::updates
