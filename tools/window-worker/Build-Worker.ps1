[CmdletBinding()]
param([string]$RepositoryRoot,[string]$OutputRoot,[string]$SharedExternalRoot,[string]$SourceManifest,
 [string]$ProducerSession,[ValidatePattern('^[0-9a-f]{64}$')][string]$ProducerSessionSha256,
 [ValidateSet('Shipping','Live')][string]$SourceGraph='Shipping',
 [string]$PlatformToolset='v145',[string]$VCToolsVersion='14.44.35207',
 [ValidatePattern('^[0-9.]+$')][string]$WindowsSdkVersion='10.0.28000.0',[switch]$PlanOnly)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../build/Build.Context.ps1')
if(-not $RepositoryRoot){$RepositoryRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)}
$context=Get-NeuRoticBuildContext $RepositoryRoot $OutputRoot 'worker' $SharedExternalRoot $SourceManifest ''
$repo=$context.repository;$build=$context.output;$obj=Join-Path $build 'obj/';$forwarderObj=Join-Path $build 'forwarder-obj/'
$msbuild=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe'
$context.python=@($context.inputs|Where-Object{[IO.Path]::GetFileName($_.path) -ieq 'python.exe'}|Select-Object -ExpandProperty path) | Select-Object -First 1
$capturePowerShell=@($context.inputs|Where-Object{[IO.Path]::GetFileName($_.path) -in @('powershell.exe','pwsh.exe')}|Select-Object -ExpandProperty path) | Select-Object -First 1
$project=Join-Path $repo 'apps/NeuRoticWindowWorker/shipping/NeuRoticWindowWorker.vcxproj'
if($SourceGraph -eq 'Live'){
 $sourceRoot=$repo.Replace('\','/').TrimEnd('/')+'/'
 $forwarderProject=Join-Path $repo 'tools/window-worker/nr/Forwarder.vcxproj'
 $sourceGraphName='live/source'
}else{
 $sourceRoot=(Join-Path $repo 'apps/NeuRoticWindowWorker/shipping/source/').Replace('\','/')
 $forwarderProject=Join-Path $repo 'apps/NeuRoticWindowWorker/shipping/Forwarder.vcxproj'
 $sourceGraphName='shipping/source'
}
$argsBuild=@($project,('/p:NeuRoticWorkerSourceGraph='+$SourceGraph),('/p:RetainedSourceRoot='+$sourceRoot),('/p:RepoRoot='+$sourceRoot),('/p:NeuRoticForwarderProject='+$forwarderProject),'/t:Build','/nologo','/nr:false','/m:1','/v:minimal','/p:Configuration=Release','/p:Platform=x64',
 ('/p:PlatformToolset='+$PlatformToolset),('/p:VCToolsVersion='+$VCToolsVersion),('/p:WindowsTargetPlatformVersion='+$WindowsSdkVersion),'/p:TrackFileAccess=true','/p:PreferredToolArchitecture=x64',
 ('/p:NeuRoticCapturePython='+$context.python),('/p:NeuRoticCapturePowerShell='+$capturePowerShell),('/p:NeuRoticCaptureScript='+(Join-Path $repo 'tools/build/ProducerCapture.py')),('/p:NeuRoticCaptureSession='+$ProducerSession),
 ('/p:IntDir='+$obj.Replace('\','/')),('/p:OutDir='+(Join-Path $build 'bin/').Replace('\','/')),('/p:NeuRoticForwarderIntDir='+$forwarderObj.Replace('\','/')))
$plan=[ordered]@{qualification=$context.qualification;repository=$repo;output=$build;sourceGraph=$sourceGraphName;workerSourceRoot=$sourceRoot;forwarderProject=$forwarderProject;intermediate=$obj;forwarderIntermediate=$forwarderObj;sourceManifestSha256=$context.sourceManifestSha256;commands=@(@{tool=$msbuild;arguments=$argsBuild})}
if($PlanOnly){$plan|ConvertTo-Json -Depth 8;return}
Assert-NeuRoticBuildInputs $context @($PSCommandPath,(Join-Path $PSScriptRoot '../build/Build.Context.ps1'),$project,$msbuild,$forwarderProject,(Join-Path $repo 'apps/NeuRoticWindowWorker/shipping/Pin-Forwarder.ps1'),(Join-Path $repo 'OptiScaler/shaders/shader_tools/dxc.exe')) $VCToolsVersion $WindowsSdkVersion
if(-not $ProducerSession -or -not $ProducerSessionSha256 -or (Get-FileHash -LiteralPath $ProducerSession).Hash -ine $ProducerSessionSha256){throw 'Owner-frozen producer session identity required'}
$context.producerSession=$ProducerSession;$context.producerSessionSha256=$ProducerSessionSha256
Initialize-NeuRoticObservedBuild $context $msbuild $argsBuild
New-Item -ItemType Directory -Force -Path $build|Out-Null
$log=Join-Path $build 'build.log';Assert-NeuRoticOwnedOutput $repo $build -Tree;Assert-NeuRoticOwnedOutput $repo $log;[IO.File]::WriteAllText($log,'')
Invoke-NeuRoticBuildCommand $msbuild $argsBuild $log
Write-NeuRoticBuildReceipt $context $log (Join-Path $obj 'generated')
