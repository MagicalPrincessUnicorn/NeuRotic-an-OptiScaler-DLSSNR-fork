param([string]$Worker,[string]$Canonical)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-RuntimeFiles.ps1')
if(-not $Worker){$Worker=Join-Path $repo 'builds/window-worker/bin/NeuRotic.WindowWorker.exe'}
if(-not $Canonical){$Canonical=Join-Path (Split-Path -Parent $repo) 'provider-baselines/20260926-canonical/nvngx_dlssnr.dll'}
$work=[IO.Path]::GetFullPath((Join-Path $repo 'builds/hub-model-folder/fixtures'))
if(Test-Path -LiteralPath $work){throw 'Previous model-folder fixtures remain; inspect before reuse'}
$previousFixture=$env:NEUROTIC_HUB_FIXTURE_ROOT
function Check($value,$name){if(-not $value){throw $name};Write-Output ('PASS '+$name)}
try {
 $env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'Hub'
 $app=Join-Path $work 'App';$workerFolder=Join-Path $app 'Tools/WindowWorker'
 [void](New-Item -ItemType Directory -Path $workerFolder -Force)
 Copy-Item -LiteralPath $Worker -Destination (Join-Path $workerFolder 'NeuRotic.WindowWorker.exe')
 $folder=Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT 'Runtime/Place NVNGX DLSS NR File Here';$target=Join-Path $folder 'nvngx_dlssnr.dll'
 $state=Get-NeuroticRuntimeComponents $app
 Check ([IO.File]::Exists((Join-Path $folder 'README.txt'))) 'Preflight prepares the named deposit and retains instructions'
 Check ($state.neuralModel.path -eq $target -and $state.neuralModel.status -eq 'Missing') 'Empty deposit reports the canonical missing path'
 [IO.File]::WriteAllText((Join-Path $folder 'README.txt'),'user instructions')
 [void](Get-NeuroticRuntimeComponents $app)
 Check ([IO.File]::ReadAllText((Join-Path $folder 'README.txt')) -eq 'user instructions') 'Preflight retains existing instructions'
 [IO.File]::WriteAllText($target,'rejected bytes')
 $preference=Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT 'Runtime/anything.json'
 [IO.File]::WriteAllText($preference,(@{modelPath=$Canonical}|ConvertTo-Json))
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.neuralModel.path -eq $target -and $state.neuralModel.status -eq 'Present') 'Matching deposited filename is recognized without using a remembered model'
 Check ([IO.File]::ReadAllText($target) -eq 'rejected bytes') 'Destination conflict preserves user bytes without overwrite'
 [IO.File]::Delete($target)
 [IO.File]::WriteAllText($preference,(@{modelPath=$Canonical}|ConvertTo-Json))
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.neuralModel.path -eq $target -and $state.neuralModel.status -eq 'Present') ('Remembered model migrates into the single flat deposit: '+$state.neuralModel.reason)
 Check ((Get-FileHash -LiteralPath $target).Hash -eq (Get-FileHash -LiteralPath $Canonical).Hash) 'Migration preserves exact private bytes'
 $first=[IO.File]::GetLastWriteTimeUtc($target)
 [void](Get-NeuroticRuntimeComponents $app)
 Check ([IO.File]::GetLastWriteTimeUtc($target) -eq $first) 'Repeated preflight reuses the existing model'
 [IO.File]::Delete($target)
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.neuralModel.status -eq 'Missing' -and -not [IO.File]::Exists($target)) 'Removing a migrated model does not redeposit old bytes'
 [IO.File]::Copy($Canonical,$target,$false)
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.neuralModel.status -eq 'Present' -and $state.neuralModel.path -eq $target) 'Direct deposit is verified without a chooser'
 $stream=[IO.File]::Open($target,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$stream.WriteByte(88)}finally{$stream.Dispose()}
 $state=Get-NeuroticRuntimeComponents $app
 Check ($state.neuralModel.status -eq 'Present') 'Same-size byte replacement remains recognized by filename'
 [IO.File]::Delete($target);[IO.File]::Delete((Join-Path $folder 'README.txt'));[IO.Directory]::Delete($folder)
 $outside=Join-Path $work 'outside';[void](New-Item -ItemType Directory -Path $outside)
 [IO.File]::WriteAllText((Join-Path $outside 'keep.txt'),'preserved')
 [void](New-Item -ItemType Junction -Path $folder -Target $outside)
 try{
  $state=Get-NeuroticRuntimeComponents $app
  Check ($state.neuralModel.status -eq 'Unavailable') 'Linked deposit folder refuses preflight readiness'
  Check (-not [IO.File]::Exists((Join-Path $outside 'README.txt')) -and [IO.File]::ReadAllText((Join-Path $outside 'keep.txt')) -eq 'preserved') 'Linked deposit cannot write instructions outside the runtime'
 }finally{[IO.Directory]::Delete($folder)}
} finally {
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$previousFixture
 if([IO.Path]::GetFullPath($work) -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub-model-folder/fixtures'))){throw 'Unexpected model-folder cleanup root'}
 if(Test-Path -LiteralPath $work){
  foreach($entry in Get-ChildItem -LiteralPath $work -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked model-folder fixture cleanup refused'}}
  Remove-Item -LiteralPath $work -Recurse -Force
 }
}
