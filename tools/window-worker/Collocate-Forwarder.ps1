$ErrorActionPreference = 'Stop'
$build = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../builds/window-worker'))
$source = Join-Path $build 'nr/nvngx.dll_dlssnr.dll'
$destination = Join-Path $build 'bin/nvngx.dll_dlssnr.dll'
Copy-Item -LiteralPath $source -Destination $destination -Force
$stream = [IO.File]::OpenRead($destination)
try {
    $digest = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($digest.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $digest.Dispose() }
} finally { $stream.Dispose() }
$pin = Get-Content -LiteralPath (Join-Path $build 'nr/generated/WindowNrForwarderIdentity.h') -Raw
if ($pin -notmatch [regex]::Escape($hash)) { throw 'Delivered forwarder does not match compiled identity' }
$worker = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $build 'bin/NeuRotic.WindowWorker.exe')))
if (-not $worker.Contains(([string][char]0) + $hash + ([string][char]0))) {
    throw 'Window Worker executable has a stale forwarder pin; rebuild the worker and package both files together'
}
Write-Output "Worker default forwarder present and pinned: $hash"
