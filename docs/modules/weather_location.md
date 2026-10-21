# Windows weather location

`weather_location` reads a general location from Windows for automatic weather. The first-use or
Settings surface explains that rounded coordinates are sent to the weather service and requires an
explicit Use Windows location action. Fresh default settings never establish consent. Existing
opt-outs and manually chosen locations remain host policy; this module never overwrites settings.

`BeginWindowsLocationAccess` runs only on the foreground owning UI STA, asks Windows asynchronously,
and delivers a Result to a callback that the host must guard and post to its dispatcher. Only an
actual allowed result may establish app consent. That app choice may persist across launches;
it is not proof of current Windows permission. Every worker read checks current LocationStatus and
the actual read remains subject to OS permission. No worker/startup path requests permission.

`ReadWindowsLocation` is serial MTA worker work, uses a requested 10-km accuracy and returns only
0.1-degree rounded coordinates. It requests a cache age of 30 minutes and independently validates
the returned timestamp, since Windows can exceed the requested cache age depending on accuracy.
Future/stale/invalid readings remain unavailable. Country stays unknown: no city/country, coordinates
or fallback place is invented. There is no IP/SSID lookup, reverse geocoder, logging or persistence.
Cancellation/deadline waits own a shared completion event and cancel on the worker; no late callback
captures the caller's stack or host. The host may refresh on resume and at a bounded 30-minute interval.

Microsoft documents unpackaged use without a location manifest capability, while packaged apps need
the location capability. Actual availability still depends on Windows privacy policy, location
services, runtime/device support and desktop consent behavior; denied or unsupported configurations
remain errors with a Windows location-privacy Settings route supplied by the host. No silent elevation
or privacy-setting changes are attempted. No successful real location has been claimed by automation.

Sources: [Windows location guidance](https://learn.microsoft.com/windows/apps/develop/maps-and-location/get-location),
[RequestAccessAsync](https://learn.microsoft.com/uwp/api/windows.devices.geolocation.geolocator.requestaccessasync),
[GetGeopositionAsync](https://learn.microsoft.com/uwp/api/windows.devices.geolocation.geolocator.getgeopositionasync).
Pure unit cases cover coarse rounding, invalid coordinates, freshness and admission failures;
valid runtime/location reads and real consent are never invoked by tests. Compilation and test results
must be recorded separately in TEST_STATUS/HANDOFF.
