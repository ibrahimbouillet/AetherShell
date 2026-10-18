// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Release notes come from the GitHub release body: untrusted, unsigned text.
// The UI renders plain text only (no Markdown, HTML or links are interpreted);
// this sanitizer makes the text safe and tidy for that:
//
//  * malformed UTF-8 becomes U+FFFD;
//  * CRLF and lone CR become LF; U+2028/U+2029 become LF;
//  * control characters are removed except LF and TAB (C0, DEL, C1), as are the invisible
//    formatting characters that can disguise text: bidirectional embeddings/overrides/isolates
//    (U+202A..U+202E, U+2066..U+2069), U+200E/U+200F marks, U+FEFF, U+FFF9..U+FFFB;
//  * trailing spaces/tabs are trimmed from each line, leading and trailing blank lines are
//    dropped and runs of blank lines collapse to a single blank line;
//  * the result is capped at `maxBytes` on a code-point boundary, ending with U+2026 when cut.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace aether::updates {

inline constexpr std::size_t kMaxSanitizedNotesBytes = 64 * 1024;

[[nodiscard]] std::string SanitizeReleaseNotes(std::string_view untrusted, std::size_t maxBytes = kMaxSanitizedNotesBytes);

}  // namespace aether::updates
