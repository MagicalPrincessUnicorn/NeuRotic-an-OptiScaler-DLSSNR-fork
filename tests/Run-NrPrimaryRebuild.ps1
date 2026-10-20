$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'builds/inspector-motion/primary-rebuild'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$source = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp')
function Write-Slice([string]$start, [string]$end, [string]$name) {
    $begin = $source.IndexOf($start)
    if ($begin -lt 0) { throw "Production slice start missing: $start" }
    $finish = $source.IndexOf($end, $begin)
    if ($finish -lt 0) { throw "Production slice end missing: $end" }
    [IO.File]::WriteAllText((Join-Path $out $name), $source.Substring($begin, $finish - $begin))
}
Write-Slice 'if (g_nr.resumeFeatureAwaitingRelease != nullptr)' '// Placement belongs' 'primary-gate.inc'
Write-Slice 'if (g_nr.feature != nullptr && (resolutionChanged || tuningChanged))' 'if (g_nr.meter == nullptr)' 'primary-rebuild.inc'
Write-Slice 'void ReleaseSurfacesIfFormatChanged(' 'void Barrier(' 'primary-format.inc'
$commands = @"
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /I "$out" "$PSScriptRoot\nr_primary_rebuild.cpp" /Fe:"$out\test.exe" /Fo:"$out\test.obj"
if errorlevel 1 exit /b 1
"$out\test.exe"
exit /b %errorlevel%
"@
$runner = Join-Path $out 'run.cmd'
[IO.File]::WriteAllText($runner, $commands)
try {
    & $env:ComSpec /d /c $runner
    $result = $LASTEXITCODE
} finally {
    $object = Join-Path $out 'test.obj'
    if (Test-Path -LiteralPath $object) { Remove-Item -LiteralPath $object }
}
exit $result
