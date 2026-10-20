param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repo 'builds'))+'\'
$output=if([IO.Path]::IsPathRooted($OutputDirectory)){[IO.Path]::GetFullPath($OutputDirectory)}else{[IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))}
if(-not $output.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Test output must be inside this checkout builds directory.'}
for($path=$output;$path -and $path.Length -ge $repo.Length;$path=[IO.Path]::GetDirectoryName($path)){
    if((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Test output must not cross a junction.'}
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source=Get-Content -LiteralPath (Join-Path $repo 'OptiScaler/hooks/Streamline_Hooks.cpp') -Raw
function Get-Section([string]$begin,[string]$end){
    $start=$source.IndexOf($begin,[StringComparison]::Ordinal)
    if($start -lt 0){throw "Missing production section: $begin"}
    $stop=$source.IndexOf($end,$start+$begin.Length,[StringComparison]::Ordinal)
    if($stop -lt 0){throw "Missing production section boundary: $end"}
    return $source.Substring($start,$stop-$start)
}
$support=Get-Section 'static std::optional<bool> NrNativeMfgAdapterSupport(' 'std::optional<bool> StreamlineHooks::nativeMfgAdapter('
$adapter=Get-Section 'std::optional<bool> StreamlineHooks::nativeMfgAdapter(' 'bool StreamlineHooks::prepareNativeMfgCapabilities('
$prepare=Get-Section 'bool StreamlineHooks::prepareNativeMfgCapabilities(' 'void StreamlineHooks::streamlineLogCallback_sl1('
$owned=@($support,$adapter,$prepare) -join "`n"
Set-Content -LiteralPath (Join-Path $output 'nr_mfg_owned_adapter_hooks.inc') -Value $owned -Encoding UTF8
$device=Get-Section 'sl::Result StreamlineHooks::hkslSetD3DDevice(' 'bool StreamlineHooks::experimentalFgLane('
$bind=Get-Section 'void StreamlineHooks::bindNativeMfgWrapper(' 'void StreamlineHooks::wrapNativeDlssgFunction('
$plugin=Get-Section 'bool StreamlineHooks::hkdlssg_slOnPluginLoad(' 'const char* StreamlineHooks::hkdlssg_slGetPluginJSONConfig_sl1('
$nested=@($device,$support,$adapter,$prepare,$bind,$plugin) -join "`n"
Set-Content -LiteralPath (Join-Path $output 'nr_mfg_plugin_startup_hooks.inc') -Value $nested -Encoding UTF8
$headerSource=Get-Content -LiteralPath (Join-Path $repo 'OptiScaler/hooks/Streamline_Hooks.h') -Raw
$adapterDeclaration=[regex]::Match($headerSource,'(?ms)^struct Adapter\s*\{.*?^\};').Value
$capsDeclaration=[regex]::Match($headerSource,'(?ms)^struct SystemCaps\s*\{.*?^\};').Value
$capsCapacity=[regex]::Match($headerSource,'(?m)^constexpr uint32_t kMaxNumSupportedGPUs = \d+;').Value
if(-not $adapterDeclaration -or -not $capsDeclaration -or -not $capsCapacity){throw 'Missing exact production modern caps declarations for startup fixture.'}
Set-Content -LiteralPath (Join-Path $output 'nr_mfg_plugin_startup_caps.inc') -Value (@($adapterDeclaration,$capsCapacity,$capsDeclaration) -join "`n") -Encoding UTF8
