param([string]$Label = 'verification')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'builds/vulkan-experiment/tests/input-health'
$evidence = Join-Path $root 'docs/implementation-evidence/vulkan-return-065329-20261002/input'
New-Item -ItemType Directory -Force -Path $build,$evidence | Out-Null
$before = (Get-ChildItem -LiteralPath $build -File | Measure-Object Length -Sum).Sum
$sourcePath = Join-Path $root 'OptiScaler/menu/input/input_system.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw
$start = $source.IndexOf('InputAcquisitionMode ResolveInputAcquisitionModeLocked()')
$end = $source.IndexOf('void LogHwndIdentityLocked(', $start)
$resolver = $source.Substring($start, $end-$start)
$start = $source.IndexOf('void LogInputHealthSnapshotLocked(')
$end = $source.IndexOf("`nnamespace", $start)
$health = $source.Substring($start, $end-$start)
$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'input_health_logging.cpp.in') -Raw
[IO.File]::WriteAllText((Join-Path $build 'input-health.cpp'), $template.Replace('// PRODUCTION_RESOLVER', $resolver).Replace('// PRODUCTION_HEALTH', $health))
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$compile = 'cl /nologo /std:c++20 /EHsc /MT /O2 /I"' + (Join-Path $root 'OptiScaler') + '" input-health.cpp /Fe:input-health.exe /Fo:input-health.obj /link /INCREMENTAL:NO user32.lib'
[IO.File]::WriteAllLines((Join-Path $build 'build.cmd'), @('@echo off', ('call "'+$vcvars+'" -vcvars_ver=14.44 >nul'), 'if errorlevel 1 exit /b 1', 'set CL=', 'set _CL_=', $compile, 'exit /b %errorlevel%'))
Push-Location $build
try {
    $log = (& cmd /c build.cmd 2>&1 | Out-String)
    $exit = $LASTEXITCODE
    if ($exit -eq 0) {
        foreach ($mode in @('steady','transitions')) {
            $log += (& ./input-health.exe $mode 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0) { $exit = 1 }
        }
    }
    Remove-Item -LiteralPath (Join-Path $build 'input-health.obj'),(Join-Path $build 'input-health.cpp'),(Join-Path $build 'build.cmd') -ErrorAction SilentlyContinue
    $after = (Get-ChildItem -LiteralPath $build -File | Measure-Object Length -Sum).Sum
    $log += "storage before=$before after=$after bytes; fixture object/source removed`n"
    [IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')), $log)
    [ordered]@{
        exit_code = $exit
        source_sha256 = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
        fixture_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'input_health_logging.cpp.in') -Algorithm SHA256).Hash
        toolchain = 'MSVC x64 14.44 C++20; actual production function, resolver and input state; fixed foreground-window boundary; counted logging calls'
        storage_before_bytes = $before
        storage_after_bytes = $after
        game_execution = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence ($Label+'.json')) -Encoding utf8
    Write-Output $log
} finally { Pop-Location }
exit $exit
