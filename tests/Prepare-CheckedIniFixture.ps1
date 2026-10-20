param([string]$OutputDirectory = 'builds/ui-release/ini-tests')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
if (-not $output.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Output must stay in checkout' }
[IO.Directory]::CreateDirectory($output) | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $root 'OptiScaler/Config.cpp')).Replace("`r`n", "`n")
$definitions = foreach ($signature in @('template<class Edit> bool SaveIniSubset(', 'bool Config::SaveMenuInputSettings(', 'bool Config::SaveExperimentalSettings(', 'bool Config::SaveXeFG(')) {
    $start = $source.IndexOf($signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing definition $signature" }
    $end = $source.IndexOf("`n}`n", $start, [StringComparison]::Ordinal)
    if ($end -lt 0) { throw "Missing boundary $signature" }
    $source.Substring($start, $end + 3 - $start)
}
[IO.File]::WriteAllText((Join-Path $output 'CheckedIniFixture.inc'), ($definitions -join "`n"), (New-Object Text.UTF8Encoding($false)))
$saveStart = $source.IndexOf('bool Config::SaveIni(', [StringComparison]::Ordinal)
$tailStart = $source.IndexOf('    const bool saved =', $saveStart, [StringComparison]::Ordinal)
$tailEnd = $source.IndexOf("`n}`n", $tailStart, [StringComparison]::Ordinal)
if ($saveStart -lt 0 -or $tailStart -lt 0 -or $tailEnd -lt 0) { throw 'Missing global save publication boundary' }
$tail = "bool Config::SaveGlobalTail() {`n" + $source.Substring($tailStart, $tailEnd + 3 - $tailStart)
[IO.File]::AppendAllText((Join-Path $output 'CheckedIniFixture.inc'), "`n" + $tail, (New-Object Text.UTF8Encoding($false)))
