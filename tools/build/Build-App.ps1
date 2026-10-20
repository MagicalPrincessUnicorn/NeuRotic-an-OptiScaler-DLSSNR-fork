[CmdletBinding()]
param([string]$RepositoryRoot,[string]$OutputRoot,[string]$SharedExternalRoot,[string]$SourceManifest,[string]$DotNetRoot,[string]$NuGetPackages,[ValidatePattern('^[0-9.]+$')][string]$WindowsSdkVersion='10.0.28000.0',[string]$PlatformToolset='v145',[string]$VCToolsVersion='14.44.35207',[switch]$NativeOnly,[switch]$PlanOnly)
$ErrorActionPreference='Stop'
if(-not $RepositoryRoot){$RepositoryRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)}
. (Join-Path $PSScriptRoot 'Build.Context.ps1')
$context=Get-NeuRoticBuildContext $RepositoryRoot $OutputRoot 'app' $SharedExternalRoot $SourceManifest ''
$repo=$context.repository;$build=$context.output;$deps=Join-Path (Split-Path -Parent $repo) '_deps/hub'
if(-not $DotNetRoot){$DotNetRoot=Join-Path $deps 'dotnet'}
if(-not $NuGetPackages){$NuGetPackages=Join-Path $deps 'nuget'}
$dotnet=Join-Path $DotNetRoot 'dotnet.exe'
$msbuild=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
$native=@((Join-Path $repo 'apps/NeuRoticHub/NeuRoticHub.vcxproj'),'/t:Build','/nologo','/nr:false','/m:2','/v:minimal',
 '/p:Configuration=Release','/p:Platform=x64',('/p:PlatformToolset='+$PlatformToolset),
 ('/p:VCToolsVersion='+$VCToolsVersion),('/p:WindowsTargetPlatformVersion='+$WindowsSdkVersion),'/p:TrackFileAccess=true','/p:PreferredToolArchitecture=x64',
 ('/p:IntDir='+ (Join-Path $build 'obj/').Replace('\','/') ),('/p:OutDir='+ (Join-Path $build 'bin/').Replace('\','/') ))
$discovery=@('publish',(Join-Path $repo 'apps/NeuRoticHub/discovery/NeuRotic.Discovery.csproj'),'-c','Release','-r','win-x64',
 '--self-contained','true','--no-restore',
 '-p:RestoreLockedMode=true',('-p:BaseIntermediateOutputPath='+ (Join-Path $build 'discovery-obj/').Replace('\','/') ),
 '-o',(Join-Path $build 'bin/discovery'))
$plan=[ordered]@{qualification=$context.qualification;output=$build;nugetPackages=$NuGetPackages;dotnetRoot=$DotNetRoot;sourceManifestSha256=$context.sourceManifestSha256;configuration='Release';platform='x64';repository=$repo;nativeOnly=[bool]$NativeOnly;
 commands=@(@{tool=$msbuild;arguments=$native})}
if(-not $NativeOnly){$plan.commands+=@{tool=$dotnet;arguments=$discovery}}
if($PlanOnly){$plan|ConvertTo-Json -Depth 8;return}
$required=@($PSCommandPath,(Join-Path $PSScriptRoot 'Build.Context.ps1'),$native[0],$msbuild)
if(-not $NativeOnly){$required+=@($dotnet,$discovery[1],(Join-Path $build 'discovery-obj/project.assets.json'))}
Assert-NeuRoticBuildInputs $context $required $VCToolsVersion $WindowsSdkVersion
Initialize-NeuRoticObservedBuild $context $msbuild $native ([bool]$NativeOnly)
New-Item -ItemType Directory -Force -Path $build|Out-Null
$log=Join-Path $build 'build.log';Assert-NeuRoticOwnedOutput $repo $build -Tree;Assert-NeuRoticOwnedOutput $repo $log;[IO.File]::WriteAllText($log,'')
Invoke-NeuRoticBuildCommand $msbuild $native $log
if(-not $NativeOnly){
 $env:DOTNET_ROOT=$DotNetRoot;$env:DOTNET_CLI_HOME=Join-Path $deps 'dotnet-home';$env:DOTNET_CLI_TELEMETRY_OPTOUT='1';$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1';$env:NUGET_PACKAGES=$NuGetPackages
 Invoke-NeuRoticBuildCommand $dotnet $discovery $log
}
Write-NeuRoticBuildReceipt $context $log ''
