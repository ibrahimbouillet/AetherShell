# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
<#
.SYNOPSIS
    Configures, builds and optionally tests Aether Shell inside a Visual Studio developer environment.

.DESCRIPTION
    Locates Visual Studio (2022 or later, including Build Tools) with vswhere, imports the
    vcvarsall environment for the requested architecture into this process, then runs
    CMake (Ninja generator) and, with -Test, CTest.

.EXAMPLE
    ./scripts/build.ps1 -Config debug -Test

.EXAMPLE
    ./scripts/build.ps1 -BuildDir build/wf-status -Modules status -Target aether_status_tests -Test -TestRegex status
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'relwithdebinfo')]
    [string]$Config = 'debug',
    [string]$BuildDir = '',
    [string]$Modules = 'all',
    [string[]]$Target = @(),
    [switch]$Test,
    [string]$TestLabel = '',
    [string]$TestExclude = 'network',
    [string]$TestRegex = '',
    [string]$JUnitFile = '',
    [int]$Jobs = 0,
    [ValidateSet('x64', 'arm64')]
    [string]$Arch = 'x64',
    [switch]$Fresh,
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot

function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments, [string]$What)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) { throw "$What failed with exit code $code" }
}

function Import-VsDevEnvironment {
    param([string]$TargetArch)
    if ($env:VSCMD_ARG_TGT_ARCH -eq $TargetArch -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        return
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe was not found. Install Visual Studio 2022 or later (Build Tools are enough) with the "Desktop development with C++" workload.'
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'No Visual Studio installation with the MSVC x64 toolset was found.' }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
    $hostArch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
    $vcArg = if ($hostArch -eq $TargetArch) { $TargetArch } else { "${hostArch}_${TargetArch}" }
    # vcvarsall.bat looks for vswhere.exe on PATH; make it available and silence its diagnostics.
    $env:PATH = (Split-Path -Parent $vswhere) + ';' + $env:PATH
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = & cmd.exe /s /c "`"$vcvars`" $vcArg >nul 2>nul && set"
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) { throw "vcvarsall.bat $vcArg failed with exit code $code" }
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

Import-VsDevEnvironment -TargetArch $Arch

if (-not $BuildDir) {
    $BuildDir = Join-Path $root "build/$Config"
} elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}
$buildType = @{ debug = 'Debug'; release = 'Release'; relwithdebinfo = 'RelWithDebInfo' }[$Config]

if ($Fresh -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

$cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
$needsConfigure = $true
if ((Test-Path $cacheFile) -and $CMakeArgs.Count -eq 0) {
    $cache = Get-Content $cacheFile
    $cachedModules = ($cache | Where-Object { $_ -like 'AETHER_MODULES:*' }) -replace '^AETHER_MODULES:[A-Z]+=', ''
    $cachedType = ($cache | Where-Object { $_ -like 'CMAKE_BUILD_TYPE:*' }) -replace '^CMAKE_BUILD_TYPE:[A-Z]+=', ''
    if ($cachedModules -eq $Modules -and $cachedType -eq $buildType) { $needsConfigure = $false }
}

if ($needsConfigure) {
    $configureArgs = @('-S', $root, '-B', $BuildDir, '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$buildType", "-DAETHER_MODULES=$Modules",
        '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl') + $CMakeArgs
    Invoke-Native -Exe 'cmake' -Arguments $configureArgs -What 'CMake configure'
}

$buildArgs = @('--build', $BuildDir)
if ($Target.Count -gt 0) { $buildArgs += '--target'; $buildArgs += $Target }
if ($Jobs -gt 0) { $buildArgs += @('--parallel', "$Jobs") }
Invoke-Native -Exe 'cmake' -Arguments $buildArgs -What 'Build'

if ($Test) {
    $ctestArgs = @('--test-dir', $BuildDir, '--output-on-failure', '--no-tests=error')
    if ($TestLabel) { $ctestArgs += @('-L', $TestLabel) }
    if ($TestExclude) { $ctestArgs += @('-LE', $TestExclude) }
    if ($TestRegex) { $ctestArgs += @('-R', $TestRegex) }
    if ($JUnitFile) {
        if (-not [System.IO.Path]::IsPathRooted($JUnitFile)) { $JUnitFile = Join-Path $root $JUnitFile }
        $ctestArgs += @('--output-junit', $JUnitFile)
    }
    Invoke-Native -Exe 'ctest' -Arguments $ctestArgs -What 'Tests'
}
