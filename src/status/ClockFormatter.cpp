// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/ClockFormatter.h"

#include "platform/windows/Win32Error.h"
#include "status/ClockTime.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace aether::status {
namespace {

// --- picture tokenizer ------------------------------------------------------------------------

struct Token {
    enum class Kind { Hour12, Hour24, Marker, Space, Literal };
    Kind kind;
    std::wstring text;
};

bool IsPictureSpace(wchar_t c) noexcept {
    return c == L' ' || c == L'\u00A0' || c == L'\u202F';  // space, no-break space, narrow no-break space
}

std::vector<Token> Tokenize(std::wstring_view picture) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < picture.size()) {
        const wchar_t c = picture[i];
        std::size_t end = i + 1;
        Token::Kind kind = Token::Kind::Literal;
        if (c == L'\'') {
            // Quoted literal up to the closing quote; a doubled quote inside is an escaped quote.
            while (end < picture.size()) {
                if (picture[end] == L'\'') {
                    if (end + 1 < picture.size() && picture[end + 1] == L'\'') {
                        end += 2;
                        continue;
                    }
                    ++end;
                    break;
                }
                ++end;
            }
        } else if (c == L'h' || c == L'H' || c == L't') {
            while (end < picture.size() && picture[end] == c) ++end;
            kind = c == L'h' ? Token::Kind::Hour12 : c == L'H' ? Token::Kind::Hour24 : Token::Kind::Marker;
        } else if (IsPictureSpace(c)) {
            while (end < picture.size() && IsPictureSpace(picture[end])) ++end;
            kind = Token::Kind::Space;
        }
        tokens.push_back(Token{kind, std::wstring(picture.substr(i, end - i))});
        i = end;
    }
    return tokens;
}

bool Contains(const std::vector<Token>& tokens, Token::Kind kind) {
    return std::any_of(tokens.begin(), tokens.end(), [kind](const Token& t) { return t.kind == kind; });
}

std::wstring Join(const std::vector<Token>& tokens) {
    std::wstring result;
    for (const auto& token : tokens) result += token.text;
    return result;
}

// --- NLS helpers ------------------------------------------------------------------------------

Result<std::wstring> LocaleString(const wchar_t* locale, LCTYPE type, LocaleOverrides overrides) {
    const LCTYPE flags = type | (overrides == LocaleOverrides::Ignore ? LOCALE_NOUSEROVERRIDE : 0);
    const int size = ::GetLocaleInfoEx(locale, flags, nullptr, 0);
    if (size <= 0) return platform::LastWin32Error("GetLocaleInfoEx");
    std::wstring value(static_cast<std::size_t>(size), L'\0');
    const int written = ::GetLocaleInfoEx(locale, flags, value.data(), size);
    if (written <= 0) return platform::LastWin32Error("GetLocaleInfoEx");
    value.resize(static_cast<std::size_t>(written - 1));  // drop the terminator
    return value;
}

// NLS formatting functions report the required size (terminator included) when called without a buffer.
template <class Format>
Result<std::wstring> FormatWithNls(Format format, std::string_view context) {
    const int size = format(nullptr, 0);
    if (size <= 0) return platform::LastWin32Error(context);
    std::wstring text(static_cast<std::size_t>(size), L'\0');
    const int written = format(text.data(), size);
    if (written <= 0) return platform::LastWin32Error(context);
    text.resize(static_cast<std::size_t>(written - 1));
    return text;
}

std::wstring QuoteLiteral(std::wstring_view text) {
    std::wstring quoted = L"'";
    for (const wchar_t c : text) {
        quoted += c;
        if (c == L'\'') quoted += L'\'';
    }
    quoted += L'\'';
    return quoted;
}

Result<void> RequireAvailable(const ClockSnapshot& snapshot) {
    if (snapshot.state != ProviderState::Available) {
        return MakeError(ErrorCode::InvalidState, "the clock snapshot holds no time to format");
    }
    return Ok();
}

}  // namespace

