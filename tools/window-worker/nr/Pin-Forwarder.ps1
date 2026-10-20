param([string]$BuildRoot = (Join-Path $PSScriptRoot '../../../builds/window-worker/nr'))
$ErrorActionPreference='Stop'
$binary = Join-Path $BuildRoot 'nvngx.dll_dlssnr.dll'
$stream = [System.IO.File]::OpenRead($binary)
try {
 $digest = [System.Security.Cryptography.SHA256]::Create()
 try { $hash = [BitConverter]::ToString($digest.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
 finally { $digest.Dispose() }
} finally { $stream.Dispose() }
$generated = Join-Path $BuildRoot 'generated'
New-Item -ItemType Directory -Force -Path $generated | Out-Null
Set-Content -LiteralPath (Join-Path $generated 'WindowNrForwarderIdentity.h') -Encoding ascii -Value "#pragma once`ninline constexpr char WindowNrForwarderSha256[] = `"$hash`";"
Write-Output "Standalone forwarder pinned: $hash"
