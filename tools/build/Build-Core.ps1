[CmdletBinding()]
param(
 [string]$RepositoryRoot,
 [string]$OutputRoot,[string]$SharedExternalRoot,[string]$SourceManifest,
 [ValidatePattern('^[0-9.]+$')][string]$WindowsSdkVersion='10.0.28000.0',
 [string]$PlatformToolset='v145',
 [string]$VCToolsVersion='14.44.35207',
 [string]$BuildLabel,[string]$ProducerSession,[ValidatePattern('^[0-9a-f]{64}$')][string]$ProducerSessionSha256,
 [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
if(-not $RepositoryRoot){$RepositoryRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)}
. (Join-Path $PSScriptRoot 'Build.Context.ps1')
$context=Get-NeuRoticBuildContext $RepositoryRoot $OutputRoot 'core' $SharedExternalRoot $SourceManifest $BuildLabel
$repo=$context.repository;$build=$context.output;$BuildLabel=$context.buildLabel
$vs=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/18/Community'
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$cl=Join-Path $vs ('VC/Tools/MSVC/'+$VCToolsVersion+'/bin/Hostx64/x64/cl.exe')
$flow=Join-Path $build 'flow'
$generated=Join-Path $build 'generated'
$targets=Join-Path $repo 'tools/build/Core.Dependencies.targets'
$context.python=@($context.inputs|Where-Object{[IO.Path]::GetFileName($_.path) -ieq 'python.exe'}|Select-Object -ExpandProperty path) | Select-Object -First 1
$configure=@('-S',(Join-Path $repo 'addons/prepared-guides/flow'),'-B',$flow,'-G','Ninja',
 '--trace-expand','--trace-format=json-v1',('--trace-redirect='+(Join-Path $flow 'configure-trace.jsonl')),
 ('-DNR_CAPTURE_PYTHON='+$context.python),('-DNR_CAPTURE_SCRIPT='+(Join-Path $repo 'tools/build/ProducerCapture.py')),('-DNR_CAPTURE_SESSION='+$ProducerSession),
 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_BUILD_TYPE=Release',('-DCMAKE_MAKE_PROGRAM='+$ninja),('-DCMAKE_CXX_COMPILER='+$cl),('-DNR_FLOW_FFX_SDK='+$context.sharedDependencies['FidelityFX-SDK']))
$compileFlow=@('--build',$flow,'--target','neurotic_prepared_flow','--parallel','2')
$core=@((Join-Path $repo 'OptiScaler/OptiScaler.vcxproj'),'/t:Build','/nologo','/nr:false','/m:1','/v:minimal',
 '/p:Configuration=Release','/p:Platform=x64',('/p:PlatformToolset='+$PlatformToolset),
 ('/p:SolutionDir='+$repo.Replace('\','/')+'/'),
 ('/p:VCToolsVersion='+$VCToolsVersion),('/p:WindowsTargetPlatformVersion='+$WindowsSdkVersion),'/p:TrackFileAccess=true','/p:PreferredToolArchitecture=x64',
 '/p:PreBuildEventUseInBuild=false','/p:PostBuildEventUseInBuild=false','/p:CL_MPCount=2',
 ('/p:IntDir='+ (Join-Path $build 'obj/').Replace('\','/') ),('/p:OutDir='+ (Join-Path $build 'bin/').Replace('\','/') ),
 ('/p:NativeGuideFlowRoot='+$flow),('/p:NeuRoticRepoRoot='+$repo),('/p:NeuRoticGeneratedRoot='+$generated),('/p:NeuRoticSharedExternalRoot='+(Split-Path -Parent $context.sharedDependencies['FidelityFX-SDK'])),
 ('/p:ForceImportBeforeCppTargets='+$targets))
$plan=[ordered]@{qualification=$context.qualification;output=$build;sharedDependencies=$context.sharedDependencies;sourceManifestSha256=$context.sourceManifestSha256;buildLabel=$BuildLabel;configuration='Release';platform='x64';repository=$repo;generated=$generated;
 commands=@(@{tool=$cmake;arguments=$configure},@{tool=$cmake;arguments=$compileFlow},@{tool=$msbuild;arguments=$core})}
if($PlanOnly){$plan|ConvertTo-Json -Depth 8;return}
Assert-NeuRoticBuildInputs $context @($PSCommandPath,(Join-Path $PSScriptRoot 'Build.Context.ps1'),$core[0],$targets,$cmake,$ninja,$msbuild,$cl,(Join-Path $repo 'addons/prepared-guides/flow/CMakeLists.txt'),(Join-Path $context.sharedDependencies['FidelityFX-SDK'] 'sdk/tools/binary_store/FidelityFX_SC.exe')) $VCToolsVersion $WindowsSdkVersion
Initialize-NeuRoticObservedBuild $context $msbuild $core
New-Item -ItemType Directory -Force -Path $generated|Out-Null
[IO.File]::WriteAllText((Join-Path $generated 'resource_build_date.h'),('#define VER_BUILD_DATE "'+[DateTime]::UtcNow.ToString('yyyyMMdd')+'_INTERNAL_DEVELOPMENT"'+"`n"))
[IO.File]::WriteAllText((Join-Path $generated 'resource_build_commit.h'),('#define VER_BUILD_COMMIT "'+$BuildLabel+'"'+"`n"))
$log=Join-Path $build 'build.log';[IO.File]::WriteAllText($log,'')
Invoke-NeuRoticBuildCommand $cmake $configure $log
Invoke-NeuRoticBuildCommand $cmake $compileFlow $log
Invoke-NeuRoticBuildCommand $msbuild $core $log
Write-NeuRoticBuildReceipt $context $log $generated
