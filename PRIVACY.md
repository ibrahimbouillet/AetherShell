# Privacy

Aether Shell runs on your PC for your Windows account. It has no account, no advertising and no telemetry: it never
reports what you do, what you open or how it is used. What it keeps stays on your PC.

## What goes over the network, and when

Aether Shell contacts two services, over HTTPS, and nothing else. Each request carries what any web request does
(your IP address, and a User-Agent naming Aether Shell and its version).

| Service | When | What it receives | Turn it off |
|---|---|---|---|
| **GitHub** (`api.github.com`, `github.com` and its download hosts) | Setup downloads the languages of your Windows display languages after installing; the application downloads a language you choose, or one Windows switches to, the first time it is needed. Update checks: automatic (2 minutes after start, then every 24 hours) unless turned off, or when you ask. | Which release and which files are asked for. | Settings → Updates → *Check for updates automatically*. Languages are only downloaded when one is missing. |
| **Open-Meteo** (`api.open-meteo.com`, `geocoding-api.open-meteo.com`) | Only when you turn weather on (it is off when Aether Shell is installed). | The coordinates of the place shown, the temperature unit; when you search for a place, the text you type and your language code. With your consent to Windows location, your position rounded to 0.001° (about 100 m) with *Precise location*, else to 0.1° (about 10 km). | Settings → Weather. Turning it off stops every request and removes the last place and reading from disk. |

Windows location is read only after you allow it, in the foreground, and always under Windows' own location settings.
Nothing about your windows or files, and nothing you type in Search, is sent anywhere: file search asks Windows' own
search index, on your PC.

The services' own policies: [GitHub General Privacy Statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement),
[Open-Meteo terms and privacy](https://open-meteo.com/en/terms).

## What stays on your PC

In your account's local application data: your settings (including the optional first name the welcome greets you
with), Aether Shell's logs, the last weather reading while weather is on, downloaded language packs and update files.
None of it is sent anywhere. Uninstalling removes Aether Shell's programs and keeps this data unless you choose
*Also remove my settings, cache and logs*; only what Aether Shell made is removed.

## Code signing

How releases are signed, and by whom: see the [code signing policy](README.md#code-signing-policy).
