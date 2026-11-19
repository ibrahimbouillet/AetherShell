# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# Authenticode-signs Aether Shell's programs with the publisher's code-signing identity, so Windows shows the
# publisher's name instead of "Unknown publisher" and SmartScreen can build a reputation for it (A04,
# docs/release/CODE_SIGNING.md). It never creates, imports or trusts a certificate, and never changes the
# machine's certificate stores: the identity is the maintainer's, configured through the environment.
#
#   AETHER_SIGN_MODE          certificate | pfx | artifact-signing
#     certificate             AETHER_SIGN_CERT_THUMBPRINT: a certificate already in the signer's own store
#                             (CurrentUser\My, a hardware token or a cloud HSM's key storage provider).
#     pfx                     AETHER_SIGN_PFX (file) and AETHER_SIGN_PFX_PASSWORD (optional).
#     artifact-signing        Azure Artifact Signing (formerly Trusted Signing): AETHER_SIGN_DLIB (the
#                             Azure.CodeSigning.Dlib.dll of its client tools) and AETHER_SIGN_METADATA (its
#                             metadata.json: endpoint, account, certificate profile). Signs in the cloud.
#   AETHER_SIGN_TIMESTAMP_URL RFC 3161 time stamp server (default: the mode's usual one). "none" only for local
#                             tests: an unstamped signature expires with its certificate.
#   AETHER_SIGNTOOL           signtool.exe (default: the newest one in the Windows 10/11 SDK).
#
# -AllowUntrusted accepts a signature whose certificate Windows does not trust (a local test certificate); a
# release never passes it, so its signatures must verify against Windows' own trust (signtool verify /pa).
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Files,
    [switch]$AllowUntrusted
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Find-SignTool {
    $configured = [Environment]::GetEnvironmentVariable('AETHER_SIGNTOOL')
    if ($configured) {
        if (-not (Test-Path -LiteralPath $configured -PathType Leaf)) { throw 'AETHER_SIGNTOOL does not name a file.' }
        return $configured
    }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $found = @(Get-ChildItem -LiteralPath $kits -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^10\.' } | Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } | Where-Object { Test-Path -LiteralPath $_ })
    if ($found.Count -eq 0) { throw 'signtool.exe was not found: install the Windows SDK or set AETHER_SIGNTOOL.' }
    return $found[0]
}

function Get-Setting([string]$Name, [switch]$Required) {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ($Required -and -not $value) { throw "$Name is not set." }
    return $value
}

$mode = Get-Setting 'AETHER_SIGN_MODE' -Required
$signArgs = @('sign', '/fd', 'SHA256', '/d', 'Aether Shell', '/du', 'https://github.com/ibrahimbouillet/AetherShell')
$defaultTimestamp = 'http://timestamp.digicert.com'
switch ($mode) {
    'certificate' {
        $thumbprint = (Get-Setting 'AETHER_SIGN_CERT_THUMBPRINT' -Required) -replace '\s', ''
        if ($thumbprint -notmatch '^[0-9A-Fa-f]{40}$') { throw 'AETHER_SIGN_CERT_THUMBPRINT must be a SHA-1 thumbprint (40 hexadecimal digits).' }
        $signArgs += @('/sha1', $thumbprint)
    }
    'pfx' {
        $pfx = Get-Setting 'AETHER_SIGN_PFX' -Required
        if (-not (Test-Path -LiteralPath $pfx -PathType Leaf)) { throw 'AETHER_SIGN_PFX does not name a file.' }
        $signArgs += @('/f', $pfx)
        $password = Get-Setting 'AETHER_SIGN_PFX_PASSWORD'
        if ($password) { $signArgs += @('/p', $password) }
    }
    'artifact-signing' {
        $dlib = Get-Setting 'AETHER_SIGN_DLIB' -Required
        $metadata = Get-Setting 'AETHER_SIGN_METADATA' -Required
        foreach ($file in @($dlib, $metadata)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'An Artifact Signing file is missing.' } }
        $signArgs += @('/dlib', $dlib, '/dmdf', $metadata)
        $defaultTimestamp = 'http://timestamp.acs.microsoft.com'
    }
    default { throw "AETHER_SIGN_MODE must be certificate, pfx or artifact-signing." }
}
$timestamp = Get-Setting 'AETHER_SIGN_TIMESTAMP_URL'
if (-not $timestamp) { $timestamp = $defaultTimestamp }
if ($timestamp -ne 'none') { $signArgs += @('/tr', $timestamp, '/td', 'SHA256') }

$signtool = Find-SignTool
foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'A file to sign is missing.' }
    $name = Split-Path -Leaf $file
    # The password, when there is one, is never echoed: signtool's own output names only the files.
    & $signtool @signArgs $file | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "signtool could not sign $name." }
    $signature = Get-AuthenticodeSignature -LiteralPath $file
    if (-not $signature.SignerCertificate) { throw "$name carries no signature after signing." }
    if ($AllowUntrusted) {
        Write-Host "signed (test certificate, not trusted by Windows): $name"
        continue
    }
    & $signtool verify /pa /q $file | Out-Null
    if ($LASTEXITCODE -ne 0 -or $signature.Status -ne 'Valid') { throw "$name is signed but Windows does not trust its signature." }
    Write-Host "signed and verified: $name"
}
