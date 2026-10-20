// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Read-only, fail-closed scan of the Windows taskbar's pin list ("Favorites" value of
// HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Taskband). The value's layout is
// undocumented and can change with any Windows update, so it is never interpreted: no PIDL or
// record framing is parsed and no byte of it is handed to a shell API. The scan only looks for
// strings that are already known to the caller, decoded as UTF-16LE at both byte parities:
//   * the long file names of the pinned shortcuts (.lnk files of the pinned-shortcut folder), whose
//     first positions give the Windows taskbar order;
//   * packaged AppUserModelIDs ("<PackageFamilyName>!<AppId>") that the caller confirms are
//     installed, for pins that have no shortcut file.
// Nothing is invented: a name that does not occur keeps no position, and an AUMID-like string the
// caller does not know is ignored. Pure; the blob, the names and the AUMIDs are never logged.
#pragma once

#include "core/Result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aether::apps {

inline constexpr std::size_t kDefaultMaxTaskbandBytes = 512u * 1024u;

struct TaskbandScan {
    // Parallel to the shortcut names passed in: the byte offset of the name's first occurrence, or
    // nullopt when it does not occur.
    std::vector<std::optional<std::size_t>> shortcutOffsets;
    // Confirmed packaged AppUserModelIDs with the byte offset of their first occurrence, in blob
    // order, each at most once (ignoring case), spelled as in the blob.
    std::vector<std::pair<std::size_t, std::wstring>> packaged;

    [[nodiscard]] std::size_t MatchedShortcuts() const noexcept;
};

// Matching ignores case (invariant lower-case mapping). A shortcut name is not matched where it is only
// the tail of a longer shortcut name that occurs at the same place ("a.lnk" inside "data.lnk").
// For packaged pins, every '!' is tried with bounded candidate starts and ends (family name at most
// 64 code units, AppId at most 64), and a candidate is accepted only when IsPackagedAumid holds and
// `isKnownPackagedAumid` confirms it; the longest confirmed candidate wins. A null predicate accepts
// none. A blob larger than `maxBytes` returns Unsupported with no partial result.
[[nodiscard]] Result<TaskbandScan> ScanTaskband(std::span<const std::uint8_t> blob,
                                                const std::vector<std::wstring>& shortcutFileNames,
                                                const std::function<bool(std::wstring_view)>& isKnownPackagedAumid,
                                                std::size_t maxBytes = kDefaultMaxTaskbandBytes);

}  // namespace aether::apps
