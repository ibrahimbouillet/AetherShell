// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/TaskbandScan.h"

#include "applications/ApplicationIdentity.h"
#include "core/Strings.h"

#include <algorithm>
#include <map>

namespace aether::apps {
namespace {

// Bounds of a packaged AppUserModelID (see IsPackagedAumid): "<Name>_<PublisherId>!<AppId>" with a
// 3..50 character name, a 13 character publisher id and a 1..64 character AppId.
constexpr std::size_t kMinFamilyName = 3 + 1 + 13;
constexpr std::size_t kMaxFamilyName = 50 + 1 + 13;
constexpr std::size_t kMaxAppId = 64;

bool IsAsciiAlphanumeric(wchar_t c) noexcept {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
}
bool IsFamilyNameChar(wchar_t c) noexcept { return IsAsciiAlphanumeric(c) || c == L'.' || c == L'-' || c == L'_'; }
bool IsAppIdChar(wchar_t c) noexcept { return IsAsciiAlphanumeric(c) || c == L'.' || c == L'-' || c == L'_'; }

// UTF-16LE code units starting at byte `parity`; code unit i lies at byte parity + 2 * i.
std::wstring Decode(std::span<const std::uint8_t> blob, std::size_t parity) {
    std::wstring result;
    if (blob.size() <= parity) return result;
    const std::size_t units = (blob.size() - parity) / 2;
    result.resize(units);
    for (std::size_t i = 0; i < units; ++i) {
        const std::size_t at = parity + 2 * i;
        result[i] = static_cast<wchar_t>(static_cast<unsigned>(blob[at]) | (static_cast<unsigned>(blob[at + 1]) << 8));
    }
    return result;
}

std::wstring FoldAscii(std::wstring_view value) {
    std::wstring result(value);
    for (auto& c : result) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return result;
}

// Invariant lower case that keeps every code unit in place (offsets must not move); ASCII-only
// folding when the full mapping would change the length.
std::wstring Fold(std::wstring_view value) {
    auto lowered = text::ToLower(value);
    if (lowered.size() == value.size()) return lowered;
    return FoldAscii(value);
}

std::optional<std::size_t> FirstShortcutOccurrence(const std::wstring& text, const std::vector<std::wstring>& names,
                                                   std::size_t which) {
    const std::wstring& name = names[which];
    std::size_t position = text.find(name);
    while (position != std::wstring::npos) {
        bool tailOfLonger = false;
        for (std::size_t other = 0; other < names.size() && !tailOfLonger; ++other) {
            const std::wstring& longer = names[other];
            if (other == which || longer.size() <= name.size() || !longer.ends_with(name)) continue;
            const std::size_t extra = longer.size() - name.size();
            tailOfLonger = position >= extra && text.compare(position - extra, longer.size(), longer) == 0;
        }
        if (!tailOfLonger) return position;
        position = text.find(name, position + 1);
    }
    return std::nullopt;
}

// The longest confirmed packaged AUMID whose '!' is at `bang`, as [start, end) in `text`.
std::optional<std::pair<std::size_t, std::size_t>> PackagedAt(
    const std::wstring& text, std::size_t bang, const std::function<bool(std::wstring_view)>& isKnown) {
    std::size_t left = 0;
    while (left < kMaxFamilyName && left < bang && IsFamilyNameChar(text[bang - 1 - left])) ++left;
    std::size_t right = 0;
    while (right < kMaxAppId && bang + 1 + right < text.size() && IsAppIdChar(text[bang + 1 + right])) ++right;
    if (left < kMinFamilyName || right == 0) return std::nullopt;

    std::optional<std::pair<std::size_t, std::size_t>> best;
    const std::wstring_view view(text);
    for (std::size_t familyLength = left; familyLength >= kMinFamilyName; --familyLength) {
        const std::size_t start = bang - familyLength;
        for (std::size_t appIdLength = right; appIdLength >= 1; --appIdLength) {
            const std::size_t end = bang + 1 + appIdLength;
            if (best && end - start <= best->second - best->first) break;  // cannot beat the best
            const auto candidate = view.substr(start, end - start);
            if (IsPackagedAumid(candidate) && isKnown(candidate)) {
                best.emplace(start, end);
                break;  // the longest AppId for this start
            }
        }
    }
    return best;
}

}  // namespace

std::size_t TaskbandScan::MatchedShortcuts() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(shortcutOffsets.begin(), shortcutOffsets.end(), [](const auto& offset) { return offset.has_value(); }));
}

Result<TaskbandScan> ScanTaskband(std::span<const std::uint8_t> blob, const std::vector<std::wstring>& shortcutFileNames,
                                  const std::function<bool(std::wstring_view)>& isKnownPackagedAumid,
                                  std::size_t maxBytes) {
    if (blob.size() > maxBytes) return MakeError(ErrorCode::Unsupported, "the taskbar pin list exceeds the size limit");
    try {
        TaskbandScan scan;
        scan.shortcutOffsets.assign(shortcutFileNames.size(), std::nullopt);
        std::vector<std::wstring> names;
        names.reserve(shortcutFileNames.size());
        for (const auto& name : shortcutFileNames) names.push_back(Fold(text::Trim(name)));

        std::map<std::wstring, std::pair<std::size_t, std::wstring>> packaged;  // folded AUMID -> (offset, spelling)
        for (const std::size_t parity : {std::size_t{0}, std::size_t{1}}) {
            const std::wstring raw = Decode(blob, parity);
            if (raw.empty()) continue;
            const std::wstring folded = Fold(raw);
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (names[i].empty()) continue;
                if (const auto position = FirstShortcutOccurrence(folded, names, i)) {
                    const std::size_t offset = parity + 2 * *position;
                    auto& current = scan.shortcutOffsets[i];
                    if (!current || offset < *current) current = offset;
                }
            }
            if (!isKnownPackagedAumid) continue;
            for (std::size_t bang = raw.find(L'!'); bang != std::wstring::npos; bang = raw.find(L'!', bang + 1)) {
                const auto found = PackagedAt(raw, bang, isKnownPackagedAumid);
                if (!found) continue;
                const std::size_t offset = parity + 2 * found->first;
                std::wstring spelling = raw.substr(found->first, found->second - found->first);
                auto key = FoldAscii(spelling);
                const auto existing = packaged.find(key);
                if (existing == packaged.end()) {
                    packaged.emplace(std::move(key), std::make_pair(offset, std::move(spelling)));
                } else if (offset < existing->second.first) {
                    existing->second = std::make_pair(offset, std::move(spelling));
                }
            }
        }
        for (auto& entry : packaged) scan.packaged.push_back(std::move(entry.second));
        std::sort(scan.packaged.begin(), scan.packaged.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        return scan;
    } catch (...) {
        return MakeError(ErrorCode::Internal, "the taskbar pin list could not be scanned");
    }
}

}  // namespace aether::apps
