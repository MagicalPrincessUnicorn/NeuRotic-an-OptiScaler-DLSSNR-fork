param([string]$Fxc='C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\fxc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dir=Join-Path $root 'OptiScaler/nr/semantic/character'
& $Fxc /nologo /T cs_5_0 /E main /O3 /Fh (Join-Path $dir 'CharacterThumbnailBytecode.h') /Vn g_CharacterThumbnail (Join-Path $dir 'CharacterThumbnail.hlsl')
if($LASTEXITCODE -ne 0){throw 'Thumbnail compilation failed'}
$header=Join-Path $dir 'CharacterThumbnailBytecode.h'
$normalized=(Get-Content -LiteralPath $header | ForEach-Object { $_.TrimEnd() }) -join "`n"
[IO.File]::WriteAllText($header,$normalized.TrimEnd()+"`n",[Text.Encoding]::ASCII)
# Checked-in bytecode keeps shader compilation entirely out of Present.
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $dir 'CharacterThumbnail.hlsl'),(Join-Path $dir 'CharacterThumbnailBytecode.h')
