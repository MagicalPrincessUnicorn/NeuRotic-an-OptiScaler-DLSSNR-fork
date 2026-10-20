$ErrorActionPreference='Stop'
$allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixture=Join-Path $repo 'builds/audit-installer-direct'
if(Test-Path -LiteralPath $fixture){throw 'Inspect old fixture before reuse'}
[void](New-Item -ItemType Directory -Path $fixture)
$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $fixture 'user-data'
$script:testPackageRoot=Join-Path $fixture 'package'
$game=Join-Path $fixture 'game'
[void](New-Item -ItemType Directory -Path $game,(Join-Path $script:testPackageRoot 'payload'),$env:NEUROTIC_HUB_FIXTURE_ROOT)
try{
 foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-HubProtocol.ps1','NeuRotic-HubDumbfire.ps1','NeuRotic-HubSettings.ps1','NeuRotic-HubHistory.ps1')){
  $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo ('tools/packaging/customer/support/'+$name)),[ref]$tokens,[ref]$errors)
  if($errors.Count){throw 'Production script parsing failed'}
  # AST imports do not retain the defining file's automatic PSScriptRoot.
  # Bind only that environment value to the original support directory.
  $supportLiteral="'"+(Join-Path $repo 'tools/packaging/customer/support').Replace("'","''")+"'"
  foreach($function in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($function.Extent.Text.Replace('$PSScriptRoot',$supportLiteral)))}
 }
 $exe=Join-Path $game 'FixtureGame.exe';Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination $exe
 [IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'ORIGINAL WRAPPER')
 [IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),'ORIGINAL CONFIG')
 [IO.File]::WriteAllText((Join-Path $script:testPackageRoot 'payload/OptiScaler.dll'),'DUMMY NEUROTIC PAYLOAD')
 [IO.File]::WriteAllText((Join-Path $script:testPackageRoot 'payload/OptiScaler.ini'),'DUMMY PACKAGE CONFIG')
 $script:testManifest=[pscustomobject]@{files=@(foreach($name in @('OptiScaler.dll','OptiScaler.ini')){[pscustomobject]@{path=('payload\'+$name);sha256=(HashFile (Join-Path $script:testPackageRoot ('payload/'+$name)))}})}
 # Use a tiny inert package, substituting only package lookup. Production
 # target detection, content projection, backup, write, commit and rollback
 # discovery functions remain intact. No dummy DLL is loaded.
 function Resolve-HubPackage([string]$Id){return [pscustomobject]@{id=$Id;root=$script:testPackageRoot;digest=('a'*64);manifest=$script:testManifest}}
 $request=[pscustomobject]@{operation='DumbfireInstall';gameExecutable=$exe;packageId='fixture';proxyName='dxgi.dll'}
 $plan=New-HubDumbfirePlan $request
 if($plan.status -ne 'Planned'){throw ($plan|ConvertTo-Json -Depth 8)}
 $target=Get-HubTarget $exe
 $planPath=Join-Path (Get-HubUserRoot) ($plan.planId+'.plan.json')
 $result=Invoke-HubDumbfireExecute $plan $target $planPath
 if($result.status -ne 'Succeeded'){throw ($result|ConvertTo-Json -Depth 8)}
 $quickRequest=Join-Path $fixture 'quick-request.json';$quickResult=Join-Path $fixture 'quick-result.json'
 [IO.File]::WriteAllText($quickRequest,(@{protocolVersion=1;kind='Inspect';gameExecutable=$exe}|ConvertTo-Json))
 Invoke-HubProtocol $quickRequest $quickResult
 $quick=Get-Content -LiteralPath $quickResult -Raw|ConvertFrom-Json
 if($quick.status -ne 'Inspected' -or -not $quick.dumbfireHistory.rollbackAvailable -or $quick.inspection.target.sha256){throw ('Quick Inspect failed with completed direct history: '+($quick|ConvertTo-Json -Depth 8))}
 Write-Output 'PASS Quick Inspect discovers completed direct recovery without hashing the executable'
 $pendingPath=Join-Path (Get-HubDumbfireStore $target) 'Pending.json'
 SaveRecord $pendingPath @{kind='neurotic-dumbfire-pending';transactionId=$quick.dumbfireHistory.transactionId;directory=$target.directory}
 Invoke-HubProtocol $quickRequest $quickResult
 $quick=Get-Content -LiteralPath $quickResult -Raw|ConvertFrom-Json
 if($quick.status -ne 'Inspected' -or -not $quick.dumbfireHistory.pending){throw 'Quick Inspect failed with pending direct recovery'}
 Remove-NeuroticPath -LiteralPath $pendingPath -ExpectedHash (HashFile $pendingPath)
 Write-Output 'PASS Quick Inspect discovers pending direct recovery'
 $rollback=New-HubDumbfirePlan ([pscustomobject]@{operation='DumbfireRollback';gameExecutable=$exe})
 if($rollback.status -ne 'Planned' -or -not $result.rollbackAvailable){throw 'Successful direct installation must remain discoverably recoverable'}
 $restored=Invoke-HubDumbfireExecute $rollback $target (Join-Path (Get-HubUserRoot) ($rollback.planId+'.plan.json'))
 if($restored.status -ne 'Succeeded' -or [IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -ne 'ORIGINAL WRAPPER' -or [IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini')) -ne 'ORIGINAL CONFIG'){throw 'Completed direct install did not restore original bytes'}
 if((Get-HubDumbfireHistory $target).rollbackAvailable){throw 'Restored direct installation must leave the completion index'}
 Write-Output 'PASS Successful direct installation is discovered and restores exact wrapper/config originals'
}finally{
 Release-NeuroticTargetLocks
 $absolute=[IO.Path]::GetFullPath($fixture)
 if($absolute -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/audit-installer-direct'))){throw 'Unexpected cleanup target'}
 $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($absolute)
 while($pending.Count){foreach($item in Get-ChildItem -LiteralPath $pending.Pop() -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked cleanup refused'};if($item.PSIsContainer){$pending.Push($item.FullName)}}}
 Remove-Item -LiteralPath $absolute -Recurse -Force
 Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
}
