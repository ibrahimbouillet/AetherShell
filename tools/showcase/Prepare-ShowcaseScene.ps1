# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
<#
.SYNOPSIS
    Prepares a private, isolated showcase scene without launching applications.
.DESCRIPTION
    Clones Aether settings and their genuine pinned application identities into a new local
    data directory. Only the Visual Studio Code launch route is changed, to a real shortcut
    that opens an empty isolated profile. Original settings and Code profiles are never edited.
    The returned object contains private local paths: keep it in memory, not public logs.
    No application is launched, closed, hidden or captured by this script.

    VS Code isolation: https://code.visualstudio.com/docs/configure/command-line
    Portable mode overrides isolation: https://code.visualstudio.com/docs/setup/portable
.PARAMETER SettingsFile
    The current Aether settings.json file to read without changing it.
.PARAMETER SceneRoot
    A new, nonexistent local directory. By default a unique directory is created below
    LocalAppData/AetherShell/Showcase/scenes. Existing directories are never reused.
.PARAMETER DesktopOnly
    Preserves all pins without requiring or isolating Code. Use for desktop, controls and Settings
    presentations that never launch another application. Code return paths are null in this mode.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SettingsFile,

    [Parameter()]
    [string]$SceneRoot,

    [switch]$DesktopOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-SceneProperty {
    param([object]$Object, [string]$Name)
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Set-SceneProperty {
    param([object]$Object, [string]$Name, [object]$Value)
    $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $Value -Force
}

function Resolve-SceneLocalPath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path -match '[\x00-\x1f\"]' -or
        $Path -notmatch '^[A-Za-z]:[\\/]') {
        throw 'Scene paths must be absolute local filesystem paths.'
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-SceneNoReparseAncestor {
    param([string]$Path)
    $current = $Path
    while (-not [string]::IsNullOrWhiteSpace($current)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Scene preparation does not follow filesystem reparse points.'
            }
        }
        $parent = [System.IO.Path]::GetDirectoryName($current)
        if ($parent -eq $current) { break }
        $current = $parent
    }
}

function Test-SceneCodeExecutable {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or
        [System.IO.Path]::GetFileName($Path) -ine 'Code.exe') { return $false }
    $candidate = Resolve-SceneLocalPath $Path
    Assert-SceneNoReparseAncestor $candidate
    if (-not [System.IO.File]::Exists($candidate)) { return $false }
    $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($candidate)
    return $version.ProductName -eq 'Visual Studio Code'
}

function Write-SceneNewUtf8File {
    param([string]$Path, [string]$Content)
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Content)
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length) }
    finally { $stream.Dispose() }
}

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw 'Showcase scene preparation requires Windows.'
}

$sourceSettings = Resolve-SceneLocalPath $SettingsFile
Assert-SceneNoReparseAncestor $sourceSettings
if (-not [System.IO.File]::Exists($sourceSettings)) {
    throw 'The source Aether settings file is unavailable.'
}
if ((Get-Item -LiteralPath $sourceSettings).Length -gt 1MB) {
    throw 'The source Aether settings file exceeds the supported size.'
}
$settings = [System.IO.File]::ReadAllText($sourceSettings) | ConvertFrom-Json
$dock = Get-SceneProperty $settings 'dock'
$pins = @(Get-SceneProperty $dock 'pinned')
$general = Get-SceneProperty $settings 'general'
if ($null -eq $dock -or $null -eq $general -or $pins.Count -eq 0) {
    throw 'Aether settings must contain the genuine pinned applications and general settings.'
}

