param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
if (-not $output.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Fixture output must stay inside this checkout.'
}
[IO.Directory]::CreateDirectory($output) | Out-Null
function Export-Definition([string]$Source, [string]$Signature, [string]$Name) {
    $text = [IO.File]::ReadAllText((Join-Path $root $Source)).Replace("`r`n", "`n")
    $start = $text.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing production definition: $Signature" }
    $end = $text.IndexOf("`n}`n", $start, [StringComparison]::Ordinal)
    if ($end -lt 0) { throw "Missing definition boundary: $Signature" }
    $definition = $text.Substring($start, $end + 3 - $start)
    [IO.File]::WriteAllText((Join-Path $output $Name), $definition, (New-Object Text.UTF8Encoding($false)))
}
Export-Definition 'OptiScaler/Config.cpp' 'bool Config::SaveExperimentalSettings(' 'NrExperimentalWriterFixture.inc'
Export-Definition 'OptiScaler/dlssnr/NrExperimentalSession.h' 'inline void DisableInMemory(' 'NrExperimentalDisableFixture.inc'
Export-Definition 'OptiScaler/dlssnr/NrExperimentalSession.h' 'inline bool Applied(' 'NrExperimentalAppliedFixture.inc'
