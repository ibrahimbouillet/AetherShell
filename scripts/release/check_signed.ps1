# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
#
# Checks a program that was Authenticode-signed elsewhere (SignPath, D88) against the program this build made: the
# signed file must be byte for byte the built one plus a signature, and Windows must trust that signature. Used before
# setup embeds programs signed elsewhere (installer/GeneratePayload.cmake, AETHER_SIGNED_PROGRAMS_DIR) and before a
# setup signed elsewhere replaces the audited one (scripts/package.ps1 -SignedSetup).
#
# A signature changes three places of a Windows program: the optional header's CheckSum, the Certificate Table entry
# of its data directories, and the certificate table itself, appended at the end after padding to eight bytes. All the
# rest must be identical.
#
# -AllowUntrusted accepts a certificate Windows does not trust (a local test certificate); never for a release.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Unsigned,
    [Parameter(Mandatory = $true)][string]$Signed,
    [switch]$AllowUntrusted
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-Program([string]$Path) {
    $name = Split-Path -Leaf $Path
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$name is missing." }
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path).Path)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "$name is not a Windows program." }
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($pe -lt 0x40 -or $pe + 24 + 2 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $pe) -ne 0x00004550) {
        throw "$name is not a Windows program."
    }
    $optional = $pe + 24
    $magic = [BitConverter]::ToUInt16($bytes, $optional)
    if ($magic -eq 0x10B) { $directories = $optional + 96 } elseif ($magic -eq 0x20B) { $directories = $optional + 112 } else {
        throw "$name has an unknown optional header."
    }
    $certificateEntry = $directories + 4 * 8   # IMAGE_DIRECTORY_ENTRY_SECURITY
    if ($certificateEntry + 8 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $directories - 4) -lt 5) {
        throw "$name has no certificate table entry."
    }
    [pscustomobject]@{
        Name = $name
        Bytes = $bytes
        CheckSum = $optional + 64
        CertificateEntry = $certificateEntry
        CertificateOffset = [int64][BitConverter]::ToUInt32($bytes, $certificateEntry)
        CertificateSize = [int64][BitConverter]::ToUInt32($bytes, $certificateEntry + 4)
    }
}

# The image as hashed for comparison: the first $Length bytes with CheckSum and the certificate entry cleared.
function Get-ImageDigest($Program, [int64]$Length) {
    $bytes = $Program.Bytes
    [Array]::Clear($bytes, $Program.CheckSum, 4)
    [Array]::Clear($bytes, $Program.CertificateEntry, 8)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($bytes, 0, [int]$Length)) } finally { $sha.Dispose() }
}

$built = Read-Program $Unsigned
$given = Read-Program $Signed
$name = $given.Name
if ($built.CertificateSize -ne 0) { throw "The built $($built.Name) already carries a signature." }
if ($given.CertificateSize -eq 0) { throw "$name carries no signature." }
$length = [int64]$built.Bytes.Length
$padded = [int64]([math]::Ceiling($length / 8.0) * 8)
if (($given.CertificateOffset -ne $length -and $given.CertificateOffset -ne $padded) -or
    $given.CertificateOffset + $given.CertificateSize -ne $given.Bytes.Length) {
    throw "$name is not the program this build made, signed."
}
for ($i = $length; $i -lt $given.CertificateOffset; ++$i) {
    if ($given.Bytes[$i] -ne 0) { throw "$name is not the program this build made, signed." }
}
if ((Get-ImageDigest $built $length) -ne (Get-ImageDigest $given $length)) {
    throw "$name is not the program this build made, signed."
}

$signature = Get-AuthenticodeSignature -LiteralPath $Signed
if (-not $signature.SignerCertificate -or $signature.Status -eq 'HashMismatch' -or $signature.Status -eq 'NotSigned') {
    throw "$name has no intact signature."
}
$signer = $signature.SignerCertificate.GetNameInfo([Security.Cryptography.X509Certificates.X509NameType]::SimpleName, $false)
if ($AllowUntrusted) {
    Write-Host "same program, signed by $signer (test certificate, not trusted by Windows): $name"
    exit 0
}
if ($signature.Status -ne 'Valid') { throw "$name is signed by $signer but Windows does not trust its signature." }
Write-Host "same program, signed by $signer and trusted by Windows: $name"
