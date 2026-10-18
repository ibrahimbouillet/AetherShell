# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
[CmdletBinding()]
param(
    [string]$BuildDir = 'build/package',
    [string]$OutputDir = 'dist',
    [ValidateSet('x64','arm64')][string]$Arch = 'x64',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
$packageStage = 'configuration validation'
try {
    if (-not $SkipBuild) {
        $packageStage = 'Release build'
        & ./scripts/build.ps1 -Config release -BuildDir $BuildDir -Modules installer -Arch $Arch `
            -Target AetherShellSetup -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON'
    }
    $packageStage = 'configuration validation'
    $buildPath = if ([IO.Path]::IsPathRooted($BuildDir)) { [IO.Path]::GetFullPath($BuildDir) } else { [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDir)) }
    $outputPath = if ([IO.Path]::IsPathRooted($OutputDir)) { [IO.Path]::GetFullPath($OutputDir) } else { [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDir)) }
    $cache = Get-Content -LiteralPath (Join-Path $buildPath 'CMakeCache.txt') -Raw
    if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\s*$') { throw 'Packaging requires a Release build.' }
    $info = Get-Content -LiteralPath (Join-Path $buildPath 'generated/aether/BuildInfo.h') -Raw
    if ($info -notmatch 'kVersion = "([^"]+)"') { throw 'Cannot read the built version.' }
    $version = $Matches[1]
    if ($info -notmatch 'kTargetArchitecture = "([^"]+)"' -or $Matches[1] -ne $Arch) { throw 'The built architecture differs from the requested package.' }
    $channel = if ($version.Contains('-')) { 'prerelease' } else { 'stable' }
    $inputSetup = Join-Path $buildPath 'modules/installer/AetherShellSetup.exe'
    # Deny replacement between privacy audit and copy. Raw setup bytes include every uncompressed
    # RCDATA payload; audit_package never loads or executes the application or installer.
    $lockedSetup = [IO.File]::Open($inputSetup, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $packageStage = 'binary privacy and source freshness audit'
        & python ./scripts/release/audit_package.py $inputSetup --source-root $projectRoot --check-freshness --version $version
        if ($LASTEXITCODE -ne 0) { throw 'Package audit rejected the build.' }
        $packageStage = 'asset copy'
        New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
        $setup = Join-Path $outputPath "AetherShell-$version-win-$Arch-setup.exe"
        Copy-Item -LiteralPath $inputSetup -Destination $setup
    } finally { $lockedSetup.Dispose() }
    $packageStage = 'manifest generation'
    & python ./scripts/release/make_manifest.py --version $version --channel $channel $setup
    if ($LASTEXITCODE -ne 0) { throw 'Manifest generation failed.' }
    $manifest = Join-Path $outputPath "AetherShell-$version-manifest.json"
    $files = @($setup)
    if ([Environment]::GetEnvironmentVariable('AETHER_RELEASE_SIGNING_KEY_FILE')) {
        $packageStage = 'manifest signing'
        & python ./scripts/release/sign_manifest.py $manifest
        if ($LASTEXITCODE -ne 0) { throw 'Manifest signing or trust-anchor verification failed.' }
        $files += @($manifest,"$manifest.sig")
        Write-Host 'Manifest signature verified against the compiled public trust anchor.'
    } else {
        Write-Warning 'UNSIGNED MANIFEST: keep the manifest local. Do not publish it or enable automatic updates until it is signed with the trusted release key.'
    }
    $packageStage = 'checksum generation'
    & python ./scripts/release/make_checksums.py --version $version @files
    if ($LASTEXITCODE -ne 0) { throw 'Checksum generation failed.' }
    Write-Host "Package prepared for review: AetherShell-$version-win-$Arch-setup.exe"
} catch {
    # PowerShell's default exception rendering can expose user-specific source/input paths.
    [Console]::Error.WriteLine("package: failed during $packageStage; check the local inputs and rebuild if needed.")
    exit 1
} finally { Pop-Location }