$shell = $null
$codePin = $null
$codeExecutable = $null
try {
    if (-not $DesktopOnly) {
    foreach ($pin in $pins) {
        $executable = [string](Get-SceneProperty $pin 'executablePath')
        if (Test-SceneCodeExecutable $executable) {
            $codePin = $pin
            $codeExecutable = Resolve-SceneLocalPath $executable
            break
        }
        $shortcut = [string](Get-SceneProperty $pin 'shortcutPath')
        if ([string]::IsNullOrWhiteSpace($shortcut) -or
            [System.IO.Path]::GetExtension($shortcut) -ine '.lnk') { continue }
        $shortcut = Resolve-SceneLocalPath $shortcut
        Assert-SceneNoReparseAncestor $shortcut
        if (-not [System.IO.File]::Exists($shortcut)) { continue }
        if ($null -eq $shell) { $shell = New-Object -ComObject WScript.Shell }
        $sourceLink = $shell.CreateShortcut($shortcut)
        try {
            $target = [string]$sourceLink.TargetPath
            if (Test-SceneCodeExecutable $target) {
                $codePin = $pin
                $codeExecutable = Resolve-SceneLocalPath $target
                break
            }
        }
        finally { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($sourceLink) }
    }
    if ($null -eq $codePin -or [string]::IsNullOrWhiteSpace($codeExecutable)) {
        throw 'No genuine installed Visual Studio Code executable was found in the saved pins.'
    }
    if ([string]::IsNullOrWhiteSpace([string](Get-SceneProperty $codePin 'id')) -or
        [string]::IsNullOrWhiteSpace([string](Get-SceneProperty $codePin 'displayName'))) {
        throw 'The Visual Studio Code pin is missing its saved identity or display name.'
    }
    $codeDirectory = [System.IO.Path]::GetDirectoryName($codeExecutable)
    if (Test-Path -LiteralPath (Join-Path $codeDirectory 'data')) {
        throw 'Portable Visual Studio Code data overrides profile isolation; scene preparation stopped.'
    }
    }

    if ([string]::IsNullOrWhiteSpace($SceneRoot)) {
        $localRoot = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::LocalApplicationData)
        if ([string]::IsNullOrWhiteSpace($localRoot)) { throw 'Local application data is unavailable.' }
        $SceneRoot = Join-Path $localRoot ('AetherShell\Showcase\scenes\scene-' + [guid]::NewGuid().ToString('N'))
    }
    $SceneRoot = Resolve-SceneLocalPath $SceneRoot
    Assert-SceneNoReparseAncestor $SceneRoot
    if (Test-Path -LiteralPath $SceneRoot) {
        throw 'A showcase scene requires a new directory; existing scene data was left unchanged.'
    }
    $aetherDirectory = Join-Path $SceneRoot 'aether'
    $configDirectory = Join-Path $aetherDirectory 'config'
    $codeUserDirectory = Join-Path $SceneRoot 'code-user-data'
    $codeSettingsDirectory = Join-Path $codeUserDirectory 'User'
    $extensionsDirectory = Join-Path $SceneRoot 'code-extensions'
    $shortcutPath = Join-Path $SceneRoot 'Visual Studio Code.lnk'

    # Create only fresh scene-owned directories. Never copy the private Code profile or workspace.
    New-Item -ItemType Directory -Path $SceneRoot | Out-Null
    foreach ($directory in @($aetherDirectory, $configDirectory)) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }
    if (-not $DesktopOnly) {
    foreach ($directory in @($codeUserDirectory, $codeSettingsDirectory, $extensionsDirectory)) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }
    $codeSettings = [ordered]@{
        'workbench.startupEditor' = 'none'
        'window.restoreWindows' = 'none'
        'files.hotExit' = 'off'
        'telemetry.telemetryLevel' = 'off'
        'update.showReleaseNotes' = $false
        'workbench.welcomePage.walkthroughs.openOnInstall' = $false
    }
    Write-SceneNewUtf8File (Join-Path $codeSettingsDirectory 'settings.json') `
        (($codeSettings | ConvertTo-Json -Depth 4) + [System.Environment]::NewLine)

    # These quoted paths are generated by this script; no saved shortcut arguments are reused.
    # The scene's extensions directory is fresh and empty, so owner-installed add-ons are isolated
    # without --disable-extensions, whose warning toast would intrude into the product recording.
    # Built-in extensions keep their normal behavior; no app or operating-system security is changed.
    $arguments = '--new-window --user-data-dir "' + $codeUserDirectory +
        '" --extensions-dir "' + $extensionsDirectory +
        '" --skip-welcome --skip-release-notes'
    if ($null -eq $shell) { $shell = New-Object -ComObject WScript.Shell }
    $sceneLink = $shell.CreateShortcut($shortcutPath)
    try {
        $sceneLink.TargetPath = $codeExecutable
        $sceneLink.Arguments = $arguments
        $sceneLink.WorkingDirectory = $SceneRoot
        $sceneLink.IconLocation = $codeExecutable + ',0'
        $sceneLink.Description = 'Visual Studio Code isolated showcase window'
        $sceneLink.Save()
    }
    finally { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($sceneLink) }

    # Preserve pin identity, display name, executable and order, so its genuine icon remains.
    # Aether prefers AppsFolder identities over shortcuts: clear only those launch routes.
    Set-SceneProperty $codePin 'parsingName' ''
    Set-SceneProperty $codePin 'appUserModelId' ''
    Set-SceneProperty $codePin 'shortcutPath' $shortcutPath
    Set-SceneProperty $codePin 'arguments' $arguments
    }
    Set-SceneProperty $general 'replaceWindowsTaskbar' $true
    # Freeze the owner's selected pins in this private scene. Background taskbar imports must not
    # append other applications after the scene snapshot has been reviewed.
    Set-SceneProperty $general 'syncTaskbarPins' $false
    if ($null -ne $general.PSObject.Properties['startWithWindows']) {
        Set-SceneProperty $general 'startWithWindows' $false
    }
    if ($null -ne $general.PSObject.Properties['startup']) {
        Set-SceneProperty $general 'startup' $false
    }
    Write-SceneNewUtf8File (Join-Path $configDirectory 'settings.json') `
        (($settings | ConvertTo-Json -Depth 32) + [System.Environment]::NewLine)

    # Private runtime return value only. Do not serialize this object into repository evidence.
    [pscustomobject]@{
        SceneRoot = $SceneRoot
        AetherDataDirectory = $aetherDirectory
        CodeUserDataDirectory = $(if ($DesktopOnly) { $null } else { $codeUserDirectory })
        ShortcutPath = $(if ($DesktopOnly) { $null } else { $shortcutPath })
    }
}
finally {
    if ($null -ne $shell) { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) }
}
