// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/ui/WeatherWindow.h"

#include "core/Localization.h"
#include "core/WorkerPool.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "graphics/TextCache.h"
#include "graphics/Transition.h"
#include "graphics/WeatherArt.h"
#include "platform/windows/MonitorUtilities.h"
#include "ui/accessibility/AccessibleSurface.h"
#include "weather/WeatherConditions.h"
#include "weather/WeatherTypes.h"
#include "weather/WorldMap.h"
#include "weather/ui/WeatherMapView.h"

#include <commctrl.h>
#include <oleacc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

namespace aether::weather::ui {
namespace {

using graphics::Color;
namespace chr = std::chrono;

// Geometry in DIPs, relative to the panel's top-left corner (the window adds kMargin around it).
constexpr float kMargin = 10.0f;  // transparent edge: the entrance's travel
constexpr float kPanelWidth = 888.0f;
constexpr float kPanelHeight = 716.0f;
constexpr float kRadius = 14.0f;
constexpr float kCardRadius = 10.0f;
constexpr float kLeftX = 16.0f, kLeftW = 500.0f;
constexpr float kRightX = 532.0f, kRightW = 340.0f;
constexpr float kHeroTop = 70.0f, kHeroBottom = 258.0f;
constexpr float kHourTop = 270.0f, kHourBottom = 374.0f;
constexpr float kDayTop = 386.0f, kDayBottom = 672.0f;
constexpr float kMapTop = 70.0f, kMapBottom = 404.0f;
constexpr float kBannerHeight = 58.0f;
constexpr float kTilesTop = 416.0f, kTilesBottom = 672.0f;
constexpr float kFooterTop = 682.0f, kFooterBottom = 708.0f;
constexpr float kSearchLeft = 492.0f, kSearchRight = 752.0f, kSearchTop = 17.0f, kSearchBottom = 49.0f;
constexpr float kDropLeft = 432.0f, kDropTop = 54.0f, kResultHeight = 46.0f;
constexpr std::size_t kMaxResults = 5;
constexpr std::size_t kHourCells = 12;
constexpr std::size_t kMapSamples = 14;

constexpr UINT_PTR kRefreshTimer = 61, kSearchTimer = 62, kCaretTimer = 63;
constexpr UINT kRefreshMs = 15u * 60u * 1000u;  // D78: the same rhythm as the Top Bar's weather
constexpr UINT kSearchDelayMs = 350;
constexpr double kFreshSeconds = 10.0 * 60.0;  // opening again within this reuses the forecast
constexpr double kRestSeconds = 60.0;          // the illustrations rest after a minute without input
constexpr double kRampSeconds = 0.4;
constexpr double kPermissionSeconds = 45.0;    // Windows' location prompt may take the focus meanwhile
constexpr UINT kInvoke = WM_APP + 231;
constexpr int kEditId = 300;

// oleacc.h declares these GUIDs without defining them unless INITGUID is set; keep local copies.
constexpr GUID kClsidAccPropServices = {0xb5f8350b, 0x0548, 0x48b1, {0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7}};
constexpr GUID kPropIdAccName = {0x608d3df8, 0x8128, 0x4aa7, {0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91}};

// Stable identities (UI Automation ids "item-N").
enum : int {
    kClose = 1,
    kSettings,
    kHome,
    kSearch,
    kAllow,
    kLocationSettings,
    kRetry,
    kAttribution,
    kResult = 20,  // + index
    kSummary = 100,
    kHourText = 110,  // + cell
    kDayText = 140,   // + day
    kDetailText = 160,  // + tile
    kMapText = 190,
};

enum class Banner { None, Allow, Precise, Settings, Unavailable, Locating };
enum class Tile { FeelsLike, Humidity, Wind, Sun, Uv, Pressure };

std::wstring T(std::wstring_view english) { return std::wstring(i18n::Tr(english)); }

bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p) noexcept { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }
Color Mix(const Color& a, const Color& b, float t) noexcept {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Color White(float alpha) noexcept { return Color{1.0f, 1.0f, 1.0f, alpha}; }
Color Black(float alpha) noexcept { return Color{0.0f, 0.0f, 0.0f, alpha}; }
float Luminance(const Color& c) noexcept { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
float Ease(float t) noexcept { return t * t * (3.0f - 2.0f * t); }

// --- dates and times, in the interface's formatting locale ---
std::wstring LocaleName() { return std::wstring(i18n::FormattingLocale()); }
const wchar_t* LocaleOrUser(const std::wstring& locale) noexcept { return locale.empty() ? LOCALE_NAME_USER_DEFAULT : locale.c_str(); }

SYSTEMTIME CivilTime(chr::sys_seconds value) noexcept {
    const auto day = chr::floor<chr::days>(value);
    const chr::year_month_day date{day};
    const chr::hh_mm_ss<chr::seconds> time{value - day};
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(static_cast<int>(date.year()));
    st.wMonth = static_cast<WORD>(static_cast<unsigned>(date.month()));
    st.wDay = static_cast<WORD>(static_cast<unsigned>(date.day()));
    st.wDayOfWeek = static_cast<WORD>(chr::weekday{day}.c_encoding());
    st.wHour = static_cast<WORD>(time.hours().count());
    st.wMinute = static_cast<WORD>(time.minutes().count());
    st.wSecond = static_cast<WORD>(time.seconds().count());
    return st;
}
std::wstring FormatTime(const SYSTEMTIME& st, DWORD flags) {
    const auto locale = LocaleName();
    std::array<wchar_t, 64> buffer{};
    if (::GetTimeFormatEx(LocaleOrUser(locale), flags, &st, nullptr, buffer.data(), static_cast<int>(buffer.size())) <= 0) return {};
    return buffer.data();
}
// "14:00" or "2 PM": the hour alone where the locale writes it so, otherwise with its minutes.
std::wstring FormatHour(chr::sys_seconds value) {
    auto text = FormatTime(CivilTime(value), TIME_NOMINUTESORSECONDS);
    if (!text.empty() && std::all_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) text += L":00";
    return text;
}
std::wstring FormatClock(chr::sys_seconds value) { return FormatTime(CivilTime(value), TIME_NOSECONDS); }
std::wstring FormatWeekday(chr::sys_days day) {
    const auto locale = LocaleName();
    const SYSTEMTIME st = CivilTime(chr::sys_seconds{day});
    std::array<wchar_t, 64> buffer{};
    if (::GetDateFormatEx(LocaleOrUser(locale), 0, &st, L"dddd", buffer.data(), static_cast<int>(buffer.size()), nullptr) <= 0) return {};
    std::wstring text = buffer.data();
    if (!text.empty()) ::CharUpperBuffW(text.data(), 1);
    return text;
}
// A device-clock instant in the device's own time zone.
std::wstring FormatDeviceClock(chr::system_clock::time_point when) {
    using Ticks = chr::duration<std::int64_t, std::ratio<1, 10'000'000>>;
    const std::int64_t value = chr::duration_cast<Ticks>(when.time_since_epoch()).count() + 116444736000000000LL;
    if (value <= 0) return {};
    const FILETIME file{static_cast<DWORD>(value & 0xFFFFFFFF), static_cast<DWORD>(static_cast<std::uint64_t>(value) >> 32)};
    SYSTEMTIME utc{}, local{};
    if (!::FileTimeToSystemTime(&file, &utc) || !::SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return {};
    return FormatTime(local, TIME_NOSECONDS);
}
double HourOf(chr::sys_seconds value) noexcept {
    const auto day = chr::floor<chr::days>(value);
    return chr::duration<double, std::ratio<3600>>(value - day).count();
}
std::wstring_view PeriodText(double hour) noexcept {
    if (hour >= 5.0 && hour < 12.0) return L"Morning";
    if (hour >= 12.0 && hour < 18.0) return L"Afternoon";
    if (hour >= 18.0 && hour < 22.0) return L"Evening";
    return L"Night";
}
std::wstring_view UvLevel(double uv) noexcept {
    if (uv < 3.0) return L"Low";
    if (uv < 6.0) return L"Moderate";
    if (uv < 8.0) return L"High";
    if (uv < 11.0) return L"Very high";
    return L"Extreme";
}
std::wstring_view ErrorText(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Network:
        case ErrorCode::Timeout: return L"No connection to the weather service.";
        case ErrorCode::RateLimited: return L"The weather service is busy. Try again in a moment.";
        default: return L"The weather could not be loaded.";
    }
}
std::wstring Join(std::initializer_list<std::wstring_view> parts) {
    std::wstring text;
    for (const auto part : parts) {
        if (part.empty()) continue;
        if (!text.empty()) text += L" · ";
        text += part;
    }
    return text;
}
std::wstring Percent(int value) { return std::format(L"{}%", value); }
std::wstring SpeedUnit(status::TemperatureUnit unit) { return unit == status::TemperatureUnit::Fahrenheit ? L"mph" : L"km/h"; }

struct ShownPlace {
    double latitude = 0.0;
    double longitude = 0.0;
    std::wstring title;   // a city's name; empty for a device position (the map names it)
    std::wstring detail;  // its region and country
    bool device = false;  // the person's own position (a dot on the map), not a city (a pin)
    bool searched = false;
    bool valid = false;
};

struct MapSample {
    double latitude = 0.0;
    double longitude = 0.0;
    std::wstring name;
    std::optional<double> temperature;
};

// What a load brings back to the UI thread.
struct Loaded {
    std::uint64_t generation = 0;
    std::shared_ptr<const WorldMap> map;
    bool mapFailed = false;
    std::optional<std::size_t> country;
    std::optional<GeoBounds> view;
    std::vector<MapSample> samples;
    std::wstring nearest;
    std::optional<DetailedForecast> forecast;
    std::optional<Error> error;
};

struct Target {
    int id = 0;
    D2D1_RECT_F rect{};
    std::wstring name;
    float radius = 8.0f;
};

struct TextItem {
    int id = 0;
    D2D1_RECT_F rect{};
    std::wstring name;
};

}  // namespace

struct WeatherWindow::Impl final : aether::ui::accessibility::IAccessibleSurfaceModel {
    WeatherWindow& owner;
    WeatherWindowContext context;
    graphics::DesignTokens tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    graphics::MotionPolicy motion;
    graphics::TextCache text;
    aether::ui::accessibility::AccessibleSurface accessible;
    WorkerPool worker{WorkerPool::Options{2, L"Aether weather window", ComApartment::None}};
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    CancellationSource loadCancel, searchCancel;
    std::uint64_t generation = 0, searchGeneration = 0;

    // Data
    WeatherHome home;
    ShownPlace place;
    std::shared_ptr<const WorldMap> map;
    bool mapUnavailable = false;
    std::optional<std::size_t> country;
    std::optional<GeoBounds> view;
    std::vector<MapSample> samples;
    std::wstring nearest;
    std::optional<DetailedForecast> forecast;
    ShownPlace forecastFor;  // the place `forecast` belongs to
    std::wstring error;
    bool loading = false;
    double permissionUntil = 0.0;

    // City search
    HWND edit = nullptr;
    HFONT editFont = nullptr;
    int editFontPx = 0;
    std::wstring query;
    std::vector<LocationCandidate> results;
    bool dropdown = false, searching = false;
    std::wstring searchMessage;
    std::optional<std::size_t> selected;
    bool caretVisible = true;
    float inputScroll = 0.0f;

    // Layout and input
    float scale = 1.0f;  // fitted into a small work area
    RECT anchor{};
    std::vector<Target> targets;
    std::vector<TextItem> texts;
    std::optional<int> hovered, pressed, focused;
    std::optional<std::size_t> hoveredSample;
    bool cues = false, closing = false, destroyed = false, liquid = false;
    std::uint64_t renders = 0;

    // Motion: one clock for every illustration; energy 0 is their still pose.
    double clock = 0.0, lastStep = 0.0, lastInput = 0.0;
    float energy = 0.0f;
    bool stepping = false;

    // The map, drawn once into a bitmap until its data, size or colours change.
    winrt::com_ptr<ID2D1Bitmap1> mapLayer;
    D2D1_RECT_F mapLayerArea{};
    float mapLayerScale = 0.0f;
    std::vector<std::uint8_t> heatPixels;
    UINT heatWidth = 0, heatHeight = 0;

    Impl(WeatherWindow& window, WeatherWindowContext value) : owner(window), context(value) {}
    ~Impl() override {
        *alive = false;
        loadCancel.Cancel();
        searchCancel.Cancel();
        worker.Shutdown();
        if (editFont) ::DeleteObject(editFont);
    }

    // --- state ---
    [[nodiscard]] bool HighContrast() const noexcept { return tokens.mode == graphics::ThemeMode::HighContrast; }
    [[nodiscard]] bool Light() const noexcept { return tokens.mode == graphics::ThemeMode::Light; }
    [[nodiscard]] ShownPlace HomePlace() const {
        ShownPlace p;
        if (home.source == HomeSource::None || !std::isfinite(home.latitude) || !std::isfinite(home.longitude)) return p;
        p.latitude = home.latitude;
        p.longitude = home.longitude;
        p.valid = true;
        p.device = home.source != HomeSource::ChosenCity;
        if (!p.device) {
            const auto comma = home.name.find(L", ");
            p.title = home.name.substr(0, comma);
            if (comma != std::wstring::npos) p.detail = home.name.substr(comma + 2);
        }
        return p;
    }
    [[nodiscard]] static bool SamePlace(const ShownPlace& a, const ShownPlace& b) noexcept {
        return a.valid && b.valid && std::abs(a.latitude - b.latitude) < 0.02 && std::abs(a.longitude - b.longitude) < 0.02 &&
               a.device == b.device;
    }
    [[nodiscard]] Banner CurrentBanner() const noexcept {
        if (home.locating) return Banner::Locating;
        switch (home.source) {
            case HomeSource::PreciseLocation:
            case HomeSource::ChosenCity: return Banner::None;
            case HomeSource::ApproximateLocation: return Banner::Precise;
            case HomeSource::None: break;
        }
        switch (home.permission) {
            case LocationPermission::Denied: return Banner::Settings;
            case LocationPermission::Unavailable: return Banner::Unavailable;
            case LocationPermission::Granted: return Banner::Locating;
            default: return Banner::Allow;
        }
    }
    [[nodiscard]] std::wstring Title() const {
        if (!place.valid) return T(L"Weather");
        if (!place.device) return place.title;
        return nearest.empty() ? T(L"Your location") : nearest;
    }
    [[nodiscard]] std::wstring CountryName() const {
        if (!map || !country) return {};
        return map->CountryName(*country, static_cast<std::size_t>(i18n::CurrentLanguage()));
    }

    // --- geometry ---
    [[nodiscard]] D2D1_RECT_F MapCard() const {
        const float top = CurrentBanner() == Banner::None ? kMapTop : kMapTop + kBannerHeight + 8.0f;
        return D2D1::RectF(kRightX, top, kRightX + kRightW, kMapBottom);
    }
    [[nodiscard]] D2D1_RECT_F MapArea() const {
        const auto card = MapCard();
        return D2D1::RectF(card.left + 8.0f, card.top + 34.0f, card.right - 8.0f, card.bottom - 8.0f);
    }
    [[nodiscard]] static D2D1_RECT_F SearchRect() noexcept { return D2D1::RectF(kSearchLeft, kSearchTop, kSearchRight, kSearchBottom); }
    [[nodiscard]] static D2D1_RECT_F InputRect() noexcept {
        return D2D1::RectF(kSearchLeft + 34.0f, kSearchTop + 6.0f, kSearchRight - 12.0f, kSearchBottom - 6.0f);
    }
    [[nodiscard]] std::size_t DropdownRows() const noexcept {
        return results.empty() ? ((searching || !searchMessage.empty()) ? 1 : 0) : std::min(results.size(), kMaxResults);
    }
    [[nodiscard]] D2D1_RECT_F DropdownRect() const noexcept {
        return D2D1::RectF(kDropLeft, kDropTop, kSearchRight, kDropTop + 8.0f + kResultHeight * static_cast<float>(DropdownRows()));
    }
    [[nodiscard]] bool DropdownShown() const noexcept { return dropdown && DropdownRows() > 0; }
    [[nodiscard]] D2D1_RECT_F TileRect(int index) const noexcept {
        const float width = (kRightW - 8.0f) / 2.0f, height = (kTilesBottom - kTilesTop - 16.0f) / 3.0f;
        const float x = kRightX + static_cast<float>(index % 2) * (width + 8.0f);
        const float y = kTilesTop + static_cast<float>(index / 2) * (height + 8.0f);
        return D2D1::RectF(x, y, x + width, y + height);
    }
    // Panel DIPs from window DIPs, and window pixels from panel DIPs.
    [[nodiscard]] D2D1_POINT_2F ToPanel(D2D1_POINT_2F point) const noexcept {
        return D2D1::Point2F(point.x / scale - kMargin, point.y / scale - kMargin);
    }
    [[nodiscard]] RECT ToScreen(const D2D1_RECT_F& r) const noexcept {
        const auto bounds = owner.BoundsPx();
        const float factor = scale * static_cast<float>(owner.Dpi()) / 96.0f;
        return RECT{bounds.left + static_cast<LONG>(std::lround((r.left + kMargin) * factor)),
                    bounds.top + static_cast<LONG>(std::lround((r.top + kMargin) * factor)),
                    bounds.left + static_cast<LONG>(std::lround((r.right + kMargin) * factor)),
                    bounds.top + static_cast<LONG>(std::lround((r.bottom + kMargin) * factor))};
    }

    // --- map placement of the samples ---
    struct Pill {
        std::size_t sample = 0;
        D2D1_RECT_F rect{};
        D2D1_POINT_2F at{};
    };
    [[nodiscard]] std::vector<Pill> Pills(const MapProjection& projection) const {
        std::vector<Pill> pills;
        const auto area = MapArea();
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (!samples[i].temperature) continue;
            const auto at = projection.Project(samples[i].latitude, samples[i].longitude);
            const auto rect = D2D1::RectF(at.x - 17.0f, at.y - 10.0f, at.x + 17.0f, at.y + 10.0f);
            if (rect.left < area.left || rect.right > area.right || rect.top < area.top || rect.bottom > area.bottom) continue;
            pills.push_back({i, rect, at});
        }
        return pills;
    }

    // --- targets and accessibility ---
    void Add(int id, const D2D1_RECT_F& rect, std::wstring name, float radius = 8.0f) { targets.push_back({id, rect, std::move(name), radius}); }
    [[nodiscard]] D2D1_RECT_F BannerButton() const {
        const auto banner = CurrentBanner();
        const std::wstring label = BannerButtonLabel(banner);
        if (label.empty()) return {};
        graphics::TextStyle style;
        style.size = 12.0f;
        style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        const float width = std::clamp(const_cast<graphics::TextCache&>(text).Measure(label, style).width + 24.0f, 72.0f, 170.0f);
        const float bottom = kMapTop + kBannerHeight - 12.0f;
        return D2D1::RectF(kRightX + kRightW - 12.0f - width, bottom - 30.0f, kRightX + kRightW - 12.0f, bottom);
    }
    [[nodiscard]] static std::wstring BannerButtonLabel(Banner banner) {
        switch (banner) {
            case Banner::Allow: return T(L"Allow");
            case Banner::Precise: return T(L"Use precise location");
            case Banner::Settings: return T(L"Location settings");
            default: return {};
        }
    }
    [[nodiscard]] D2D1_RECT_F RetryRect() const noexcept {
        const float cx = kLeftX + kLeftW / 2.0f;
        return D2D1::RectF(cx - 70.0f, kHeroBottom - 58.0f, cx + 70.0f, kHeroBottom - 26.0f);
    }
    [[nodiscard]] D2D1_RECT_F AttributionRect() const {
        graphics::TextStyle style;  // as Painter::Text draws it
        style.size = 11.0f;
        style.opticalSmall = true;
        style.tabularFigures = true;
        const float width = const_cast<graphics::TextCache&>(text).Measure(T(L"Weather data by Open-Meteo.com"), style).width;
        return D2D1::RectF(kLeftX, kFooterTop, kLeftX + std::min(width + 8.0f, 300.0f), kFooterBottom);
    }
    void Rebuild() {
        targets.clear();
        Add(kSearch, SearchRect(), T(L"Search for a city"), 16.0f);
        Add(kHome, D2D1::RectF(760.0f, 17.0f, 792.0f, 49.0f), T(L"My location"));
        Add(kSettings, D2D1::RectF(800.0f, 17.0f, 832.0f, 49.0f), T(L"Weather settings"));
        Add(kClose, D2D1::RectF(840.0f, 17.0f, 872.0f, 49.0f), T(L"Close"));
        if (const auto banner = CurrentBanner(); banner == Banner::Allow || banner == Banner::Precise)
            Add(kAllow, BannerButton(), BannerButtonLabel(banner), 6.0f);
        else if (banner == Banner::Settings)
            Add(kLocationSettings, BannerButton(), BannerButtonLabel(banner), 6.0f);
        if (!error.empty() && !forecast && place.valid) Add(kRetry, RetryRect(), T(L"Try again"), 6.0f);
        Add(kAttribution, AttributionRect(), T(L"Weather data by Open-Meteo.com"), 4.0f);
        if (DropdownShown()) {
            for (std::size_t i = 0; i < std::min(results.size(), kMaxResults); ++i) {
                const float top = kDropTop + 4.0f + kResultHeight * static_cast<float>(i);
                Add(kResult + static_cast<int>(i), D2D1::RectF(kDropLeft + 4.0f, top, kSearchRight - 4.0f, top + kResultHeight),
                    FormatLocationLabel(results[i]), 6.0f);
            }
        }
        if (focused && !Find(*focused)) focused.reset();
        RebuildTexts();
    }
    void RebuildTexts() {
        texts.clear();
        if (!forecast) {
            texts.push_back({kSummary, D2D1::RectF(kLeftX, kHeroTop, kLeftX + kLeftW, kHeroBottom), StatusText()});
            return;
        }
        const auto& f = *forecast;
        const auto unit = f.unit;
        texts.push_back({kSummary, D2D1::RectF(kLeftX, kHeroTop, kLeftX + kLeftW, kHeroBottom),
                         Join({Title(), FormatTemperature(f.current.temperature, unit), i18n::Tr(ConditionText(f.current.weatherCode))})});
        for (std::size_t i = 0; i < kHourCells && i * 2 < f.hourly.size(); ++i) {
            const auto h = HourAt(i);
            texts.push_back({kHourText + static_cast<int>(i), HourCell(i),
                             Join({i == 0 ? T(L"Now") : FormatHour(h.localTime), FormatTemperature(h.temperature, unit),
                                   i18n::Tr(ConditionText(h.weatherCode))})});
        }
        for (std::size_t i = 0; i < f.daily.size() && i < kForecastDays; ++i) {
            const auto& d = f.daily[i];
            texts.push_back({kDayText + static_cast<int>(i), DayRow(i),
                             Join({i == 0 ? T(L"Today") : FormatWeekday(d.date), i18n::Tr(ConditionText(d.weatherCode)),
                                   FormatTemperature(d.minTemperature, unit) + L" – " + FormatTemperature(d.maxTemperature, unit)})});
        }
        for (int tile = 0; tile < 6; ++tile) {
            const auto [title, value, note] = TileTexts(static_cast<Tile>(tile));
            texts.push_back({kDetailText + tile, TileRect(tile), Join({title, value, note})});
        }
        if (const auto name = CountryName(); !name.empty()) texts.push_back({kMapText, MapCard(), Join({name, T(L"Temperatures now")})});
    }
    [[nodiscard]] const Target* Find(int id) const {
        for (const auto& target : targets)
            if (target.id == id) return &target;
        return nullptr;
    }
    [[nodiscard]] std::optional<int> Hit(D2D1_POINT_2F panel) const {
        // The dropdown lies over everything else.
        if (DropdownShown() && Contains(DropdownRect(), panel)) {
            for (const auto& target : targets)
                if (target.id >= kResult && Contains(target.rect, panel)) return target.id;
            return std::nullopt;
        }
        for (const auto& target : targets)
            if (target.id < kResult && Contains(target.rect, panel)) return target.id;
        return std::nullopt;
    }

    std::wstring RootName() const override { return T(L"Weather"); }
    CONTROLTYPEID RootControlType() const override { return UIA_PaneControlTypeId; }
    std::size_t ItemCount() const override {
        // The search field is the native edit's own element.
        return targets.size() - (Find(kSearch) ? 1 : 0) + texts.size();
    }
    aether::ui::accessibility::AccessibleItem Item(std::size_t index) const override {
        aether::ui::accessibility::AccessibleItem item;
        std::vector<const Target*> buttons;
        for (const auto& target : targets)
            if (target.id != kSearch) buttons.push_back(&target);
        item.isOffscreen = !owner.IsShown();
        if (index < buttons.size()) {
            const auto& target = *buttons[index];
            item.id = target.id;
            item.name = target.name;
            item.controlType = target.id == kAttribution ? UIA_HyperlinkControlTypeId
                             : target.id >= kResult      ? UIA_ListItemControlTypeId
                                                         : UIA_ButtonControlTypeId;
            item.supportsInvoke = true;
            item.hasKeyboardFocus = focused == target.id;
            item.boundsPx = ToScreen(target.rect);
            return item;
        }
        const auto& entry = texts.at(index - buttons.size());
        item.id = entry.id;
        item.name = entry.name;
        item.controlType = UIA_TextControlTypeId;
        item.isFocusable = false;
        item.boundsPx = ToScreen(entry.rect);
        return item;
    }
    Result<void> Invoke(int id) override {
        if (!owner.IsShown() || !Find(id)) return MakeError(ErrorCode::Unavailable, "weather action unavailable");
        return ::PostMessageW(owner.Handle(), kInvoke, static_cast<WPARAM>(id), 0)
                   ? Ok()
                   : Result<void>(MakeError(ErrorCode::Platform, "weather action could not be queued"));
    }
    Result<void> Toggle(int id) override { return Invoke(id); }
    Result<void> SetFocus(int id) override {
        if (!owner.IsShown() || !Find(id)) return MakeError(ErrorCode::Unavailable, "weather focus unavailable");
        Focus(id, true);
        return Ok();
    }

    // --- focus and actions ---
    void Focus(int id, bool keyboard) {
        focused = id;
        cues = cues || keyboard;
        if (id == kSearch) {
            if (edit) ::SetFocus(edit);
        } else if (edit && ::GetFocus() == edit) {
            ::SetFocus(owner.Handle());
        }
        (void)accessible.NotifyFocusChanged(id);
        owner.Invalidate();
    }
    void FocusNext(bool backwards) {
        std::vector<int> order;
        for (const auto& target : targets)
            if (target.id < kResult) order.push_back(target.id);
        if (order.empty()) return;
        const auto found = focused ? std::find(order.begin(), order.end(), *focused) : order.end();
        const int count = static_cast<int>(order.size());
        int index = found == order.end() ? (backwards ? 0 : -1) : static_cast<int>(found - order.begin());
        index = (index + (backwards ? -1 : 1) + count) % count;
        dropdown = false;
        Focus(order[static_cast<std::size_t>(index)], true);
        Rebuild();
    }
    template <class... Args>
    void Emit(Signal<Args...>& signal, const Args&... arguments) {
        try {
            signal.Emit(arguments...);
        } catch (...) {
        }
    }
    void Activate(int id) {
        const double now = graphics::FrameScheduler::Now();
        switch (id) {
            case kClose: owner.Close(); return;
            case kSettings: Emit(owner.WeatherSettingsRequested); return;
            case kHome: GoHome(); return;
            case kSearch: Focus(kSearch, false); return;
            case kAllow:
                permissionUntil = now + kPermissionSeconds;
                Emit(owner.LocationAccessRequested);
                return;
            case kLocationSettings: Emit(owner.LocationSettingsRequested); return;
            case kRetry: Load(false); return;
            case kAttribution: Emit(owner.LinkRequested, std::wstring(L"https://open-meteo.com/")); return;
            default:
                if (id >= kResult && static_cast<std::size_t>(id - kResult) < results.size()) Pick(static_cast<std::size_t>(id - kResult));
                return;
        }
    }
    void GoHome() {
        ClearSearch();
        const auto next = HomePlace();
        const bool same = SamePlace(next, place) && !place.searched;
        place = next;
        if (!same || !forecast) {
            if (!SamePlace(forecastFor, place)) forecast.reset();
            Load(false);
        }
        Rebuild();
        owner.Invalidate();
    }
    void Pick(std::size_t index) {
        if (index >= results.size()) return;
        const auto candidate = results[index];
        ClearSearch();
        place = ShownPlace{};
        place.latitude = candidate.latitude;
        place.longitude = candidate.longitude;
        place.title = candidate.name;
        place.detail = Join({candidate.admin1 == candidate.name ? std::wstring_view{} : std::wstring_view{candidate.admin1},
                             candidate.country});
        place.searched = true;
        place.valid = std::isfinite(place.latitude) && std::isfinite(place.longitude);
        if (owner.Handle()) ::SetFocus(owner.Handle());
        focused.reset();
        forecast.reset();
        nearest.clear();
        Load(false);
        Rebuild();
        owner.Invalidate();
    }
    void ClearSearch() {
        searchCancel.Cancel();
        ++searchGeneration;
        if (owner.Handle()) ::KillTimer(owner.Handle(), kSearchTimer);
        results.clear();
        searching = false;
        searchMessage.clear();
        selected.reset();
        dropdown = false;
        query.clear();
        if (edit && ::GetWindowTextLengthW(edit) > 0) ::SetWindowTextW(edit, L"");
    }

    // --- data ---
    void Load(bool quiet) {
        if (!place.valid) {
            // No place: nothing of an earlier one stays on screen.
            loadCancel.Cancel();
            ++generation;
            loading = false;
            forecast.reset();
            country.reset();
            view.reset();
            samples.clear();
            nearest.clear();
            error.clear();
            mapLayer = nullptr;
            heatPixels.clear();
            Rebuild();
            owner.Invalidate();
            return;
        }
        loadCancel.Cancel();
        loadCancel = CancellationSource{};
        const auto token = loadCancel.Token();
        const auto ticket = ++generation;
        loading = true;
        if (!quiet) error.clear();
        const double latitude = place.latitude, longitude = place.longitude;
        const bool device = place.device;
        const auto unit = home.unit;
        auto knownMap = map;
        auto* client = context.forecast;
        auto* ui = &context.dispatcher;
        const auto weak = std::weak_ptr<bool>(alive);
        Impl* self = this;
        const bool posted = worker.Post([=]() {
            Loaded loaded;
            loaded.generation = ticket;
            loaded.map = knownMap;
            try {
                if (!loaded.map) {
                    if (auto parsed = LoadEmbeddedWorldMap()) loaded.map = std::make_shared<const WorldMap>(std::move(*parsed));
                    else loaded.mapFailed = true;
                }
                std::vector<GeoPoint> points;
                if (loaded.map) {
                    const auto& world = *loaded.map;
                    loaded.country = world.CountryFor(latitude, longitude);
                    loaded.view = world.FocusBounds(latitude, longitude, loaded.country);
                    for (const auto index : world.SamplePlaces(*loaded.view, loaded.country, latitude, longitude, kMapSamples)) {
                        const auto& sample = world.Places()[index];
                        loaded.samples.push_back({sample.latitude, sample.longitude, world.PlaceName(index), std::nullopt});
                        points.push_back({sample.latitude, sample.longitude});
                    }
                    if (device) {
                        if (const auto closest = world.NearestPlace(latitude, longitude, 30.0, loaded.country)) {
                            const auto& town = world.Places()[*closest];
                            const auto name = world.PlaceName(*closest);
                            loaded.nearest = DistanceKilometres(latitude, longitude, town.latitude, town.longitude) <= 8.0
                                                 ? name
                                                 : i18n::TrFormat(L"Near {}", name);
                        }
                    }
                }
                if (!client) {
                    loaded.error = MakeError(ErrorCode::Unavailable, "forecast unavailable");
                } else {
                    if (auto fetched = client->Fetch(latitude, longitude, unit, token)) loaded.forecast = std::move(*fetched);
                    else loaded.error = fetched.error();
                    if (!token.IsCancelled() && points.size() <= kMaxForecastPoints && !points.empty()) {
                        if (auto readings = client->FetchPoints(points, unit, token)) {
                            for (const auto& reading : *readings)
                                if (reading.index < loaded.samples.size()) loaded.samples[reading.index].temperature = reading.temperature;
                        }
                    }
                }
            } catch (...) {
                loaded.error = MakeError(ErrorCode::Internal, "weather window load failed");
            }
            ui->Post([weak, self, loaded = std::move(loaded)]() mutable {
                if (const auto live = weak.lock(); live && *live) self->Receive(std::move(loaded));
            });
        });
        if (!posted) loading = false;
        Rebuild();
        owner.Invalidate();
    }
    void Receive(Loaded loaded) {
        if (loaded.generation != generation) return;
        loading = false;
        if (loaded.map) map = loaded.map;
        mapUnavailable = loaded.mapFailed;
        if (loaded.forecast) {
            forecast = std::move(loaded.forecast);
            forecastFor = place;
            error.clear();
        } else if (loaded.error && loaded.error->code != ErrorCode::Cancelled) {
            error = T(ErrorText(loaded.error->code));
        }
        country = loaded.country;
        view = loaded.view;
        samples = std::move(loaded.samples);
        if (place.device) nearest = std::move(loaded.nearest);
        mapLayer = nullptr;
        heatPixels.clear();
        Rebuild();
        if (owner.IsShown()) {
            (void)accessible.NotifyStructureChanged();
            owner.Invalidate();
        }
    }
    [[nodiscard]] std::wstring StatusText() const {
        if (!place.valid) return T(L"Search for a city, or allow your location, to see the weather.");
        if (loading) return T(L"Loading the weather…");
        if (!error.empty()) return error;
        return {};
    }

    // --- search ---
    void ReadQuery() {
        if (!edit) return;
        const int length = ::GetWindowTextLengthW(edit);
        std::wstring value(static_cast<std::size_t>(std::max(length, 0)) + 1, L'\0');
        ::GetWindowTextW(edit, value.data(), length + 1);
        value.resize(static_cast<std::size_t>(std::max(length, 0)));
        query = std::move(value);
    }
    [[nodiscard]] std::wstring TrimmedQuery() const {
        const auto first = query.find_first_not_of(L" \t");
        if (first == std::wstring::npos) return {};
        const auto last = query.find_last_not_of(L" \t");
        return query.substr(first, last - first + 1);
    }
    void QueryChanged() {
        ReadQuery();
        selected.reset();
        searchCancel.Cancel();
        ++searchGeneration;
        if (TrimmedQuery().size() < 2) {
            results.clear();
            searching = false;
            searchMessage.clear();
            dropdown = false;
            ::KillTimer(owner.Handle(), kSearchTimer);
        } else {
            dropdown = true;
            searching = true;
            searchMessage.clear();
            ::SetTimer(owner.Handle(), kSearchTimer, kSearchDelayMs, nullptr);
        }
        Rebuild();
        owner.Invalidate();
    }
    void RunSearch() {
        ::KillTimer(owner.Handle(), kSearchTimer);
        const auto q = TrimmedQuery();
        if (q.size() < 2) return;
        auto* geocoding = context.geocoding;
        if (!geocoding) {
            searching = false;
            results.clear();
            searchMessage = T(L"City search is unavailable.");
            Rebuild();
            owner.Invalidate();
            return;
        }
        searchCancel.Cancel();
        searchCancel = CancellationSource{};
        const auto token = searchCancel.Token();
        const auto ticket = ++searchGeneration;
        // The geocoder takes a language's primary subtag ("pt", not "pt-BR"; "zh", not "zh-Hans").
        std::string_view code = i18n::Info(i18n::CurrentLanguage()).code;
        code = code.substr(0, code.find('-'));
        const std::wstring language(code.begin(), code.end());
        auto* ui = &context.dispatcher;
        const auto weak = std::weak_ptr<bool>(alive);
        Impl* self = this;
        searching = true;
        (void)worker.Post([=]() {
            std::optional<std::vector<LocationCandidate>> found;
            bool failed = false;
            try {
                if (auto result = geocoding->Search(q, language, token)) found = std::move(*result);
                else failed = result.error().code != ErrorCode::Cancelled;
            } catch (...) {
                failed = true;
            }
            ui->Post([weak, self, ticket, found = std::move(found), failed]() mutable {
                if (const auto live = weak.lock(); live && *live) self->ReceiveResults(ticket, std::move(found), failed);
            });
        });
    }
    void ReceiveResults(std::uint64_t ticket, std::optional<std::vector<LocationCandidate>> found, bool failed) {
        if (ticket != searchGeneration) return;
        searching = false;
        results.clear();
        if (found) {
            for (auto& candidate : *found)
                if (results.size() < kMaxResults && std::isfinite(candidate.latitude) && std::isfinite(candidate.longitude))
                    results.push_back(std::move(candidate));
        }
        searchMessage = failed ? T(L"City search is unavailable.") : results.empty() && found ? T(L"No city found.") : std::wstring{};
        selected = results.empty() ? std::nullopt : std::optional<std::size_t>(0);
        Rebuild();
        owner.Invalidate();
    }

    // --- motion ---
    [[nodiscard]] bool MotionWanted(double now) const noexcept {
        return owner.IsShown() && !closing && motion.AllowsMovement() && !HighContrast() && now - lastInput < kRestSeconds;
    }
    void Wake() {
        const double now = graphics::FrameScheduler::Now();
        lastInput = now;
        if (!MotionWanted(now)) return;
        if (!stepping) {
            stepping = true;
            lastStep = now;
        }
        owner.RequestAnimationFrame();
    }
    bool Step(double now) {
        if (!stepping) return false;
        const double elapsed = std::clamp(now - lastStep, 0.0, 0.1);
        lastStep = now;
        const float goal = MotionWanted(now) ? 1.0f : 0.0f;
        const float delta = static_cast<float>(elapsed / kRampSeconds);
        energy = goal > energy ? std::min(goal, energy + delta) : std::max(goal, energy - delta);
        clock += elapsed * Ease(energy);
        if (energy <= 0.0f && goal <= 0.0f) stepping = false;  // this frame draws the still pose
        return true;
    }
    [[nodiscard]] graphics::WeatherArtMotion Motion() const noexcept { return {clock, Ease(energy)}; }

    // --- the map's picture ---
    void BuildHeat(const MapProjection& projection) {
        heatPixels.clear();
        std::vector<TemperatureSample> points;
        for (const auto& sample : samples)
            if (sample.temperature) points.push_back({sample.latitude, sample.longitude, *sample.temperature});
        if (forecast) points.push_back({place.latitude, place.longitude, forecast->current.temperature});
        if (points.size() < 2) return;
        const auto area = projection.area;
        const float width = area.right - area.left, height = area.bottom - area.top;
        if (width <= 1.0f || height <= 1.0f) return;
        heatWidth = 96;
        heatHeight = static_cast<UINT>(std::clamp(std::lround(96.0f * height / width), 8L, 128L));
        heatPixels.resize(static_cast<std::size_t>(heatWidth) * heatHeight * 4);
        const auto unit = forecast ? forecast->unit : home.unit;
        for (UINT y = 0; y < heatHeight; ++y) {
            for (UINT x = 0; x < heatWidth; ++x) {
                const auto point = D2D1::Point2F(area.left + (static_cast<float>(x) + 0.5f) * width / static_cast<float>(heatWidth),
                                                 area.top + (static_cast<float>(y) + 0.5f) * height / static_cast<float>(heatHeight));
                const auto [latitude, longitude] = projection.Unproject(point);
                const auto value = InterpolateTemperature(points, latitude, longitude, projection.cosine);
                const auto color = TemperatureColor(value.value_or(10.0), unit);
                constexpr float alpha = 0.9f;
                auto* pixel = &heatPixels[(static_cast<std::size_t>(y) * heatWidth + x) * 4];
                pixel[0] = static_cast<std::uint8_t>(std::lround(color.b * alpha * 255.0f));
                pixel[1] = static_cast<std::uint8_t>(std::lround(color.g * alpha * 255.0f));
                pixel[2] = static_cast<std::uint8_t>(std::lround(color.r * alpha * 255.0f));
                pixel[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
            }
        }
    }
    [[nodiscard]] winrt::com_ptr<ID2D1PathGeometry> Shape(ID2D1Factory* factory, const MapProjection& projection, bool focus,
                                                         const GeoBounds& wide) const {
        winrt::com_ptr<ID2D1PathGeometry> path;
        winrt::com_ptr<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(path.put())) || FAILED(path->Open(sink.put()))) return {};
        sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
        std::vector<D2D1_POINT_2F> points;
        const auto rings = map->Rings();
        const auto addRing = [&](std::size_t ring) {
            const auto vertices = map->RingPoints(ring);
            if (vertices.size() < 3) return;
            points.clear();
            points.reserve(vertices.size());
            for (const auto& v : vertices) points.push_back(projection.Project(static_cast<double>(v.lat) / 100.0, static_cast<double>(v.lon) / 100.0));
            sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        };
        if (focus) {
            if (country && *country < map->Countries().size()) {
                const auto& c = map->Countries()[*country];
                for (std::uint32_t i = 0; i < c.ringCount; ++i) {
                    const std::size_t ring = c.firstRing + i;
                    if (ring < rings.size() && rings[ring].bounds.Intersects(wide)) addRing(ring);
                }
            }
        } else {
            for (std::size_t ring = 0; ring < rings.size(); ++ring)
                if ((!country || rings[ring].country != *country) && rings[ring].bounds.Intersects(wide)) addRing(ring);
        }
        if (FAILED(sink->Close())) return {};
        return path;
    }
    [[nodiscard]] Color SeaColor() const noexcept {
        if (HighContrast()) return tokens.surface.background;
        return Light() ? Color::FromHex(0xCFE4F5) : Color::FromHex(0x16212E);
    }
    [[nodiscard]] Color LandColor() const noexcept {
        if (HighContrast()) return tokens.surface.card;
        return Light() ? Color::FromHex(0xEDEAE2) : Color::FromHex(0x313840);
    }
    // Draws the sea, the neighbours, the country with its temperatures and its outline into `mapLayer`.
    void BuildMapLayer(ID2D1DeviceContext* dc, const MapProjection& projection, float pixelsPerDip) {
        mapLayer = nullptr;
        const auto area = projection.area;
        const UINT width = static_cast<UINT>(std::ceil((area.right - area.left) * pixelsPerDip));
        const UINT height = static_cast<UINT>(std::ceil((area.bottom - area.top) * pixelsPerDip));
        if (width == 0 || height == 0 || width > 4096 || height > 4096) return;
        winrt::com_ptr<ID2D1Bitmap1> layer;
        const float dpi = 96.0f * pixelsPerDip;
        if (FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                                    D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                                                            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi),
                                    layer.put())))
            return;
        winrt::com_ptr<ID2D1Factory> factory;
        dc->GetFactory(factory.put());
        if (!factory) return;
        const double margin = 0.25;
        GeoBounds wide = projection.view;
        wide.west -= projection.view.Width() * margin;
        wide.east += projection.view.Width() * margin;
        wide.south -= projection.view.Height() * margin;
        wide.north += projection.view.Height() * margin;
        // Wider than the view where the area is wider than the view's shape.
        const auto [topLatitude, leftLongitude] = projection.Unproject(D2D1::Point2F(area.left, area.top));
        const auto [bottomLatitude, rightLongitude] = projection.Unproject(D2D1::Point2F(area.right, area.bottom));
        wide.Include(GeoBounds{leftLongitude, bottomLatitude, rightLongitude, topLatitude});
        const auto countryShape = Shape(factory.get(), projection, true, wide);
        const auto neighbourShape = Shape(factory.get(), projection, false, wide);
        if (heatPixels.empty()) BuildHeat(projection);
        winrt::com_ptr<ID2D1Bitmap1> heat;
        if (!heatPixels.empty())
            (void)dc->CreateBitmap(D2D1::SizeU(heatWidth, heatHeight), heatPixels.data(), heatWidth * 4,
                                   D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                                           D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                   heat.put());
        winrt::com_ptr<ID2D1Image> previous;
        dc->GetTarget(previous.put());
        D2D1_MATRIX_3X2_F transform{};
        dc->GetTransform(&transform);
        float oldDpiX = 96.0f, oldDpiY = 96.0f;
        dc->GetDpi(&oldDpiX, &oldDpiY);
        const auto antialias = dc->GetAntialiasMode();
        dc->SetTarget(layer.get());
        dc->SetDpi(dpi, dpi);
        dc->SetTransform(D2D1::Matrix3x2F::Translation(-area.left, -area.top));
        dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        dc->Clear(D2D1::ColorF(0, 0, 0, 0));
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(dc->CreateSolidColorBrush(SeaColor().ToD2D(), brush.put()))) {
            winrt::com_ptr<ID2D1RoundedRectangleGeometry> clip;
            factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(area, 8.0f, 8.0f), clip.put());
            if (clip)
                dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                     D2D1::IdentityMatrix(), 1.0f, nullptr, D2D1_LAYER_OPTIONS1_NONE),
                              nullptr);
            dc->FillRectangle(area, brush.get());
            const bool hc = HighContrast();
            if (neighbourShape) {
                brush->SetColor(LandColor().WithAlpha(hc ? 1.0f : 0.75f).ToD2D());
                dc->FillGeometry(neighbourShape.get(), brush.get());
                brush->SetColor((hc ? tokens.textSecondary : Light() ? Black(0.18f) : White(0.16f)).ToD2D());
                dc->DrawGeometry(neighbourShape.get(), brush.get(), 0.6f);
            }
            if (countryShape) {
                if (heat && !hc) {
                    dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), countryShape.get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                         D2D1::IdentityMatrix(), 1.0f, nullptr, D2D1_LAYER_OPTIONS1_NONE),
                                  nullptr);
                    brush->SetColor(LandColor().ToD2D());
                    dc->FillRectangle(area, brush.get());
                    dc->DrawBitmap(heat.get(), area, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                    dc->PopLayer();
                } else {
                    brush->SetColor(LandColor().ToD2D());
                    dc->FillGeometry(countryShape.get(), brush.get());
                }
                brush->SetColor((hc ? tokens.textPrimary : Light() ? Black(0.45f) : White(0.7f)).ToD2D());
                dc->DrawGeometry(countryShape.get(), brush.get(), 1.0f);
            }
            if (clip) dc->PopLayer();
        }
        dc->SetAntialiasMode(antialias);
        dc->SetTransform(transform);
        dc->SetDpi(oldDpiX, oldDpiY);
        dc->SetTarget(previous.get());
        mapLayer = std::move(layer);
        mapLayerArea = area;
        mapLayerScale = pixelsPerDip;
    }

    // --- texts of the details ---
    struct TileText {
        std::wstring title, value, note;
    };
    [[nodiscard]] TileText TileTexts(Tile tile) const {
        TileText t;
        if (!forecast) return t;
        const auto& f = *forecast;
        const auto& c = f.current;
        switch (tile) {
            case Tile::FeelsLike:
                t.title = T(L"Feels like");
                if (c.apparentTemperature) {
                    t.value = FormatTemperature(*c.apparentTemperature, f.unit);
                    const double difference = *c.apparentTemperature - c.temperature;
                    t.note = std::abs(difference) < 1.5 ? T(L"Close to the real temperature")
                           : difference < 0.0          ? T(L"The wind makes it feel colder")
                                                       : T(L"Humidity makes it feel warmer");
                }
                break;
            case Tile::Humidity:
                t.title = T(L"Humidity");
                if (c.humidity) t.value = Percent(*c.humidity);
                if (c.cloudCover) t.note = i18n::TrFormat(L"Cloud cover {}", Percent(*c.cloudCover));
                break;
            case Tile::Wind:
                t.title = T(L"Wind");
                if (c.windSpeed) t.value = std::format(L"{} {}", std::lround(*c.windSpeed), SpeedUnit(f.unit));
                if (c.windGusts) t.note = i18n::TrFormat(L"Gusts {}", std::format(L"{} {}", std::lround(*c.windGusts), SpeedUnit(f.unit)));
                break;
            case Tile::Sun:
                t.title = T(L"Sunrise and sunset");
                if (!f.daily.empty() && f.daily.front().sunrise && f.daily.front().sunset)
                    t.value = L"↑ " + FormatClock(*f.daily.front().sunrise) + L"   ↓ " + FormatClock(*f.daily.front().sunset);
                break;
            case Tile::Uv:
                t.title = T(L"UV index");
                if (!f.daily.empty() && f.daily.front().uvIndex) {
                    t.value = std::format(L"{}", std::lround(*f.daily.front().uvIndex));
                    t.note = T(UvLevel(*f.daily.front().uvIndex));
                }
                break;
            case Tile::Pressure:
                t.title = T(L"Pressure");
                if (c.pressure) t.value = std::format(L"{} hPa", std::lround(*c.pressure));
                t.note = T(L"At sea level");
                break;
        }
        if (t.value.empty()) t.value = L"–";
        return t;
    }
    // The hours shown: every other one from now, Now being the current conditions.
    [[nodiscard]] HourlyPoint HourAt(std::size_t index) const {
        auto hour = forecast->hourly[index * 2];
        if (index == 0) {
            hour.temperature = forecast->current.temperature;
            hour.weatherCode = forecast->current.weatherCode;
            hour.isDay = forecast->current.isDay;
        }
        return hour;
    }
    [[nodiscard]] static D2D1_RECT_F HourCell(std::size_t index) noexcept {
        const float width = (kLeftW - 16.0f) / static_cast<float>(kHourCells);
        const float left = kLeftX + 8.0f + width * static_cast<float>(index);
        return D2D1::RectF(left, kHourTop + 6.0f, left + width, kHourBottom - 6.0f);
    }
    [[nodiscard]] static D2D1_RECT_F DayRow(std::size_t index) noexcept {
        const float height = (kDayBottom - kDayTop - 12.0f) / static_cast<float>(kForecastDays);
        const float top = kDayTop + 6.0f + height * static_cast<float>(index);
        return D2D1::RectF(kLeftX + 8.0f, top, kLeftX + kLeftW - 8.0f, top + height);
    }

    // --- painting ---
    struct Painter {
        Impl& p;
        ID2D1DeviceContext* dc;
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        ID2D1SolidColorBrush* Paint(const Color& color) {
            brush->SetColor(color.ToD2D());
            return brush.get();
        }
        void Fill(const D2D1_RECT_F& rect, float radius, const Color& color) {
            dc->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), Paint(color));
        }
        void Stroke(const D2D1_RECT_F& rect, float radius, const Color& color, float width = 1.0f) {
            const float h = width / 2.0f;
            dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left + h, rect.top + h, rect.right - h, rect.bottom - h), radius, radius),
                                     Paint(color), width);
        }
        void Text(std::wstring_view value, const D2D1_RECT_F& rect, const Color& color, float size,
                  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                  bool display = false, bool wrap = false) {
            if (value.empty()) return;
            graphics::TextStyle style;
            style.size = size;
            style.weight = weight;
            style.alignment = align;
            style.display = display;
            style.ellipsis = !wrap;
            style.opticalSmall = size <= 12.0f;
            style.tabularFigures = !display && !wrap;
            p.text.Draw(dc, value, rect, style, Paint(color));
        }
        [[nodiscard]] Color Primary() const { return p.tokens.textPrimary; }
        [[nodiscard]] Color Secondary() const { return p.tokens.textSecondary; }
        [[nodiscard]] Color RainColor() const {
            if (p.HighContrast()) return p.tokens.textPrimary;
            return p.Light() ? Color::FromHex(0x1F6FD1) : Color::FromHex(0x59ADFF);
        }
        // A card: Grain's frosted layer, or in Liquid a lens of the glass with its rim of light.
        void Card(const D2D1_RECT_F& rect) {
            if (p.HighContrast()) {
                Fill(rect, kCardRadius, p.tokens.surface.card);
                Stroke(rect, kCardRadius, p.tokens.textPrimary, 1.0f);
                return;
            }
            const bool light = p.Light();
            if (p.liquid) {
                Fill(rect, kCardRadius, light ? White(0.28f) : White(0.06f));
                winrt::com_ptr<ID2D1GradientStopCollection> stops;
                const std::array<D2D1_GRADIENT_STOP, 2> gradient{{{0.0f, (light ? White(0.85f) : White(0.28f)).ToD2D()},
                                                                  {1.0f, (light ? White(0.25f) : White(0.04f)).ToD2D()}}};
                winrt::com_ptr<ID2D1LinearGradientBrush> rim;
                if (SUCCEEDED(dc->CreateGradientStopCollection(gradient.data(), 2, stops.put())) &&
                    SUCCEEDED(dc->CreateLinearGradientBrush(
                        D2D1::LinearGradientBrushProperties(D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.left, rect.bottom)),
                        stops.get(), rim.put()))) {
                    dc->DrawRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f), kCardRadius, kCardRadius),
                        rim.get(), 1.0f);
                }
                return;
            }
            Fill(rect, kCardRadius, light ? White(0.55f) : White(0.05f));
            Stroke(rect, kCardRadius, light ? Black(0.07f) : White(0.07f), 1.0f);
        }
        void Art(const D2D1_RECT_F& box, int code, bool isDay, float stroke = 1.5f) {
            const auto subject = graphics::WeatherSubjectForCode(code, isDay);
            const bool mono = p.HighContrast();
            graphics::PaintWeatherArt(dc, box, subject, mono ? graphics::WeatherArtStyle::Monochrome : graphics::WeatherArtStyle::Color,
                                      p.tokens.textPrimary, p.Motion(), stroke);
        }
        void Highlight(int id, const D2D1_RECT_F& rect, float radius) {
            const bool hot = p.hovered == id, down = p.pressed == id && hot;
            if (!hot) return;
            const Color color = p.HighContrast() ? p.tokens.surface.subtleFill
                              : p.Light()        ? Black(down ? 0.09f : 0.05f)
                                                 : White(down ? 0.06f : 0.09f);
            Fill(rect, radius, color);
        }
        void Focus(int id) {
            if (p.cues && p.focused == id) {
                if (const auto* target = p.Find(id)) graphics::PaintFocusRing(dc, target->rect, target->radius, p.tokens);
            }
        }
        void Button(int id, std::wstring_view label, bool accent) {
            const auto* target = p.Find(id);
            if (!target) return;
            const auto& r = target->rect;
            if (accent && !p.HighContrast()) {
                const auto& a = p.tokens.accentRoles;
                const bool hot = p.hovered == id, down = hot && p.pressed == id;
                Fill(r, target->radius, down ? a.fillPressed : hot ? a.fillHover : a.fill);
                Text(label, r, a.textOnAccent, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
            } else {
                Fill(r, target->radius, p.HighContrast() ? p.tokens.surface.controlFill : p.Light() ? White(0.7f) : White(0.08f));
                Highlight(id, r, target->radius);
                Stroke(r, target->radius, p.HighContrast() ? p.tokens.textPrimary : p.Light() ? Black(0.1f) : White(0.1f));
                Text(label, r, Primary(), 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            Focus(id);
        }
        void Icon(int id, graphics::Glyph glyph) {
            const auto* target = p.Find(id);
            if (!target) return;
            Highlight(id, target->rect, target->radius);
            const float cx = (target->rect.left + target->rect.right) / 2.0f, cy = (target->rect.top + target->rect.bottom) / 2.0f;
            graphics::PaintGlyph(dc, glyph, D2D1::RectF(cx - 8.0f, cy - 8.0f, cx + 8.0f, cy + 8.0f), Paint(Primary()), p.tokens.glyph.stroke);
            Focus(id);
        }
    };

    void PaintHeader(Painter& g) {
        g.Text(Title(), D2D1::RectF(kLeftX + 4.0f, 12.0f, kSearchLeft - 12.0f, 40.0f), g.Primary(), 22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
               DWRITE_TEXT_ALIGNMENT_LEADING, true);
        std::wstring when;
        if (forecast) {
            const auto local = forecast->LocalNow(chr::system_clock::now());
            when = FormatWeekday(chr::floor<chr::days>(local)) + L" " + FormatClock(local);
        }
        std::wstring where = place.device ? CountryName() : place.detail;
        std::wstring precision;
        if (place.valid && !place.searched) {
            precision = home.source == HomeSource::PreciseLocation       ? T(L"Precise location")
                      : home.source == HomeSource::ApproximateLocation ? T(L"Approximate location")
                      : home.source == HomeSource::ChosenCity          ? T(L"Chosen in Settings")
                                                                       : std::wstring{};
        }
        g.Text(Join({where, when, precision}), D2D1::RectF(kLeftX + 4.0f, 40.0f, kSearchLeft - 12.0f, 60.0f), g.Secondary(), 12.0f);
        PaintSearch(g);
        g.Icon(kHome, graphics::Glyph::Location);
        g.Icon(kSettings, graphics::Glyph::Settings);
        g.Icon(kClose, graphics::Glyph::Close);
    }
    void PaintSearch(Painter& g) {
        const auto field = SearchRect();
        const bool focusedField = edit && ::GetFocus() == edit;
        const bool hc = HighContrast();
        g.Fill(field, 16.0f, hc ? tokens.surface.controlFill : Light() ? White(0.75f) : White(0.08f));
        g.Highlight(kSearch, field, 16.0f);
        g.Stroke(field, 16.0f, focusedField ? tokens.accentRoles.fill : hc ? tokens.textPrimary : Light() ? Black(0.1f) : White(0.1f),
                 focusedField ? 1.5f : 1.0f);
        graphics::PaintGlyph(g.dc, graphics::Glyph::Search, D2D1::RectF(field.left + 12.0f, field.top + 9.0f, field.left + 26.0f, field.top + 23.0f),
                             g.Paint(g.Secondary()), tokens.glyph.stroke);
        const auto input = InputRect();
        graphics::TextStyle style;
        style.size = 13.0f;
        g.dc->PushAxisAlignedClip(input, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (query.empty()) {
            if (!focusedField) g.Text(T(L"Search for a city"), input, g.Secondary(), 13.0f);
        } else {
            winrt::com_ptr<IDWriteTextLayout> layout;
            text.Factory()->CreateTextLayout(query.data(), static_cast<UINT32>(query.size()), text.Format(style), 10000.0f,
                                             input.bottom - input.top, layout.put());
            if (layout) {
                DWORD start = 0, end = 0;
                ::SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
                FLOAT caretX = 0, caretY = 0;
                DWRITE_HIT_TEST_METRICS caret{};
                layout->HitTestTextPosition(end, FALSE, &caretX, &caretY, &caret);
                inputScroll = std::max(0.0f, caretX - (input.right - input.left) + 3.0f);
                const auto origin = D2D1::Point2F(input.left - inputScroll, input.top);
                if (focusedField && end > start) {
                    std::vector<DWRITE_HIT_TEST_METRICS> ranges(query.size() + 1);
                    UINT32 count = 0;
                    if (SUCCEEDED(layout->HitTestTextRange(start, end - start, origin.x, origin.y, ranges.data(),
                                                           static_cast<UINT32>(ranges.size()), &count)))
                        for (UINT32 i = 0; i < count; ++i)
                            g.dc->FillRectangle(D2D1::RectF(ranges[i].left, ranges[i].top, ranges[i].left + ranges[i].width,
                                                            ranges[i].top + ranges[i].height),
                                                g.Paint(hc ? tokens.surface.selectedFill : tokens.accentRoles.fill.WithAlpha(0.32f)));
                }
                g.dc->DrawTextLayout(origin, layout.get(), g.Paint(g.Primary()), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        if (focusedField && caretVisible) {
            DWORD start = 0, end = 0;
            ::SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            if (start == end) {
                float x = input.left;
                if (!query.empty()) {
                    winrt::com_ptr<IDWriteTextLayout> layout;
                    text.Factory()->CreateTextLayout(query.data(), static_cast<UINT32>(query.size()), text.Format(style), 10000.0f,
                                                     input.bottom - input.top, layout.put());
                    FLOAT caretX = 0, caretY = 0;
                    DWRITE_HIT_TEST_METRICS caret{};
                    if (layout && SUCCEEDED(layout->HitTestTextPosition(end, FALSE, &caretX, &caretY, &caret))) x = input.left - inputScroll + caretX;
                }
                const float cy = (input.top + input.bottom) / 2.0f;
                g.dc->DrawLine(D2D1::Point2F(x + 0.5f, cy - 8.0f), D2D1::Point2F(x + 0.5f, cy + 8.0f), g.Paint(g.Primary()), 1.0f);
            }
        }
        g.dc->PopAxisAlignedClip();
        g.Focus(kSearch);
    }
    void PaintDropdown(Painter& g) {
        if (!DropdownShown()) return;
        const auto box = DropdownRect();
        const bool hc = HighContrast();
        graphics::PaintElevation(g.dc, box, 10.0f, tokens, graphics::Elevation::Popup, 0.8f);
        g.Fill(box, 10.0f, hc ? tokens.surface.card : Light() ? Color::FromHex(0xF7F7F9) : Color::FromHex(0x2A2D33));
        g.Stroke(box, 10.0f, hc ? tokens.textPrimary : Light() ? Black(0.1f) : White(0.1f));
        if (results.empty()) {
            const auto row = D2D1::RectF(box.left + 14.0f, box.top + 4.0f, box.right - 14.0f, box.top + 4.0f + kResultHeight);
            g.Text(searching ? T(L"Searching…") : searchMessage, row, g.Secondary(), 12.0f);
            return;
        }
        for (std::size_t i = 0; i < std::min(results.size(), kMaxResults); ++i) {
            const auto* target = Find(kResult + static_cast<int>(i));
            if (!target) continue;
            const auto& r = target->rect;
            if (selected == i) g.Fill(r, 6.0f, hc ? tokens.surface.selectedFill : tokens.accentRoles.fill.WithAlpha(0.18f));
            g.Highlight(target->id, r, 6.0f);
            graphics::PaintGlyph(g.dc, graphics::Glyph::Location, D2D1::RectF(r.left + 10.0f, r.top + 15.0f, r.left + 26.0f, r.top + 31.0f),
                                 g.Paint(g.Secondary()), tokens.glyph.stroke);
            const auto& c = results[i];
            g.Text(c.name, D2D1::RectF(r.left + 36.0f, r.top + 4.0f, r.right - 8.0f, r.top + 24.0f), g.Primary(), 13.0f,
                   DWRITE_FONT_WEIGHT_SEMI_BOLD);
            g.Text(Join({c.admin1 == c.name ? std::wstring_view{} : std::wstring_view{c.admin1}, c.country}),
                   D2D1::RectF(r.left + 36.0f, r.top + 23.0f, r.right - 8.0f, r.bottom - 4.0f), g.Secondary(), 11.0f);
            g.Focus(target->id);
        }
    }
    void PaintHero(Painter& g) {
        const auto card = D2D1::RectF(kLeftX, kHeroTop, kLeftX + kLeftW, kHeroBottom);
        const bool hc = HighContrast();
        if (!forecast) {
            g.Card(card);
            const auto status = StatusText();
            float top = card.top + 20.0f;
            if (!place.valid) {
                // Nothing to show yet: a still, quiet illustration above the invitation.
                const float cx = (card.left + card.right) / 2.0f;
                graphics::PaintWeatherArt(g.dc, D2D1::RectF(cx - 36.0f, card.top + 22.0f, cx + 36.0f, card.top + 94.0f),
                                          graphics::WeatherSubjectForCode(2, true), graphics::WeatherArtStyle::Monochrome,
                                          tokens.textSecondary, graphics::WeatherArtMotion{}, 1.6f);
                top = card.top + 96.0f;
            }
            const float bottom = Find(kRetry) ? kHeroBottom - 66.0f : kHeroBottom - 20.0f;
            if (!status.empty())
                g.Text(status, D2D1::RectF(card.left + 40.0f, top, card.right - 40.0f, bottom), loading ? g.Secondary() : g.Primary(),
                       15.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, false, true);
            if (Find(kRetry)) g.Button(kRetry, T(L"Try again"), true);
            return;
        }
        const auto& f = *forecast;
        const auto& c = f.current;
        const auto subject = graphics::WeatherSubjectForCode(c.weatherCode, c.isDay);
        const auto local = f.LocalNow(chr::system_clock::now());
        const double hour = HourOf(local);
        double sunrise = 6.0, sunset = 18.0;
        if (!f.daily.empty() && f.daily.front().sunrise && f.daily.front().sunset) {
            sunrise = HourOf(*f.daily.front().sunrise);
            sunset = HourOf(*f.daily.front().sunset);
        }
        Color ink = g.Primary(), soft = g.Secondary();
        if (hc) {
            g.Card(card);
        } else {
            // D78: the place's sky at its hour: night blue, dawn, light morning blue, day, dusk.
            const auto sky = graphics::SkyFor(subject, hour, sunrise, sunset);
            const std::array<D2D1_GRADIENT_STOP, 2> gradient{{{0.0f, sky.top.ToD2D()}, {1.0f, sky.bottom.ToD2D()}}};
            winrt::com_ptr<ID2D1GradientStopCollection> stops;
            winrt::com_ptr<ID2D1LinearGradientBrush> fill;
            if (SUCCEEDED(g.dc->CreateGradientStopCollection(gradient.data(), 2, stops.put())) &&
                SUCCEEDED(g.dc->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(card.left, card.top), D2D1::Point2F(card.left, card.bottom)), stops.get(),
                    fill.put())))
                g.dc->FillRoundedRectangle(D2D1::RoundedRect(card, kCardRadius, kCardRadius), fill.get());
            else
                g.Fill(card, kCardRadius, sky.top);
            g.Stroke(card, kCardRadius, White(0.12f));
            ink = sky.darkText ? Color::FromHex(0x10202E) : Color::FromHex(0xFFFFFF);
            soft = ink.WithAlpha(0.78f);
        }
        const auto art = D2D1::RectF(card.left + 18.0f, card.top + 28.0f, card.left + 150.0f, card.top + 160.0f);
        g.Art(art, c.weatherCode, c.isDay, 1.3f);
        const float x = card.left + 176.0f;
        g.Text(Join({T(PeriodText(hour)), FormatClock(local)}), D2D1::RectF(x, card.top + 14.0f, card.right - 16.0f, card.top + 34.0f), soft, 13.0f,
               DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.Text(FormatTemperature(c.temperature, f.unit), D2D1::RectF(x - 4.0f, card.top + 32.0f, card.right - 16.0f, card.top + 108.0f), ink, 64.0f,
               DWRITE_FONT_WEIGHT_SEMI_LIGHT, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        g.Text(i18n::Tr(ConditionText(c.weatherCode)), D2D1::RectF(x, card.top + 110.0f, card.right - 16.0f, card.top + 134.0f), ink, 17.0f,
               DWRITE_FONT_WEIGHT_SEMI_BOLD);
        std::wstring range;
        if (!f.daily.empty())
            range = L"↑ " + FormatTemperature(f.daily.front().maxTemperature, f.unit, false) + L"   ↓ " +
                    FormatTemperature(f.daily.front().minTemperature, f.unit, false);
        std::wstring feels;
        if (c.apparentTemperature) feels = i18n::TrFormat(L"Feels like {}", FormatTemperature(*c.apparentTemperature, f.unit, false));
        g.Text(Join({range, feels}), D2D1::RectF(x, card.top + 136.0f, card.right - 16.0f, card.top + 158.0f), soft, 13.0f);
        if (!error.empty())
            g.Text(T(L"Could not update; showing the last forecast."), D2D1::RectF(x, card.top + 160.0f, card.right - 16.0f, card.bottom - 6.0f),
                   soft, 11.0f);
    }
    void PaintHourly(Painter& g) {
        const auto card = D2D1::RectF(kLeftX, kHourTop, kLeftX + kLeftW, kHourBottom);
        g.Card(card);
        if (!forecast) return;
        const auto& f = *forecast;
        for (std::size_t i = 0; i < kHourCells && i * 2 < f.hourly.size(); ++i) {
            const auto h = HourAt(i);
            const auto cell = HourCell(i);
            g.Text(i == 0 ? T(L"Now") : FormatHour(h.localTime), D2D1::RectF(cell.left, cell.top + 2.0f, cell.right, cell.top + 18.0f),
                   i == 0 ? g.Primary() : g.Secondary(), 11.0f, i == 0 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                   DWRITE_TEXT_ALIGNMENT_CENTER);
            const float cx = (cell.left + cell.right) / 2.0f;
            g.Art(D2D1::RectF(cx - 14.0f, cell.top + 20.0f, cx + 14.0f, cell.top + 48.0f), h.weatherCode, h.isDay);
            g.Text(FormatTemperature(h.temperature, f.unit, false), D2D1::RectF(cell.left, cell.top + 50.0f, cell.right, cell.top + 70.0f),
                   g.Primary(), 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
            if (h.precipitationProbability && *h.precipitationProbability >= 10)
                g.Text(Percent(*h.precipitationProbability), D2D1::RectF(cell.left, cell.top + 70.0f, cell.right, cell.bottom),
                       g.RainColor(), 10.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    void PaintDaily(Painter& g) {
        const auto card = D2D1::RectF(kLeftX, kDayTop, kLeftX + kLeftW, kDayBottom);
        g.Card(card);
        if (!forecast || forecast->daily.empty()) return;
        const auto& f = *forecast;
        double low = f.daily.front().minTemperature, high = f.daily.front().maxTemperature;
        for (const auto& d : f.daily) {
            low = std::min(low, d.minTemperature);
            high = std::max(high, d.maxTemperature);
        }
        const double span = std::max(1.0, high - low);
        const bool hc = HighContrast();
        for (std::size_t i = 0; i < f.daily.size() && i < kForecastDays; ++i) {
            const auto& d = f.daily[i];
            const auto row = DayRow(i);
            const float cy = (row.top + row.bottom) / 2.0f;
            if (i > 0) g.dc->DrawLine(D2D1::Point2F(row.left + 8.0f, row.top), D2D1::Point2F(row.right - 8.0f, row.top),
                                      g.Paint(hc ? tokens.textSecondary : Light() ? Black(0.06f) : White(0.06f)), 1.0f);
            g.Text(i == 0 ? T(L"Today") : FormatWeekday(d.date), D2D1::RectF(row.left + 8.0f, row.top, row.left + 118.0f, row.bottom), g.Primary(),
                   13.0f, i == 0 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
            g.Art(D2D1::RectF(row.left + 120.0f, cy - 14.0f, row.left + 148.0f, cy + 14.0f), d.weatherCode, true);
            if (d.precipitationProbability && *d.precipitationProbability >= 10)
                g.Text(Percent(*d.precipitationProbability), D2D1::RectF(row.left + 152.0f, row.top, row.left + 192.0f, row.bottom), g.RainColor(),
                       11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            g.Text(FormatTemperature(d.minTemperature, f.unit, false), D2D1::RectF(row.left + 192.0f, row.top, row.left + 236.0f, row.bottom),
                   g.Secondary(), 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
            g.Text(FormatTemperature(d.maxTemperature, f.unit, false), D2D1::RectF(row.right - 48.0f, row.top, row.right - 6.0f, row.bottom),
                   g.Primary(), 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING);
            // The day's range on the week's scale, in the temperatures' colours.
            const float trackLeft = row.left + 246.0f, trackRight = row.right - 58.0f;
            const auto track = D2D1::RectF(trackLeft, cy - 3.0f, trackRight, cy + 3.0f);
            g.Fill(track, 3.0f, hc ? tokens.surface.controlFill : Light() ? Black(0.07f) : White(0.08f));
            const auto at = [&](double value) {
                return trackLeft + static_cast<float>((value - low) / span) * (trackRight - trackLeft);
            };
            auto range = D2D1::RectF(at(d.minTemperature), cy - 3.0f, std::max(at(d.maxTemperature), at(d.minTemperature) + 6.0f), cy + 3.0f);
            if (hc) {
                g.Fill(range, 3.0f, tokens.textPrimary);
            } else {
                const std::array<D2D1_GRADIENT_STOP, 2> gradient{{{0.0f, TemperatureColor(d.minTemperature, f.unit).ToD2D()},
                                                                  {1.0f, TemperatureColor(d.maxTemperature, f.unit).ToD2D()}}};
                winrt::com_ptr<ID2D1GradientStopCollection> stops;
                winrt::com_ptr<ID2D1LinearGradientBrush> fill;
                if (SUCCEEDED(g.dc->CreateGradientStopCollection(gradient.data(), 2, stops.put())) &&
                    SUCCEEDED(g.dc->CreateLinearGradientBrush(
                        D2D1::LinearGradientBrushProperties(D2D1::Point2F(range.left, cy), D2D1::Point2F(range.right, cy)), stops.get(), fill.put())))
                    g.dc->FillRoundedRectangle(D2D1::RoundedRect(range, 3.0f, 3.0f), fill.get());
            }
            if (i == 0) {
                const float x = std::clamp(at(f.current.temperature), trackLeft + 4.0f, trackRight - 4.0f);
                g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 5.0f, 5.0f), g.Paint(hc ? tokens.surface.background : Light() ? White(1.0f) : Color::FromHex(0x1B1F24)));
                g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 3.2f, 3.2f), g.Paint(g.Primary()));
            }
        }
    }
    void PaintBanner(Painter& g) {
        const auto banner = CurrentBanner();
        if (banner == Banner::None) return;
        const auto box = D2D1::RectF(kRightX, kMapTop, kRightX + kRightW, kMapTop + kBannerHeight);
        const bool hc = HighContrast();
        g.Fill(box, kCardRadius, hc ? tokens.surface.card : tokens.accentRoles.fill.WithAlpha(Light() ? 0.12f : 0.18f));
        g.Stroke(box, kCardRadius, hc ? tokens.textPrimary : tokens.accentRoles.fill.WithAlpha(0.35f));
        graphics::PaintGlyph(g.dc, graphics::Glyph::Location, D2D1::RectF(box.left + 12.0f, box.top + 21.0f, box.left + 28.0f, box.top + 37.0f),
                             g.Paint(g.Primary()), tokens.glyph.stroke);
        std::wstring message;
        switch (banner) {
            case Banner::Allow: message = T(L"See the weather where you are: allow your precise location."); break;
            case Banner::Precise: message = T(L"Your position is approximate (about 10 km)."); break;
            case Banner::Settings: message = T(L"Location is turned off for this app in Windows."); break;
            case Banner::Unavailable: message = T(L"This device cannot report its location. Search for a city instead."); break;
            case Banner::Locating: message = T(L"Reading Windows location…"); break;
            case Banner::None: break;
        }
        const auto button = BannerButton();
        const float right = button.right > button.left ? button.left - 10.0f : box.right - 12.0f;
        g.Text(message, D2D1::RectF(box.left + 38.0f, box.top + 6.0f, right, box.bottom - 6.0f), g.Primary(), 12.0f, DWRITE_FONT_WEIGHT_NORMAL,
               DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        if (Find(kAllow)) g.Button(kAllow, BannerButtonLabel(banner), true);
        if (Find(kLocationSettings)) g.Button(kLocationSettings, BannerButtonLabel(banner), false);
    }
    void PaintMap(Painter& g, float pixelsPerDip) {
        const auto card = MapCard();
        g.Card(card);
        const auto name = CountryName();
        g.Text(name.empty() ? Title() : name, D2D1::RectF(card.left + 14.0f, card.top + 8.0f, card.right - 130.0f, card.top + 30.0f), g.Primary(),
               13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        g.Text(T(L"Temperatures now"), D2D1::RectF(card.right - 150.0f, card.top + 8.0f, card.right - 14.0f, card.top + 30.0f), g.Secondary(), 11.0f,
               DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
        const auto area = MapArea();
        if (!map || !view) {
            const std::wstring status = mapUnavailable ? T(L"The map is unavailable.") : (loading || place.valid) ? T(L"Loading…") : std::wstring{};
            g.Fill(area, 8.0f, SeaColor());
            g.Text(status, area, g.Secondary(), 12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
            return;
        }
        const auto projection = MakeProjection(*view, area);
        if (!mapLayer || std::abs(mapLayerScale - pixelsPerDip) > 0.001f || mapLayerArea.top != area.top || mapLayerArea.bottom != area.bottom)
            BuildMapLayer(g.dc, projection, pixelsPerDip);
        if (mapLayer) g.dc->DrawBitmap(mapLayer.get(), area, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
        else g.Fill(area, 8.0f, SeaColor());
        g.dc->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const auto unit = forecast ? forecast->unit : home.unit;
        const bool hc = HighContrast();
        for (const auto& pill : Pills(projection)) {
            const double value = *samples[pill.sample].temperature;
            const Color fill = hc ? tokens.surface.card : TemperatureColor(value, unit);
            const Color ink = hc ? tokens.textPrimary : Luminance(fill) > 0.55f ? Color::FromHex(0x15191E) : Color::FromHex(0xFFFFFF);
            g.Fill(pill.rect, 10.0f, fill);
            g.Stroke(pill.rect, 10.0f, hc ? tokens.textPrimary : Black(0.25f));
            g.Text(FormatTemperature(value, unit, false), pill.rect, ink, 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        // The person's position (a pulsing dot) or the searched city (a pin).
        const auto at = projection.Project(place.latitude, place.longitude);
        if (place.device) {
            const auto m = Motion();
            const float phase = static_cast<float>(std::fmod(m.clock / 2.2, 1.0));
            const float radius = 7.0f + (m.energy > 0.0f ? 12.0f * phase : 4.0f);
            const float alpha = m.energy > 0.0f ? 0.45f * (1.0f - phase) * m.energy + 0.18f * (1.0f - m.energy) : 0.18f;
            const Color blue = hc ? tokens.textPrimary : Color::FromHex(0x2D7FF9);
            g.dc->FillEllipse(D2D1::Ellipse(at, radius, radius), g.Paint(blue.WithAlpha(alpha)));
            g.dc->FillEllipse(D2D1::Ellipse(at, 7.0f, 7.0f), g.Paint(hc ? tokens.surface.background : White(1.0f)));
            g.dc->FillEllipse(D2D1::Ellipse(at, 5.0f, 5.0f), g.Paint(blue));
        } else {
            const Color red = hc ? tokens.textPrimary : Color::FromHex(0xE5484D);
            g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(at.x, at.y - 10.0f), 7.0f, 7.0f), g.Paint(red));
            g.dc->DrawLine(D2D1::Point2F(at.x, at.y - 4.0f), at, g.Paint(red), 2.0f);
            g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(at.x, at.y - 10.0f), 2.6f, 2.6f), g.Paint(White(1.0f)));
        }
        // A hovered temperature names its town.
        if (hoveredSample && *hoveredSample < samples.size()) {
            for (const auto& pill : Pills(projection)) {
                if (pill.sample != *hoveredSample) continue;
                graphics::TextStyle style;
                style.size = 11.0f;
                style.opticalSmall = true;
                const auto& label = samples[pill.sample].name;
                const float width = text.Measure(label, style).width + 16.0f;
                auto box = D2D1::RectF(pill.at.x - width / 2.0f, pill.rect.top - 24.0f, pill.at.x + width / 2.0f, pill.rect.top - 4.0f);
                const float shift = std::max(0.0f, area.left + 2.0f - box.left) - std::max(0.0f, box.right - (area.right - 2.0f));
                box.left += shift;
                box.right += shift;
                if (box.top < area.top + 2.0f) {
                    box.top = pill.rect.bottom + 4.0f;
                    box.bottom = box.top + 20.0f;
                }
                g.Fill(box, 6.0f, hc ? tokens.surface.card : Light() ? White(0.95f) : Color::FromHex(0x1E2228, 0.95f));
                g.Text(label, box, g.Primary(), 11.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        }
        g.dc->PopAxisAlignedClip();
    }
    void PaintTiles(Painter& g) {
        for (int index = 0; index < 6; ++index) {
            const auto r = TileRect(index);
            g.Card(r);
            const auto tile = static_cast<Tile>(index);
            const auto labels = TileTexts(tile);
            g.Text(labels.title, D2D1::RectF(r.left + 12.0f, r.top + 8.0f, r.right - 10.0f, r.top + 24.0f), g.Secondary(), 11.0f);
            const bool hc = HighContrast();
            if (tile == Tile::Sun) {
                g.Text(labels.value, D2D1::RectF(r.left + 12.0f, r.bottom - 26.0f, r.right - 10.0f, r.bottom - 6.0f), g.Primary(), 12.0f,
                       DWRITE_FONT_WEIGHT_SEMI_BOLD);
                PaintSunArc(g, D2D1::RectF(r.left + 20.0f, r.top + 26.0f, r.right - 20.0f, r.bottom - 30.0f));
                continue;
            }
            g.Text(labels.value, D2D1::RectF(r.left + 12.0f, r.top + 24.0f, r.right - 44.0f, r.top + 50.0f), g.Primary(), 20.0f,
                   DWRITE_FONT_WEIGHT_SEMI_BOLD);
            g.Text(labels.note, D2D1::RectF(r.left + 12.0f, r.top + 50.0f, r.right - 10.0f, r.bottom - 4.0f), g.Secondary(), 11.0f,
                   DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
            if (!forecast) continue;
            const auto& c = forecast->current;
            if (tile == Tile::Wind && c.windDirection) {
                // Where the wind blows to: the arrow points downwind.
                const auto center = D2D1::Point2F(r.right - 28.0f, r.top + 38.0f);
                g.dc->DrawEllipse(D2D1::Ellipse(center, 15.0f, 15.0f), g.Paint(hc ? tokens.textSecondary : g.Secondary().WithAlpha(0.5f)), 1.0f);
                const double angle = (*c.windDirection + 180.0) * std::numbers::pi / 180.0;
                const float dx = static_cast<float>(std::sin(angle)), dy = static_cast<float>(-std::cos(angle));
                const auto tip = D2D1::Point2F(center.x + dx * 11.0f, center.y + dy * 11.0f);
                const auto tail = D2D1::Point2F(center.x - dx * 11.0f, center.y - dy * 11.0f);
                auto* ink = g.Paint(hc ? tokens.textPrimary : tokens.accentRoles.fill);
                g.dc->DrawLine(tail, tip, ink, 2.0f);
                const auto left = D2D1::Point2F(tip.x - dx * 5.0f + dy * 4.0f, tip.y - dy * 5.0f - dx * 4.0f);
                const auto right = D2D1::Point2F(tip.x - dx * 5.0f - dy * 4.0f, tip.y - dy * 5.0f + dx * 4.0f);
                g.dc->DrawLine(tip, left, ink, 2.0f);
                g.dc->DrawLine(tip, right, ink, 2.0f);
            }
            if (tile == Tile::Uv && !forecast->daily.empty() && forecast->daily.front().uvIndex) {
                const auto bar = D2D1::RectF(r.right - 40.0f, r.top + 30.0f, r.right - 34.0f, r.bottom - 12.0f);
                if (hc) {
                    g.Stroke(bar, 3.0f, tokens.textPrimary);
                } else {
                    const std::array<D2D1_GRADIENT_STOP, 5> gradient{{{0.0f, Color::FromHex(0x8E44AD).ToD2D()},
                                                                      {0.25f, Color::FromHex(0xE74C3C).ToD2D()},
                                                                      {0.5f, Color::FromHex(0xF39C12).ToD2D()},
                                                                      {0.75f, Color::FromHex(0xF1C40F).ToD2D()},
                                                                      {1.0f, Color::FromHex(0x2ECC71).ToD2D()}}};
                    winrt::com_ptr<ID2D1GradientStopCollection> stops;
                    winrt::com_ptr<ID2D1LinearGradientBrush> fill;
                    if (SUCCEEDED(g.dc->CreateGradientStopCollection(gradient.data(), 5, stops.put())) &&
                        SUCCEEDED(g.dc->CreateLinearGradientBrush(
                            D2D1::LinearGradientBrushProperties(D2D1::Point2F(bar.left, bar.top), D2D1::Point2F(bar.left, bar.bottom)), stops.get(),
                            fill.put())))
                        g.dc->FillRoundedRectangle(D2D1::RoundedRect(bar, 3.0f, 3.0f), fill.get());
                }
                const float level = std::clamp(static_cast<float>(*forecast->daily.front().uvIndex / 11.0), 0.0f, 1.0f);
                const float y = bar.bottom - level * (bar.bottom - bar.top);
                g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F((bar.left + bar.right) / 2.0f, y), 5.0f, 5.0f), g.Paint(g.Primary()));
            }
            if (tile == Tile::Humidity && c.humidity) {
                const auto bar = D2D1::RectF(r.right - 40.0f, r.top + 30.0f, r.right - 34.0f, r.bottom - 12.0f);
                g.Fill(bar, 3.0f, hc ? tokens.surface.controlFill : Light() ? Black(0.08f) : White(0.1f));
                const float level = std::clamp(static_cast<float>(*c.humidity) / 100.0f, 0.0f, 1.0f);
                g.Fill(D2D1::RectF(bar.left, bar.bottom - level * (bar.bottom - bar.top), bar.right, bar.bottom), 3.0f,
                       hc ? tokens.textPrimary : g.RainColor());
            }
        }
    }
    void PaintSunArc(Painter& g, const D2D1_RECT_F& box) {
        if (!forecast || forecast->daily.empty() || !forecast->daily.front().sunrise || !forecast->daily.front().sunset) return;
        const auto& today = forecast->daily.front();
        const double rise = static_cast<double>(today.sunrise->time_since_epoch().count());
        const double set = static_cast<double>(today.sunset->time_since_epoch().count());
        const double now = static_cast<double>(forecast->LocalNow(chr::system_clock::now()).time_since_epoch().count());
        const bool hc = HighContrast();
        const float horizon = box.bottom - 2.0f;
        const float cx = (box.left + box.right) / 2.0f, rx = (box.right - box.left) / 2.0f, ry = std::max(8.0f, horizon - box.top - 2.0f);
        g.dc->DrawLine(D2D1::Point2F(box.left - 8.0f, horizon), D2D1::Point2F(box.right + 8.0f, horizon),
                       g.Paint(hc ? tokens.textSecondary : g.Secondary().WithAlpha(0.45f)), 1.0f);
        winrt::com_ptr<ID2D1Factory> factory;
        g.dc->GetFactory(factory.put());
        winrt::com_ptr<ID2D1PathGeometry> arc;
        winrt::com_ptr<ID2D1GeometrySink> sink;
        if (factory && SUCCEEDED(factory->CreatePathGeometry(arc.put())) && SUCCEEDED(arc->Open(sink.put()))) {
            sink->BeginFigure(D2D1::Point2F(box.left, horizon), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(box.right, horizon), D2D1::SizeF(rx, ry), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close())) {
                winrt::com_ptr<ID2D1StrokeStyle> dashed;
                const float dashes[] = {2.0f, 3.0f};
                factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                       D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f),
                                           dashes, 2, dashed.put());
                g.dc->DrawGeometry(arc.get(), g.Paint(hc ? tokens.textSecondary : g.Secondary().WithAlpha(0.7f)), 1.2f, dashed.get());
            }
        }
        if (set <= rise) return;
        const double t = (now - rise) / (set - rise);
        if (t < 0.0 || t > 1.0) return;  // the sun is down: only its path shows
        const double angle = std::numbers::pi * (1.0 - t);
        const auto sun = D2D1::Point2F(cx + rx * static_cast<float>(std::cos(angle)), horizon - ry * static_cast<float>(std::sin(angle)));
        g.dc->FillEllipse(D2D1::Ellipse(sun, 9.0f, 9.0f), g.Paint(hc ? tokens.textPrimary.WithAlpha(0.3f) : Color::FromHex(0xFFC83D, 0.3f)));
        g.dc->FillEllipse(D2D1::Ellipse(sun, 5.5f, 5.5f), g.Paint(hc ? tokens.textPrimary : Color::FromHex(0xFFC83D)));
    }
    void PaintFooter(Painter& g) {
        const auto link = AttributionRect();
        g.Highlight(kAttribution, link, 4.0f);
        const Color linkColor = HighContrast() ? tokens.textPrimary : tokens.accentRoles.fill;
        g.Text(T(L"Weather data by Open-Meteo.com"), link, linkColor, 11.0f);
        if (hovered == kAttribution)
            g.dc->DrawLine(D2D1::Point2F(link.left, link.bottom - 5.0f), D2D1::Point2F(link.right - 4.0f, link.bottom - 5.0f), g.Paint(linkColor), 1.0f);
        g.Focus(kAttribution);
        g.Text(T(L"Map: Natural Earth"), D2D1::RectF(link.right + 12.0f, kFooterTop, link.right + 240.0f, kFooterBottom), g.Secondary(), 11.0f);
        if (forecast)
            g.Text(i18n::TrFormat(L"Updated {}", FormatDeviceClock(forecast->fetchedAt)),
                   D2D1::RectF(kRightX, kFooterTop, kRightX + kRightW, kFooterBottom), g.Secondary(), 11.0f, DWRITE_FONT_WEIGHT_NORMAL,
                   DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    // --- the hidden native edit behind the drawn search field ---
    void PlaceEdit() {
        if (!edit) return;
        const auto input = InputRect();
        const float factor = scale * static_cast<float>(owner.Dpi()) / 96.0f;
        ::SetWindowPos(edit, nullptr, static_cast<int>(std::lround((input.left + kMargin) * factor)),
                       static_cast<int>(std::lround((input.top + kMargin) * factor)),
                       std::max(1, static_cast<int>(std::lround((input.right - input.left) * factor))),
                       std::max(1, static_cast<int>(std::lround((input.bottom - input.top) * factor))), SWP_NOZORDER | SWP_NOACTIVATE);
        const int pixels = static_cast<int>(std::lround(13.0f * factor));
        if (pixels == editFontPx && editFont) return;
        HFONT font = ::CreateFontW(-pixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!font) return;
        ::SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (editFont) ::DeleteObject(editFont);
        editFont = font;
        editFontPx = pixels;
    }
    void NameEdit() {
        if (!edit) return;
        ::SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(i18n::Tr(L"Search for a city").data()));
        winrt::com_ptr<IAccPropServices> properties;
        if (SUCCEEDED(::CoCreateInstance(kClsidAccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(properties.put()))) && properties) {
            const std::wstring name = T(L"Search for a city");
            (void)properties->SetHwndPropStr(edit, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, kPropIdAccName, name.c_str());
        }
    }
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR reference) {
        auto* self = reinterpret_cast<Impl*>(reference);
        if (message == WM_NCDESTROY) ::RemoveWindowSubclass(hwnd, &EditProc, 1);
        if (message == WM_KEYDOWN && self) {
            const auto key = static_cast<UINT>(wParam);
            if (key == VK_TAB || key == VK_ESCAPE || key == VK_UP || key == VK_DOWN || key == VK_RETURN) {
                if (self->owner.OnKeyDown(key)) return 0;
            }
        }
        if (message == WM_CHAR && (wParam == L'\t' || wParam == L'\r' || wParam == 0x1B)) return 0;  // no beep
        const auto result = ::DefSubclassProc(hwnd, message, wParam, lParam);
        if (self && (message == WM_KEYDOWN || message == WM_CHAR || message == WM_LBUTTONUP || message == WM_MOUSEMOVE || message == EM_SETSEL ||
                     message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
            self->caretVisible = true;
            if (message == WM_SETFOCUS) {
                self->focused = kSearch;
                const auto blink = ::GetCaretBlinkTime();
                if (blink && blink != INFINITE) ::SetTimer(self->owner.Handle(), kCaretTimer, blink, nullptr);
                if (self->TrimmedQuery().size() >= 2) self->dropdown = true;
                self->Rebuild();
            }
            if (message == WM_KILLFOCUS) ::KillTimer(self->owner.Handle(), kCaretTimer);
            self->owner.Invalidate();
        }
        return result;
    }
};

WeatherWindow::WeatherWindow(WeatherWindowContext context)
    : SurfaceWindow(context.device, context.scheduler), impl_(std::make_unique<Impl>(*this, context)) {}

WeatherWindow::~WeatherWindow() {
    Destroy();
    impl_.reset();
}

Result<void> WeatherWindow::Initialize() {
    auto& p = *impl_;
    if (!p.context.dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "the weather window requires the UI thread");
    aether::ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.Weather";
    options.title = std::wstring(i18n::Tr(L"Weather"));
    options.noActivate = false;
    if (auto created = Create(options, RECT{0, 0, 908, 736}); !created) return created;
    p.text.Initialize(Device().DWriteFactory(), p.tokens.fontFamily, p.tokens.displayFontFamily);
    p.edit = ::CreateWindowExW(WS_EX_LAYERED, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, Handle(),
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), ::GetModuleHandleW(nullptr), nullptr);
    if (!p.edit) return MakeError(ErrorCode::Platform, "weather search field could not be created");
    if (!::SetLayeredWindowAttributes(p.edit, 0, 0, LWA_ALPHA)) return MakeError(ErrorCode::Platform, "weather search field transparency failed");
    ::SendMessageW(p.edit, EM_SETLIMITTEXT, 100, 0);
    ::SetWindowSubclass(p.edit, &Impl::EditProc, 1, reinterpret_cast<DWORD_PTR>(impl_.get()));
    p.NameEdit();
    p.Rebuild();
    return p.accessible.Connect(Handle(), p);
}

Result<void> WeatherWindow::Open(const RECT& anchorPx, bool dropDown, bool keyboard) {
    auto& p = *impl_;
    if (!p.context.dispatcher.IsUiThread() || p.destroyed || !Handle()) return MakeError(ErrorCode::InvalidState, "weather window unavailable");
    if (IsShown()) {
        ::SetForegroundWindow(Handle());
        return Ok();
    }
    // Move the hidden window to the anchor's monitor first so that Dpi() is that monitor's.
    SetBoundsPx({anchorPx.left, anchorPx.top, anchorPx.left + 1, anchorPx.top + 1});
    p.anchor = anchorPx;
    const auto monitor = platform::QueryMonitor(::MonitorFromRect(&anchorPx, MONITOR_DEFAULTTONEAREST)).value_or(platform::PrimaryMonitor());
    const float dpiScale = static_cast<float>(Dpi()) / 96.0f;
    const auto& area = monitor.workArea;
    const float width = kPanelWidth + 2.0f * kMargin, height = kPanelHeight + 2.0f * kMargin;
    const LONG edge = static_cast<LONG>(std::lround(8.0f * dpiScale));
    // The panel fits the work area, below the Top Bar's weather item when it drops from it; a screen too
    // short for that opens it centred instead.
    const auto fitBelow = [&](LONG top) {
        return std::min({1.0f, static_cast<float>(area.right - area.left - 2 * edge) / (kPanelWidth * dpiScale),
                         static_cast<float>(area.bottom - edge - top) / (kPanelHeight * dpiScale)});
    };
    bool below = dropDown;
    LONG panelTop = below ? std::clamp(anchorPx.bottom + static_cast<LONG>(std::lround(6.0f * dpiScale)), area.top, area.bottom) : area.top + edge;
    float fit = fitBelow(panelTop);
    if (below && fit < 0.5f) {
        below = false;
        panelTop = area.top + edge;
        fit = fitBelow(panelTop);
    }
    if (fit < 0.5f) return MakeError(ErrorCode::Unavailable, "the screen is too small for the weather window");
    p.scale = fit;
    const LONG w = static_cast<LONG>(std::ceil(width * fit * dpiScale)), h = static_cast<LONG>(std::ceil(height * fit * dpiScale));
    const LONG margin = static_cast<LONG>(std::lround(kMargin * fit * dpiScale));
    const LONG panelW = w - 2 * margin, panelH = h - 2 * margin;
    LONG px = below ? (anchorPx.left + anchorPx.right) / 2 - panelW / 2 : area.left + (area.right - area.left - panelW) / 2;
    LONG py = below ? panelTop : area.top + (area.bottom - area.top - panelH) / 2;
    px = std::clamp(px, area.left + edge, std::max(area.left + edge, area.right - edge - panelW));
    py = std::clamp(py, area.top, std::max(area.top, area.bottom - edge - panelH));
    const LONG x = px - margin, y = py - margin;
    SetBoundsPx({x, y, x + w, y + h});
    const RECT region{margin, margin, w - margin, h - margin};
    SetInputRegionPx(std::span<const RECT>(&region, 1));
    SetVisualRegionPx(std::span<const RECT>(&region, 1));
    p.PlaceEdit();
    // Every opening shows the home place; a city searched before is forgotten with the window.
    p.ClearSearch();
    const auto home = p.HomePlace();
    const bool samePlace = Impl::SamePlace(home, p.place) && !p.place.searched;
    p.place = home;
    const double age = p.forecast ? std::chrono::duration<double>(std::chrono::system_clock::now() - p.forecast->fetchedAt).count() : 1e9;
    const bool fresh = p.forecast && Impl::SamePlace(p.forecastFor, home) && age < kFreshSeconds && p.forecast->unit == p.home.unit;
    if (!samePlace && !Impl::SamePlace(p.forecastFor, home)) p.forecast.reset();
    if (!fresh) p.Load(p.forecast.has_value());
    p.cues = keyboard;
    p.focused.reset();
    p.hovered.reset();
    p.pressed.reset();
    p.hoveredSample.reset();
    p.mapLayer = nullptr;
    p.Rebuild();
    if (keyboard) p.focused = kSearch;
    graphics::TransitionShape from;
    from.offsetY = below ? -8.0f : 8.0f;
    from.scale = 0.985f;
    from.centerX = static_cast<float>(w) / 2.0f / dpiScale;
    from.centerY = below ? 0.0f : static_cast<float>(h) / 2.0f / dpiScale;
    Invalidate();
    ShowAnimated(graphics::MakeEnterTransition(from, p.tokens, p.motion), true);
    ::SetForegroundWindow(Handle());
    if (keyboard && p.edit) ::SetFocus(p.edit);
    ::SetTimer(Handle(), kRefreshTimer, kRefreshMs, nullptr);
    p.Wake();
    return Ok();
}

Result<void> WeatherWindow::Toggle(const RECT& anchorPx, bool keyboard) {
    if (IsShown()) {
        Close();
        return Ok();
    }
    return Open(anchorPx, true, keyboard);
}

void WeatherWindow::Close() {
    auto& p = *impl_;
    if (p.closing) return;
    p.closing = true;
    const bool shown = IsShown();
    if (Handle()) {
        ::KillTimer(Handle(), kRefreshTimer);
        ::KillTimer(Handle(), kSearchTimer);
        ::KillTimer(Handle(), kCaretTimer);
    }
    p.ClearSearch();
    p.hovered.reset();
    p.pressed.reset();
    p.hoveredSample.reset();
    p.permissionUntil = 0.0;
    p.stepping = false;
    p.energy = 0.0f;
    Hide();
    p.closing = false;
    if (shown) {
        try {
            Closed.Emit();
        } catch (...) {
        }
    }
}

bool WeatherWindow::IsOpen() const noexcept { return IsShown(); }

void WeatherWindow::SetHome(const WeatherHome& home) {
    auto& p = *impl_;
    const auto before = p.home;
    p.home = home;
    if (!home.locating && before.locating) p.permissionUntil = 0.0;
    const bool moved = before.source != home.source || std::abs(before.latitude - home.latitude) > 0.02 ||
                       std::abs(before.longitude - home.longitude) > 0.02 || before.name != home.name || before.unit != home.unit;
    if (moved && !p.place.searched) {
        p.place = p.HomePlace();
        if (!Impl::SamePlace(p.forecastFor, p.place) || before.unit != home.unit) p.forecast.reset();
        p.nearest.clear();
        if (IsShown()) p.Load(false);
    } else if (before.unit != home.unit) {
        p.forecast.reset();
        if (IsShown()) p.Load(false);
    }
    p.mapLayer = nullptr;
    p.Rebuild();
    if (IsShown()) {
        (void)p.accessible.NotifyStructureChanged();
        Invalidate();
    }
}

void WeatherWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    auto& p = *impl_;
    p.tokens = tokens;
    p.motion = motion;
    p.text.Initialize(Device().DWriteFactory(), tokens.fontFamily, tokens.displayFontFamily);
    p.mapLayer = nullptr;
    if (!motion.AllowsMovement() || p.HighContrast()) p.energy = 0.0f;
    p.Rebuild();
    if (IsShown()) {
        p.Wake();
        Invalidate();
    }
}

void WeatherWindow::Relocalize() {
    auto& p = *impl_;
    if (Handle()) ::SetWindowTextW(Handle(), std::wstring(i18n::Tr(L"Weather")).c_str());
    p.NameEdit();
    p.Rebuild();
    if (IsShown()) {
        (void)p.accessible.NotifyStructureChanged();
        Invalidate();
    }
}

std::uint64_t WeatherWindow::RenderCount() const noexcept { return impl_->renders; }
bool WeatherWindow::Animating() const noexcept { return impl_->stepping && impl_->energy > 0.0f; }

RECT WeatherWindow::PanelBoundsPx() const noexcept {
    if (!IsShown()) return RECT{};
    return impl_->ToScreen(D2D1::RectF(0.0f, 0.0f, kPanelWidth, kPanelHeight));
}

void WeatherWindow::OnRender(const aether::ui::RenderContext& context) {
    auto& p = *impl_;
    ++p.renders;
    auto* dc = context.d2d;
    const float fit = p.scale;
    const auto panel = D2D1::RectF(kMargin * fit, kMargin * fit, (kMargin + kPanelWidth) * fit, (kMargin + kPanelHeight) * fit);
    // D79: Liquid's regular glass under its light when Liquid is chosen; Grain's frosted glass otherwise.
    const bool hc = p.HighContrast();
    const bool blur = !hc && p.tokens.transparencyEnabled;
    const bool wantGlass = blur && p.tokens.liquid;
    SetBackdropPanel(panel, kRadius * fit, blur, wantGlass ? p.tokens.liquidGlass.blurOpacity : p.tokens.glass.backdropOpacity,
                     wantGlass ? &p.tokens.liquidGlass : nullptr);
    p.liquid = wantGlass && BackdropGlassActive() && PaintLiquidPanel(context, panel, kRadius * fit, 0.0f);
    if (!p.liquid) {
        graphics::GlassStyle glass;
        glass.cornerRadius = kRadius * fit;
        glass.opacity = 0.9f;
        glass.backdropBlur = BackdropBlurActive();
        glass.shadow = false;
        glass.elevation = graphics::Elevation::None;
        glass.neutral = true;
        glass.highlight = false;
        graphics::PaintGlassPanel(dc, panel, p.tokens, glass);
    }
    D2D1_MATRIX_3X2_F old{};
    dc->GetTransform(&old);
    dc->SetTransform(D2D1::Matrix3x2F::Translation(kMargin, kMargin) * D2D1::Matrix3x2F::Scale(fit, fit) * old);
    Impl::Painter g{p, dc, nullptr};
    if (FAILED(dc->CreateSolidColorBrush(p.tokens.textPrimary.ToD2D(), g.brush.put()))) {
        dc->SetTransform(old);
        return;
    }
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    float dpiX = 96.0f, dpiY = 96.0f;
    dc->GetDpi(&dpiX, &dpiY);
    p.PaintHero(g);
    p.PaintHourly(g);
    p.PaintDaily(g);
    p.PaintBanner(g);
    p.PaintMap(g, fit * dpiX / 96.0f);
    p.PaintTiles(g);
    p.PaintFooter(g);
    p.PaintHeader(g);
    p.PaintDropdown(g);
    dc->SetTransform(old);
}

bool WeatherWindow::OnAnimate(double now) { return impl_->Step(now); }

void WeatherWindow::OnPointerMove(D2D1_POINT_2F point) {
    auto& p = *impl_;
    p.Wake();
    const auto panel = p.ToPanel(point);
    const auto hit = p.Hit(panel);
    std::optional<std::size_t> sample;
    if (!hit && p.map && p.view && Contains(p.MapArea(), panel)) {
        const auto projection = MakeProjection(*p.view, p.MapArea());
        for (const auto& pill : p.Pills(projection))
            if (Contains(pill.rect, panel)) sample = pill.sample;
    }
    if (hit != p.hovered || sample != p.hoveredSample) {
        p.hovered = hit;
        p.hoveredSample = sample;
        if (hit && *hit >= kResult) p.selected = static_cast<std::size_t>(*hit - kResult);
        Invalidate();
    }
}

void WeatherWindow::OnPointerLeave() {
    auto& p = *impl_;
    if (p.hovered || p.hoveredSample) {
        p.hovered.reset();
        p.hoveredSample.reset();
        Invalidate();
    }
}

void WeatherWindow::OnPointerDown(aether::ui::PointerButton button, D2D1_POINT_2F point) {
    auto& p = *impl_;
    p.Wake();
    if (button != aether::ui::PointerButton::Left) return;
    const auto panel = p.ToPanel(point);
    const auto hit = p.Hit(panel);
    p.cues = false;
    if (hit == kSearch) {
        p.pressed.reset();
        if (p.edit) {
            ::SetFocus(p.edit);
            const auto length = ::GetWindowTextLengthW(p.edit);
            ::SendMessageW(p.edit, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
        }
        Invalidate();
        return;
    }
    // A click outside the field and its list closes the list and leaves the field.
    if (!hit || *hit < kResult) {
        if (p.dropdown) {
            p.dropdown = false;
            p.Rebuild();
        }
        if (p.edit && ::GetFocus() == p.edit) ::SetFocus(Handle());
    }
    p.pressed = hit;
    Invalidate();
}

void WeatherWindow::OnPointerUp(aether::ui::PointerButton button, D2D1_POINT_2F point) {
    auto& p = *impl_;
    if (button != aether::ui::PointerButton::Left) return;
    const auto hit = p.Hit(p.ToPanel(point));
    const auto pressed = p.pressed;
    p.pressed.reset();
    Invalidate();
    if (hit && pressed == hit) p.Activate(*hit);
}

void WeatherWindow::OnPointerCaptureLost() {
    impl_->pressed.reset();
    Invalidate();
}

void WeatherWindow::OnWheel(float, D2D1_POINT_2F) { impl_->Wake(); }

bool WeatherWindow::OnKeyDown(UINT key) {
    auto& p = *impl_;
    p.Wake();
    const bool inField = p.edit && ::GetFocus() == p.edit;
    const bool control = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (key == VK_ESCAPE) {
        if (p.DropdownShown()) {
            p.dropdown = false;
            p.Rebuild();
            Invalidate();
        } else if (inField && !p.query.empty()) {
            p.ClearSearch();
            p.Rebuild();
            Invalidate();
        } else {
            Close();
        }
        return true;
    }
    if (key == 'F' && control) {
        p.Focus(kSearch, true);
        if (p.edit) ::SendMessageW(p.edit, EM_SETSEL, 0, -1);
        return true;
    }
    if (key == VK_TAB) {
        p.FocusNext((::GetKeyState(VK_SHIFT) & 0x8000) != 0);
        return true;
    }
    if (p.DropdownShown() && !p.results.empty() && (key == VK_DOWN || key == VK_UP)) {
        const std::size_t count = std::min(p.results.size(), kMaxResults);
        const std::size_t current = p.selected.value_or(key == VK_DOWN ? count - 1 : 0);
        p.selected = key == VK_DOWN ? (current + 1) % count : (current + count - 1) % count;
        Invalidate();
        return true;
    }
    if (key == VK_RETURN && inField) {
        if (p.DropdownShown() && p.selected && *p.selected < p.results.size()) p.Pick(*p.selected);
        else if (p.TrimmedQuery().size() >= 2) p.RunSearch();
        return true;
    }
    if ((key == VK_RETURN || key == VK_SPACE) && p.focused && !inField) {
        p.cues = true;
        p.Activate(*p.focused);
        return true;
    }
    return false;
}

void WeatherWindow::OnFocusChanged(bool focused) {
    if (!focused && !impl_->closing && IsShown()) Invalidate();
}

void WeatherWindow::OnDpiChanged(UINT dpi, const RECT& suggested) {
    SurfaceWindow::OnDpiChanged(dpi, suggested);
    impl_->editFontPx = 0;
    impl_->mapLayer = nullptr;
    Close();
    SetBoundsPx(suggested);
}

std::optional<LRESULT> WeatherWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    auto& p = *impl_;
    if (message == WM_GETOBJECT) {
        LRESULT result = 0;
        if (p.accessible.HandleGetObject(Handle(), wParam, lParam, result)) return result;
    }
    if (message == kInvoke) {
        p.Activate(static_cast<int>(wParam));
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE && reinterpret_cast<HWND>(lParam) == p.edit) {
        p.QueryChanged();
        return 0;
    }
    if (message == WM_TIMER) {
        if (wParam == kSearchTimer) {
            p.RunSearch();
            return 0;
        }
        if (wParam == kCaretTimer) {
            p.caretVisible = !p.caretVisible;
            Invalidate();
            return 0;
        }
        if (wParam == kRefreshTimer) {
            if (IsShown()) p.Load(true);
            return 0;
        }
    }
    if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
        // Windows' location prompt, opened from the banner, may take the focus for a while.
        if (graphics::FrameScheduler::Now() < p.permissionUntil) return 0;
        Close();
        return 0;
    }
    if (message == WM_CLOSE) {
        Close();
        return 0;
    }
    return std::nullopt;
}

void WeatherWindow::OnCreated() {}

void WeatherWindow::OnDestroying() {
    auto& p = *impl_;
    p.destroyed = true;
    p.accessible.Disconnect();
    if (Handle()) {
        ::KillTimer(Handle(), kRefreshTimer);
        ::KillTimer(Handle(), kSearchTimer);
        ::KillTimer(Handle(), kCaretTimer);
    }
    p.edit = nullptr;
}

void WeatherWindow::OnDeviceReplaced() {
    impl_->mapLayer = nullptr;
    Invalidate();
}

}  // namespace aether::weather::ui
