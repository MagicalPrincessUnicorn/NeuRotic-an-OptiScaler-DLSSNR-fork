[CmdletBinding()]
param([switch]$NativeOnly,[switch]$SupportOnly,[string]$PackageRoot)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -property installationPath
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
& cmd.exe /d /c (Join-Path $PSScriptRoot 'Build-Native.cmd')
if($LASTEXITCODE -ne 0){throw 'Native build failed'}
if(-not $NativeOnly){
 $sdk='C:/NeuRotic/_deps/hub/dotnet/dotnet.exe'
 if(-not (Test-Path $sdk)){throw 'Blocked: .NET 10 SDK unavailable'}
 $env:DOTNET_CLI_HOME=Join-Path $root 'builds/hub/deps/dotnet-home';$env:DOTNET_CLI_TELEMETRY_OPTOUT='1';$env:NUGET_PACKAGES=Join-Path $root 'builds/hub/deps/nuget';$env:APPDATA=Join-Path $root 'builds/hub/deps/appdata'
 & $sdk publish (Join-Path $root 'apps/NeuRoticHub/discovery/NeuRotic.Discovery.csproj') -c Release -r win-x64 --self-contained true --configfile (Join-Path $root 'apps/NeuRoticHub/discovery/NuGet.Config') -o (Join-Path $root 'builds/hub/bin/discovery')
 if($LASTEXITCODE -ne 0){throw 'Discovery build failed'}
}
& (Join-Path $PSScriptRoot 'Stage-Hub.ps1') -SupportOnly:$SupportOnly -PackageRoot $PackageRoot
