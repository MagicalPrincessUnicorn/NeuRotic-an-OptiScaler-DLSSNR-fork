param([Parameter(Mandatory=$true)][string]$Binary,[Parameter(Mandatory=$true)][string]$GeneratedDir)
$ErrorActionPreference='Stop'
$stream=[IO.File]::OpenRead($Binary)
$sha=[Security.Cryptography.SHA256]::Create()
try{$hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$stream.Dispose()}
New-Item -ItemType Directory -Force -Path $GeneratedDir|Out-Null
[IO.File]::WriteAllText((Join-Path $GeneratedDir 'WindowNrForwarderIdentity.h'),("#pragma once`ninline constexpr char WindowNrForwarderSha256[] = `"$hash`";`n"),[Text.Encoding]::ASCII)
