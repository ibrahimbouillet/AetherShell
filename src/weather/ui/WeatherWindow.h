// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: Aether Shell's own weather page, opened from the Top Bar's weather item and from Settings.
//
//   * the forecast where the person is: a sky that follows the place's hour (night blue, dawn, light
//     morning blue, day, dusk), the condition's animated illustration in colour, the temperature, the
//     next 24 hours, seven days with their range, and details (feels like, humidity, wind, sun, UV,
//     pressure);
//   * a map of the country with the temperature in its towns, the person's position on it;
//   * any other city, searched by name, and back to the person's own position;
//   * the location permission: allow precise location from here, or open Windows' location settings.
//
// Data: Open-Meteo through ForecastClient/GeocodingClient on this window's own worker, only while it is
// open (and every 15 minutes while it stays open); the map is offline (WorldMap). The window never reads
// the device's position itself: the host gives it the home place and handles the permission requests.
// Motion follows MotionPolicy: nothing moves under reduced motion or high contrast, and the motion
// rests after a minute without input.
#pragma once

#include "core/Dispatcher.h"
#include "core/Signal.h"
#include "graphics/Animation.h"
#include "graphics/Theme.h"
#include "status/StatusTypes.h"
#include "ui/SurfaceWindow.h"
#include "weather/ForecastClient.h"
#include "weather/GeocodingClient.h"

#include <memory>
#include <optional>
#include <string>

namespace aether::weather::ui {

// Where the person's own weather is, as the host knows it.
enum class HomeSource {
    None,             // no position: weather is off, or automatic location is not available yet
    PreciseLocation,  // Windows location, precise (D78)
    ApproximateLocation,  // Windows location, rounded to about 10 km
    ChosenCity,       // the place chosen in Settings
};

// Whether this app may read the device's position (for the permission banner).
enum class LocationPermission {
    Unknown,
    Granted,      // Windows allows it and the person agreed in Aether Shell
    NotAsked,     // the person has not agreed in Aether Shell yet: "Allow precise location"
    Denied,       // Windows or the person refused: "Open location settings"
    Unavailable,  // no location on this device
};

struct WeatherHome {
    HomeSource source = HomeSource::None;
    double latitude = 0.0;
    double longitude = 0.0;
    std::wstring name;  // the chosen city's label; empty for a device position (the map names it)
    LocationPermission permission = LocationPermission::Unknown;
    bool locating = false;  // a location read is under way
    status::TemperatureUnit unit = status::TemperatureUnit::Celsius;
};

struct WeatherWindowContext {
    graphics::GraphicsDevice& device;
    graphics::FrameScheduler& scheduler;
    IUiDispatcher& dispatcher;
    ForecastClient* forecast = nullptr;    // null: the forecast is unavailable
    GeocodingClient* geocoding = nullptr;  // null: no city search
};

class WeatherWindow final : public aether::ui::SurfaceWindow {
public:
    explicit WeatherWindow(WeatherWindowContext context);
    ~WeatherWindow() override;

    [[nodiscard]] Result<void> Initialize();
    // Opens below `anchorPx` (the Top Bar's weather item, screen pixels), or centred on the monitor
    // of `anchorPx` when `dropDown` is false (from Settings). Shows the home place's weather.
    [[nodiscard]] Result<void> Open(const RECT& anchorPx, bool dropDown, bool keyboard = false);
    // Opens or closes, as the Top Bar's weather item does.
    [[nodiscard]] Result<void> Toggle(const RECT& anchorPx, bool keyboard = false);
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    void SetHome(const WeatherHome& home);
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);
    void Relocalize();

    // Diagnostics seams.
    [[nodiscard]] std::uint64_t RenderCount() const noexcept;
    [[nodiscard]] RECT PanelBoundsPx() const noexcept;
    // Whether the illustrations are moving now (false at rest, under reduced motion and high contrast).
    [[nodiscard]] bool Animating() const noexcept;

    Signal<> LocationAccessRequested;    // "Allow precise location": ask Windows now, from this window
    Signal<> LocationSettingsRequested;  // Windows Settings > Privacy > Location
    Signal<> WeatherSettingsRequested;   // Aether Shell's weather settings
    Signal<std::wstring> LinkRequested;  // open this web address (the data's attribution)
    Signal<> Closed;

protected:
    void OnRender(const aether::ui::RenderContext& context) override;
    bool OnAnimate(double now) override;
    void OnPointerMove(D2D1_POINT_2F point) override;
    void OnPointerLeave() override;
    void OnPointerDown(aether::ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerUp(aether::ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerCaptureLost() override;
    void OnWheel(float notches, D2D1_POINT_2F point) override;
    bool OnKeyDown(UINT key) override;
    void OnFocusChanged(bool focused) override;
    void OnDpiChanged(UINT dpi, const RECT& suggested) override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnCreated() override;
    void OnDestroying() override;
    void OnDeviceReplaced() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::weather::ui
