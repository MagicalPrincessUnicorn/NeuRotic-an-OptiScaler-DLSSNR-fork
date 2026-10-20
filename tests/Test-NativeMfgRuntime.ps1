param([Parameter(Mandatory=$true)][string]$ProviderDirectory,[string]$TestDirectory='')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$root=[IO.Path]::GetFullPath((Join-Path $repo 'builds\native-mfg\runtime-fixture'))
$allowed=[IO.Path]::GetFullPath((Join-Path $repo 'builds\native-mfg'))+'\'
if (-not $root.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture path.' }
if (Test-Path -LiteralPath $root) { throw 'Runtime fixture is already in use.' }
$provider=(Resolve-Path -LiteralPath $ProviderDirectory).Path
$renamed=Join-Path $root 'opaque-provider.bin'
$bin=if([string]::IsNullOrEmpty($TestDirectory)){Join-Path $repo 'builds\native-mfg\tests'}elseif([IO.Path]::IsPathRooted($TestDirectory)){[IO.Path]::GetFullPath($TestDirectory)}else{[IO.Path]::GetFullPath((Join-Path $repo $TestDirectory))}
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repo 'builds'))+'\'
if(-not $bin.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Test binaries must be inside this checkout builds directory.'}
$exe=Join-Path $bin 'nr_mfg_runtime.exe'
New-Item -ItemType Directory -Path $root | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $provider 'nvngx_dlssg.dll') -Destination $renamed
    & $exe $provider
    if ($LASTEXITCODE -ne 0) { throw "Named runtime test failed: $LASTEXITCODE" }
    & $exe $provider $renamed
    if ($LASTEXITCODE -ne 0) { throw "Renamed runtime test failed: $LASTEXITCODE" }
    & $exe $provider $renamed 'duplicate'
    if ($LASTEXITCODE -ne 0) { throw "Ambiguous runtime test failed: $LASTEXITCODE" }
    & $exe $provider $renamed 'conflict'
    if ($LASTEXITCODE -ne 0) { throw "Conflicting provider test failed: $LASTEXITCODE" }
    & $exe $provider $renamed 'other'
    if ($LASTEXITCODE -ne 0) { throw "Reverse provider identity test failed: $LASTEXITCODE" }
    foreach($mode in @('adapter-conflict','adapter-provider-conflict')) {
        & $exe $provider $renamed $mode
        if ($LASTEXITCODE -ne 0) { throw "$mode test failed: $LASTEXITCODE" }
    }
    & (Join-Path $bin 'nr_mfg_generation.exe') $provider
    if($LASTEXITCODE -ne 0){throw "Generation currentness test failed: $LASTEXITCODE"}
    foreach($mode in @('direct','ada','unsupported','unknown','unverified','optout','route')){
        & (Join-Path $bin 'nr_mfg_owned_adapter.exe') $provider $mode
        if($LASTEXITCODE -ne 0){throw "Owned adapter $mode test failed: $LASTEXITCODE"}
    }
    & (Join-Path $bin 'nr_mfg_plugin_startup.exe') (Join-Path $bin 'public-wrapper\sl.dlss_g.dll')
    if($LASTEXITCODE -ne 0){throw "Nested public plugin startup test failed: $LASTEXITCODE"}
    Write-Output 'PASS: inert runtime, owned adapter, generation and public nested-plugin controls. No proprietary entry executed.'
} finally {
    if ((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing junction cleanup.' }
    Remove-Item -LiteralPath $root -Recurse -Force
}
