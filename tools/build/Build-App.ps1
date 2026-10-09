[CmdletBinding()]
param([string]$RepositoryRoot,[string]$PlatformToolset='v145',[string]$VCToolsVersion='14.44.35207',[switch]$NativeOnly,[switch]$PlanOnly)
$ErrorActionPreference='Stop'
if(-not $RepositoryRoot){$RepositoryRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)}
$repo=[IO.Path]::GetFullPath($RepositoryRoot)
$build=Join-Path $repo 'builds/source/app'
$native=@((Join-Path $repo 'apps/NeuRoticHub/NeuRoticHub.vcxproj'),'/t:Build','/nologo','/m:2','/v:minimal',
 '/p:Configuration=Release','/p:Platform=x64',('/p:PlatformToolset='+$PlatformToolset),
 ('/p:VCToolsVersion='+$VCToolsVersion),'/p:TrackFileAccess=false','/p:PreferredToolArchitecture=x64',
 ('/p:IntDir='+ (Join-Path $build 'obj/').Replace('\','/') ),('/p:OutDir='+ (Join-Path $build 'bin/').Replace('\','/') ))
$discovery=@('publish',(Join-Path $repo 'apps/NeuRoticHub/discovery/NeuRotic.Discovery.csproj'),'-c','Release','-r','win-x64',
 '--self-contained','true','--configfile',(Join-Path $repo 'apps/NeuRoticHub/discovery/NuGet.Config'),
 '-p:RestoreLockedMode=true',('-p:BaseIntermediateOutputPath='+ (Join-Path $build 'discovery-obj/').Replace('\','/') ),
 '-o',(Join-Path $build 'bin/discovery'))
$plan=[ordered]@{configuration='Release';platform='x64';repository=$repo;nativeOnly=[bool]$NativeOnly;
 commands=@(@{tool='MSBuild';arguments=$native})}
if(-not $NativeOnly){$plan.commands+=@{tool='dotnet';arguments=$discovery}}
if($PlanOnly){$plan|ConvertTo-Json -Depth 8;return}
foreach($name in @('MSBuild','cl')){if(-not(Get-Command $name -ErrorAction SilentlyContinue)){throw "Missing $name. Use an x64 Visual Studio developer shell."}}
if(-not $NativeOnly -and -not(Get-Command dotnet -ErrorAction SilentlyContinue)){throw '.NET 10 SDK must be on PATH'}
New-Item -ItemType Directory -Force -Path $build|Out-Null
$env:CL=$null;$env:_CL_=$null
& MSBuild @native;if($LASTEXITCODE -ne 0){throw 'App build failed'}
if(-not $NativeOnly){
 $env:DOTNET_CLI_HOME=Join-Path $build 'dotnet-home';$env:DOTNET_CLI_TELEMETRY_OPTOUT='1';$env:NUGET_PACKAGES=Join-Path $build 'nuget'
 & dotnet @discovery;if($LASTEXITCODE -ne 0){throw 'Discovery publish failed'}
}
