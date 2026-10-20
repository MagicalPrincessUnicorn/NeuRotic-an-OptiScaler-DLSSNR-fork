. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
$script:Work=Join-Path $script:Repo 'builds/hub/uninstall-categories-fixture';$script:Support=Join-Path $script:Work 'app/support'
$prior=$env:NEUROTIC_HUB_FIXTURE_ROOT;$evidence=Join-Path $script:Repo 'docs/implementation-evidence/app-library-polish-20261004'
# Reuse only inert fixture constructors, never execute the bulk harness.
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'BulkUninstallBackend.Tests.ps1'),[ref]$null,[ref]$null)
foreach($name in @('Write-BulkJson','New-BulkGame')){$node=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true);Invoke-Expression $node.Extent.Text}
Init-HubFixtures
try{
 New-Item -ItemType Directory -Force $script:Support,$evidence|Out-Null
 foreach($source in Get-ChildItem -LiteralPath (Join-Path $script:Repo 'tools/packaging/customer/support') -File){Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $script:Support $source.Name)}
 foreach($case in @('default','extras','settings','changed')){
  $game=New-BulkGame $case;New-Item -ItemType Directory -Force (Join-Path $game 'NeuroticScreenshots')|Out-Null
  [IO.File]::WriteAllText((Join-Path $game 'NeuroticScreenshots/keep.png'),'screenshot');[IO.File]::WriteAllText((Join-Path $game 'NeuRotic-backups/user-not-owned.txt'),'unknown backup')
  New-Item -ItemType Directory -Force (Join-Path $game 'NeuRotic-backups/preserved/NeuroticScreenshots')|Out-Null
  [IO.File]::WriteAllText((Join-Path $game 'NeuRotic-backups/preserved/nvngx_dlssnr.dll'),'saved original model');[IO.File]::WriteAllText((Join-Path $game 'NeuRotic-backups/preserved/NeuroticScreenshots/saved.png'),'saved screenshot')
  $categories=if($case -eq 'extras'){@('backups','privateModels','screenshots')}elseif($case -eq 'changed'){@('privateModels')}else{@()}
  $request=[ordered]@{kind='Plan';operation='Uninstall';gameExecutable=(Join-Path $game 'FixtureGame.exe');uninstallMode=$(if($case -eq 'settings'){'RemoveSettings'}else{'KeepSettings'});cleanupCategories=@($categories)}
  $plan=HubCall $request;Assert ($plan.status -eq 'Planned') ($case+' categories plan')
  Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq ($case+' generation 2 dxgi.dll')) ($case+' preview leaves game intact')
  if($case -eq 'changed'){[IO.File]::WriteAllText((Join-Path $game 'nvngx_dlssnr.dll'),'new private model')}
  $result=Execute-Plan $plan
  Write-BulkJson (Join-Path $evidence ('uninstall-'+$case+'.json')) $result
  if($case -eq 'changed'){Assert ($result.status -eq 'PreconditionChanged' -and [IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq ($case+' generation 2 dxgi.dll')) 'changed optional cleanup blocks before restore';continue}
  Assert ($result.status -eq 'Succeeded') ($case+' uninstall success')
  Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq ($case+' original dxgi.dll')) ($case+' original proxy restored and retained')
  Assert ((Test-Path (Join-Path $game 'NeuRotic/UserData/OptiScaler.ini')) -eq ($case -ne 'settings')) ($case+' settings checkbox independent')
  Assert ((Test-Path (Join-Path $game 'nvngx_dlssnr.dll')) -eq ($case -ne 'extras')) ($case+' private model choice respected')
  Assert ((Test-Path (Join-Path $game 'NeuroticScreenshots/keep.png')) -eq ($case -ne 'extras')) ($case+' screenshot choice respected')
  Assert ((Test-Path (Join-Path $game 'NeuRotic-backups/install-0/INSTALL-MANIFEST.json')) -eq ($case -ne 'extras')) ($case+' verified backup choice respected')
  Assert ([IO.File]::ReadAllText((Join-Path $game 'NeuRotic-backups/preserved/nvngx_dlssnr.dll')) -eq 'saved original model') ($case+' recovery model retained')
  Assert ([IO.File]::ReadAllText((Join-Path $game 'NeuRotic-backups/preserved/NeuroticScreenshots/saved.png')) -eq 'saved screenshot') ($case+' recovery screenshot retained')
  Assert ([IO.File]::ReadAllText((Join-Path $game 'NeuRotic-backups/user-not-owned.txt')) -eq 'unknown backup') ($case+' unknown backup preserved')
 }
 $game=New-BulkGame 'empty';Remove-Item -LiteralPath (Join-Path $game 'nvngx_dlssnr.dll')
 $request=[ordered]@{kind='Plan';operation='Uninstall';gameExecutable=(Join-Path $game 'FixtureGame.exe');uninstallMode='KeepSettings';cleanupCategories=@('privateModels','screenshots')}
 $plan=HubCall $request;Assert ($plan.status -eq 'Planned' -and @($plan.uninstallCleanup.files).Count -eq 0) 'empty optional cleanup is a bounded valid review'
 $r=Execute-Plan $plan;Assert ($r.status -eq 'Succeeded') 'empty optional cleanup still restores successfully'
 # Exercise post-restoration partial failure with actual no-link, hash-bound file deletion.
 & {
  $engineAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $script:Support 'NeuRotic-Setup-Engine.ps1'),[ref]$null,[ref]$null)
  $safety=$engineAst.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Initialize-NeuroticFileSafety'},$true);Invoke-Expression $safety.Extent.Text;Initialize-NeuroticFileSafety
  $protocolAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $script:Support 'NeuRotic-HubProtocol.ps1'),[ref]$null,[ref]$null)
  $cleanup=$protocolAst.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-HubUninstallCleanup'},$true);Invoke-Expression $cleanup.Extent.Text
  $root=Join-Path $script:Work 'partial';New-Item -ItemType Directory $root|Out-Null
  [IO.File]::WriteAllText((Join-Path $root 'first.png'),'first');[IO.File]::WriteAllText((Join-Path $root 'second.png'),'second')
  $target=[pscustomobject]@{directory=$root;executable=(Join-Path $root 'FixtureGame.exe')}
  $files=@('first.png','second.png'|ForEach-Object{[pscustomobject]@{path=$_;sha256=(Get-FileHash (Join-Path $root $_)).Hash}})
  $proof=[pscustomobject]@{revision='reviewed';rootIdentity=[NeuRoticFileSafety]::Identity($root);files=$files;directories=@()}
  $plan=[pscustomobject]@{planId='partial-fixture';request=@{};uninstallCleanup=$proof}
  function Get-HubUserRoot{return $root}
  function SaveRecord($Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 16))}
  function Get-HubUninstallCleanupProof($Target,$Request){return $proof}
  function CheckGameClosed($Executable){}
  function SafePath($Root,$Relative){return Join-Path $Root $Relative}
  function Remove-NeuroticPath($LiteralPath,$ExpectedHash){if([IO.Path]::GetFileName($LiteralPath) -eq 'second.png'){throw 'Injected second-file failure'};[NeuRoticFileSafety]::Delete($LiteralPath,$ExpectedHash)}
  $result=Invoke-HubUninstallCleanup $target $plan
  Assert ($result.status -eq 'FailedWithChanges' -and $result.originalsRestored -and $result.decisionKind -eq 'UninstallCleanup') 'post-restore failure truthfully reports originals already restored'
  Assert ($result.deleted.Count -eq 1 -and $result.remaining.Count -eq 1 -and $result.pending -eq 'second.png') 'partial receipt identifies exact deleted remaining and pending files'
  Assert (-not (Test-Path (Join-Path $root 'first.png')) -and [IO.File]::ReadAllText((Join-Path $root 'second.png')) -eq 'second') 'partial cleanup keeps failed file intact'
  $receipt=Get-Content -Raw $result.receipt|ConvertFrom-Json
  Assert ($receipt.status -eq 'FailedWithChanges' -and $receipt.deleted[0] -eq 'first.png') 'durable partial cleanup receipt matches actual deletion'
 }
 foreach($bad in @(@('rootFiles'),@('installer'),@('assets'),@('privateModels','privateModels'))){$request.cleanupCategories=$bad;$r=HubCall $request;Assert ($r.status -eq 'FailedWithoutMutation') 'unsafe normal cleanup categories refused'}
}finally{
 $resolved=[IO.Path]::GetFullPath($script:Work);if($resolved -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub/uninstall-categories-fixture'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force;$env:NEUROTIC_HUB_FIXTURE_ROOT=$prior
}