std::wstring ConvertTimePicture(std::wstring_view picture, TimeFormat format, TimeMarkerPosition position,
                                std::wstring_view marker) {
    if (format == TimeFormat::System) return std::wstring(picture);
    std::vector<Token> tokens = Tokenize(picture);
    const bool has12 = Contains(tokens, Token::Kind::Hour12);
    const bool has24 = Contains(tokens, Token::Kind::Hour24);
    const bool hasMarker = Contains(tokens, Token::Kind::Marker);

    if (format == TimeFormat::Hours24) {
        if (!has12 && !hasMarker) return std::wstring(picture);  // already a 24-hour picture
        std::vector<Token> result;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            Token token = tokens[i];
            if (token.kind == Token::Kind::Marker) {
                // Drop the marker with the space that separates it from the time.
                if (!result.empty() && result.back().kind == Token::Kind::Space) {
                    result.pop_back();
                } else if (i + 1 < tokens.size() && tokens[i + 1].kind == Token::Kind::Space) {
                    ++i;
                }
                continue;
            }
            if (token.kind == Token::Kind::Hour12) token = Token{Token::Kind::Hour24, L"HH"};
            result.push_back(std::move(token));
        }
        while (!result.empty() && result.front().kind == Token::Kind::Space) result.erase(result.begin());
        while (!result.empty() && result.back().kind == Token::Kind::Space) result.pop_back();
        return Join(result);
    }

    // Hours12
    if (has12 && !has24 && hasMarker) return std::wstring(picture);  // already a 12-hour picture
    for (auto& token : tokens) {
        if (token.kind == Token::Kind::Hour24) token = Token{Token::Kind::Hour12, L"h"};
    }
    if (!hasMarker) {
        const Token markerToken{Token::Kind::Marker, std::wstring(marker)};
        const Token space{Token::Kind::Space, L" "};
        if (position == TimeMarkerPosition::Prefix) {
            tokens.insert(tokens.begin(), {markerToken, space});
        } else {
            tokens.push_back(space);
            tokens.push_back(markerToken);
        }
    }
    return Join(tokens);
}

Result<std::wstring> FormatTime(const ClockSnapshot& snapshot, const ClockFormatOptions& options,
                                std::wstring_view localeName, LocaleOverrides overrides) {
    if (auto available = RequireAvailable(snapshot); !available) return available.error();
    const std::wstring localeStorage(localeName);
    const wchar_t* locale = localeStorage.empty() ? LOCALE_NAME_USER_DEFAULT : localeStorage.c_str();

    // The short time format is what the taskbar clock shows; the long one carries the seconds.
    auto picture = LocaleString(locale, options.showSeconds ? LOCALE_STIMEFORMAT : LOCALE_SSHORTTIME, overrides);
    if (!picture) return picture.error();

    std::wstring effective = picture.value();
    if (options.format != TimeFormat::System) {
        auto positionText = LocaleString(locale, LOCALE_ITIMEMARKPOSN, overrides);
        const auto position = positionText && positionText.value() == L"1" ? TimeMarkerPosition::Prefix
                                                                            : TimeMarkerPosition::Suffix;
        std::wstring marker = L"tt";
        if (options.format == TimeFormat::Hours12) {
            // Locales with a 24-hour tradition (fr-FR, de-DE...) define empty AM/PM designators, which
            // would make a 12-hour time ambiguous. Use the invariant designators as a quoted literal then.
            auto am = LocaleString(locale, LOCALE_S1159, overrides);
            auto pm = LocaleString(locale, LOCALE_S2359, overrides);
            if (am && pm && am.value().empty() && pm.value().empty()) {
                auto invariant = LocaleString(LOCALE_NAME_INVARIANT, snapshot.hour >= 12 ? LOCALE_S2359 : LOCALE_S1159,
                                              LocaleOverrides::Ignore);
                if (invariant && !invariant.value().empty()) marker = QuoteLiteral(invariant.value());
            }
        }
        effective = ConvertTimePicture(effective, options.format, position, marker);
    }

    const SYSTEMTIME time = ToSystemTime(snapshot);
    const DWORD flags = options.showSeconds ? 0 : TIME_NOSECONDS;
    // LOCALE_NOUSEROVERRIDE is rejected together with an explicit picture (ERROR_INVALID_FLAGS); the
    // picture itself was already read without overrides when requested.
    return FormatWithNls(
        [&](wchar_t* buffer, int size) {
            return ::GetTimeFormatEx(locale, flags, &time, effective.c_str(), buffer, size);
        },
        "GetTimeFormatEx");
}

Result<std::wstring> FormatDate(const ClockSnapshot& snapshot, DateStyle style, std::wstring_view localeName,
                                LocaleOverrides overrides) {
    if (auto available = RequireAvailable(snapshot); !available) return available.error();
    const std::wstring localeStorage(localeName);
    const wchar_t* locale = localeStorage.empty() ? LOCALE_NAME_USER_DEFAULT : localeStorage.c_str();
    const DWORD noOverride = overrides == LocaleOverrides::Ignore ? LOCALE_NOUSEROVERRIDE : 0;

    DWORD flags = 0;
    const wchar_t* picture = nullptr;
    switch (style) {
        case DateStyle::WeekdayDayMonth: picture = L"ddd d MMM"; break;
        case DateStyle::Short: flags = DATE_SHORTDATE | noOverride; break;
        case DateStyle::Long: flags = DATE_LONGDATE | noOverride; break;
    }

    const SYSTEMTIME date = ToSystemTime(snapshot);
    return FormatWithNls(
        [&](wchar_t* buffer, int size) {
            return ::GetDateFormatEx(locale, flags, &date, picture, buffer, size, nullptr);
        },
        "GetDateFormatEx");
}

}  // namespace aether::status
