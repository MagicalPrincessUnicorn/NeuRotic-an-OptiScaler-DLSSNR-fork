[CmdletBinding()]
param(
 [string]$RepositoryRoot,
 [string]$PlatformToolset='v145',
 [string]$VCToolsVersion='14.44.35207',
 [string]$BuildLabel,
 [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
if(-not $RepositoryRoot){$RepositoryRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)}
$repo=[IO.Path]::GetFullPath($RepositoryRoot)
$build=Join-Path $repo 'builds/source/core'
$flow=Join-Path $build 'flow'
$generated=Join-Path $build 'generated'
$targets=Join-Path $repo 'tools/build/Core.Dependencies.targets'
$configure=@('-S',(Join-Path $repo 'addons/prepared-guides/flow'),'-B',$flow,'-G','Ninja',
 '-DCMAKE_BUILD_TYPE=Release',('-DNR_FLOW_FFX_SDK='+ (Join-Path $repo 'external/FidelityFX-SDK')))
$compileFlow=@('--build',$flow,'--target','neurotic_prepared_flow','--parallel','2')
$core=@((Join-Path $repo 'OptiScaler/OptiScaler.vcxproj'),'/t:Build','/nologo','/m:1','/v:minimal',
 '/p:Configuration=Release','/p:Platform=x64',('/p:PlatformToolset='+$PlatformToolset),
 ('/p:SolutionDir='+$repo.Replace('\','/')+'/'),
 ('/p:VCToolsVersion='+$VCToolsVersion),'/p:TrackFileAccess=false','/p:PreferredToolArchitecture=x64',
 '/p:PreBuildEventUseInBuild=false','/p:PostBuildEventUseInBuild=false','/p:CL_MPCount=2',
 ('/p:IntDir='+ (Join-Path $build 'obj/').Replace('\','/') ),('/p:OutDir='+ (Join-Path $build 'bin/').Replace('\','/') ),
 ('/p:NativeGuideFlowRoot='+$flow),('/p:NeuRoticRepoRoot='+$repo),('/p:NeuRoticGeneratedRoot='+$generated),
 ('/p:ForceImportBeforeCppTargets='+$targets))
$plan=[ordered]@{configuration='Release';platform='x64';repository=$repo;generated=$generated;
 commands=@(@{tool='cmake';arguments=$configure},@{tool='cmake';arguments=$compileFlow},@{tool='MSBuild';arguments=$core})}
if($PlanOnly){$plan|ConvertTo-Json -Depth 8;return}
foreach($name in @('cmake','ninja','MSBuild','cl','dxc')){if(-not(Get-Command $name -ErrorAction SilentlyContinue)){throw "Missing $name. Use an x64 Visual Studio developer shell with CMake/Ninja and Windows SDK tools on PATH."}}
foreach($path in @($core[0],$targets,(Join-Path $repo 'addons/prepared-guides/flow/CMakeLists.txt'),
 (Join-Path $repo 'external/FidelityFX-SDK/sdk/tools/binary_store/FidelityFX_SC.exe'))){if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw "Required source/dependency missing: $path. Initialize the repository submodules."}}
if(-not $BuildLabel){$BuildLabel=(& git -C $repo rev-parse --short=12 HEAD);if($LASTEXITCODE -ne 0){throw 'Unable to identify source commit'}}
if($BuildLabel -notmatch '^[A-Za-z0-9._-]{1,80}$'){throw 'Invalid build label'}
New-Item -ItemType Directory -Force -Path $generated|Out-Null
[IO.File]::WriteAllText((Join-Path $generated 'resource_build_date.h'),('#define VER_BUILD_DATE "'+[DateTime]::UtcNow.ToString('yyyyMMdd')+'_SOURCE_BUILD"'+"`n"))
[IO.File]::WriteAllText((Join-Path $generated 'resource_build_commit.h'),('#define VER_BUILD_COMMIT "'+$BuildLabel+'"'+"`n"))
$env:CL=$null;$env:_CL_=$null
& cmake @configure;if($LASTEXITCODE -ne 0){throw 'Native flow configuration failed'}
& cmake @compileFlow;if($LASTEXITCODE -ne 0){throw 'Native flow build failed'}
& MSBuild @core;if($LASTEXITCODE -ne 0){throw 'Core build failed'}
