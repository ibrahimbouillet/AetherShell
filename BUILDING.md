# Building Aether Shell

End users never need any of this: they install the published setup executable. These instructions
are for developers.

## Requirements

| Tool | Version used and verified | Notes |
|---|---|---|
| Windows | 11 x64 (build 26300) | Windows 10 2004+ is the target minimum |
| Visual Studio Build Tools | 2026 (MSVC 14.51) | "Desktop development with C++" workload; VS 2022 (MSVC 14.4x) is used by CI |
| Windows SDK | 10.0.26100 | includes the C++/WinRT headers |
| CMake | 3.25 or newer (4.4.3 verified) | bundled with Visual Studio or standalone |
| Ninja | 1.13 | bundled with Visual Studio or standalone |
| Python | 3.11+ (only for release scripts and branding renders) | standard library; Pillow + numpy for branding |

No third-party C++ libraries are downloaded.

## Configure, build and test

`scripts/build.ps1` finds Visual Studio with `vswhere`, imports the developer environment for the
requested architecture, configures with Ninja and builds. Run it from any PowerShell prompt:

```powershell
# Debug build of everything in build/debug, then unit + integration tests (network tests excluded)
./scripts/build.ps1 -Config debug -Test

# Optimized build in build/release
./scripts/build.ps1 -Config release

# One module and its dependencies only, in a private build folder
./scripts/build.ps1 -BuildDir build/wf-graphics -Modules graphics -Test -TestRegex graphics

# CI-equivalent run (warnings as errors, unit tests only, JUnit report)
./scripts/build.ps1 -Config release -BuildDir build/ci -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' `
    -Test -TestExclude 'network|integration' -JUnitFile build/ci/test-results.xml
```

Useful switches: `-Jobs <n>` (parallel compile jobs), `-Fresh` (delete the build folder first),
`-TestLabel unit|integration|network`, `-Arch arm64` (requires the MSVC ARM64 toolset; not yet verified).

Verification status: the module-scoped commands above have been run successfully for core,
diagnostics, platform, graphics and ui. The full-project build is verified once every module is
integrated (see `docs/knowledge/TEST_STATUS.md`).

From a "Developer PowerShell for VS" prompt the CMake presets can be used directly (not yet exercised
in this repository's verification runs):

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Test categories

| Label | Needs | Runs in CI |
|---|---|---|
| `unit` | nothing | yes |
| `integration` | an interactive Windows desktop (creates windows, captures the screen region of its own test windows) | no |
| `network` | Internet access (GitHub API, TLS) | no |

## Build options (CMake cache variables)

| Variable | Default | Meaning |
|---|---|---|
| `AETHER_MODULES` | `all` | semicolon list of modules; dependencies are added automatically |
| `AETHER_BUILD_TESTS` | `ON` | build test executables |
| `AETHER_WARNINGS_AS_ERRORS` | `OFF` | `/WX` (ON in CI) |
| `AETHER_VERSION_PRERELEASE` | `alpha.1` | SemVer prerelease suffix; empty for stable builds |
| `AETHER_UPDATE_REPO_OWNER` / `_NAME` | `ibrahimbouillet` / `AetherShell` | trusted release repository |
| `AETHER_SOURCE_REVISION` | git HEAD | revision embedded in the binaries |

## Output

Each module builds a static library `aether_<module>`; executables and tests are under
`build/<config>/modules/<module>/`. The static CRT is used, so binaries need no redistributable.
