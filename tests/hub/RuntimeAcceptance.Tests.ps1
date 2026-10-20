param([string]$RepoRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
. (Join-Path $RepoRoot 'tests/hub/SimpleInstallerFixtureCommon.ps1')
Start-SimpleFixtures
try{
 $env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $script:SimpleWork 'user-data'
 . (Join-Path $script:SimpleSupport 'NeuRotic-InstallCore.ps1')
 . (Join-Path $script:SimpleSupport 'NeuRotic-RuntimeFiles.ps1')
 $root=Get-NeuroticRuntimeRoot;New-Item -ItemType Directory -Path $root -Force|Out-Null
 $source=Join-Path $root 'small-model.dll';[IO.File]::WriteAllText($source,'fixture acceptance')
 $components=[ordered]@{streamline=[ordered]@{status='Missing';files=@()};neuralModel=[ordered]@{status='Present';path=$source;sha256=('a'*64)}}
 Save-NeuroticRuntimeAcceptance $components
 $read=Get-NeuroticAcceptedRuntimeComponents
 Assert-Simple ($read.neuralModel.status -eq 'Present' -and $read.neuralModel.fileIdentity -and $read.neuralModel.lastWriteTicks -and $read.neuralModel.bytes) 'accepted readiness carries path/file identity/size/write time'
 $ticks=(Get-Item -LiteralPath $source).LastWriteTimeUtc
 Remove-Item -LiteralPath $source;[IO.File]::WriteAllText($source,'fixture acceptance');[IO.File]::SetLastWriteTimeUtc($source,$ticks)
 $read=Get-NeuroticAcceptedRuntimeComponents
 Assert-Simple ($read.neuralModel.status -eq 'Unavailable' -and $read.neuralModel.reason -match 'Refresh runtime files') 'replaced deposit with same size/time invalidates acceptance by file identity'
 function Invoke-NeuroticModelCheck {throw 'installer readiness must never execute model validation'}
 $read=Get-NeuroticAcceptedRuntimeComponents
 Assert-Simple ($read.neuralModel.status -eq 'Unavailable') 'cached readiness does not launch a verifier'
 Remove-Item -LiteralPath (Join-Path $root 'accepted-runtime.json')
 $read=Get-NeuroticAcceptedRuntimeComponents
 Assert-Simple ($read.neuralModel.status -eq 'Missing') 'absent cache remains optional and requires explicit refresh'

 # All files below are inert synthetic bytes; model-check double observes the read path only.
 $unicode='Jos'+[char]0x00e9+'-'+[char]0x6f22+[char]0x5b57
 $env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $script:SimpleWork $unicode
 $root=Get-NeuroticRuntimeRoot;New-Item -ItemType Directory -Path $root -Force|Out-Null
 $source=Join-Path $root 'small-model.dll';[IO.File]::WriteAllText($source,'unicode fixture acceptance')
 $components=[ordered]@{streamline=[ordered]@{status='Missing';files=@()};neuralModel=[ordered]@{status='Present';path=$source;sha256=('a'*64)}}
 Save-NeuroticRuntimeAcceptance $components
 $read=Get-NeuroticAcceptedRuntimeComponents
 Assert-Simple ($read.neuralModel.status -eq 'Present' -and $read.neuralModel.path -ceq $source) 'UTF8 Unicode runtime acceptance round-trips exact source under Windows PowerShell'
 $game=New-SimpleGameFixture 'unicode-acceptance-game';$package=New-SimplePackageFixture
 $result=Invoke-NeuroticSimpleInstall (Join-Path $game 'FixtureGame.exe') $package 'dxgi.dll' $false @() '' $read $null
 Assert-Simple ($result.status -eq 'Succeeded' -and $result.copied -contains 'nvngx_dlssnr.dll' -and [IO.File]::ReadAllText((Join-Path $game 'nvngx_dlssnr.dll')) -ceq 'unicode fixture acceptance') 'Unicode accepted runtime is included by actual installer'
 # UTF8 BOM compatibility must retain the same identity and source path.
 $cache=Join-Path $root 'accepted-runtime.json';$text=[IO.File]::ReadAllText($cache,[Text.Encoding]::UTF8)
 [IO.File]::WriteAllText($cache,$text,(New-Object Text.UTF8Encoding($true)))
 Assert-Simple ((Get-NeuroticAcceptedRuntimeComponents).neuralModel.status -eq 'Present') 'UTF8 BOM acceptance remains compatible'
 [IO.File]::WriteAllText($source,'changed Unicode source')
 Assert-Simple ((Get-NeuroticAcceptedRuntimeComponents).neuralModel.status -eq 'Unavailable') 'changed Unicode runtime remains refused'
 $outside=Join-Path $script:SimpleWork 'unicode-outside';New-Item -ItemType Directory -Path $outside|Out-Null
 $link=Join-Path $root 'linked';New-Item -ItemType Junction -Path $link -Target $outside|Out-Null
 try{
  $refused=$false;try{[void](Assert-NeuroticRuntimePath (Join-Path $link 'file.dll'))}catch{$refused=$true}
  Assert-Simple $refused 'linked Unicode runtime ancestry remains refused'
  Assert-Simple (-not @(Get-ChildItem -LiteralPath $outside -Force).Count) 'linked outside directory remains untouched'
 }finally{[IO.Directory]::Delete($link)}
 $remembered=Join-Path $root ('remembered-'+[char]0x00e9+'.dll');[IO.File]::WriteAllText($remembered,'inert preference fixture')
 $script:ObservedModelPath=$null
 function Invoke-NeuroticModelCheck([string]$Worker,[string]$Path,[string]$ImportRoot=$null){$script:ObservedModelPath=$Path;return [pscustomobject]@{ready=$false;path=$Path;reason='synthetic path-observation double'}}
 $app=Join-Path $script:SimpleWork 'no-worker-app';New-Item -ItemType Directory -Path $app|Out-Null
 $preference=Join-Path $root 'anything.json';Save-NeuroticModelPreference $preference $remembered
 [void](Get-NeuroticRuntimeComponents $app)
 Assert-Simple ($script:ObservedModelPath -ceq $remembered) 'UTF8 saved Unicode model preference is read exactly without model execution'
 Remove-Item -LiteralPath $preference
 $legacy=Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT 'Anything';New-Item -ItemType Directory -Path $legacy|Out-Null
 [IO.File]::WriteAllText((Join-Path $legacy 'anything.json'),(@{modelPath=$remembered}|ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
 $script:ObservedModelPath=$null;[void](Get-NeuroticRuntimeComponents $app)
 Assert-Simple ($script:ObservedModelPath -ceq $remembered) 'UTF8 legacy Unicode model preference is read exactly without model execution'
}finally{Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT;End-SimpleFixtures}
