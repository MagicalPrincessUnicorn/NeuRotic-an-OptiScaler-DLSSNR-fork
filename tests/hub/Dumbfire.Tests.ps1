$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot);$support=Join-Path $repo 'tools/packaging/customer/support'
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-HubProtocol.ps1')){
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $support $name),[ref]$tokens,[ref]$errors)
 foreach($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){. ([scriptblock]::Create($definition.Extent.Text))}
}
$module=Join-Path $support 'NeuRotic-HubDumbfire.ps1';if(Test-Path $module){. $module}
$allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
$work=Join-Path $repo 'builds/hub/dumbfire-fixture';if(Test-Path $work){throw 'Prior Dumbfire fixture remains'}
New-Item -ItemType Directory -Path (Join-Path $work 'package/payload/OptiScaler') -Force|Out-Null
try{
 $packageRoot=Join-Path $work 'package';$rows=@()
 foreach($name in @('OptiScaler.dll','OptiScaler.ini','OptiScaler/asset.bin')){$path=Join-Path $packageRoot ('payload/'+$name);[IO.File]::WriteAllText($path,('verified package '+$name));$rows+=[pscustomobject]@{path=('payload\'+$name.Replace('/','\'));sha256=(HashFile $path);bytes=(Get-Item $path).Length}}
 $script:fixturePackage=[pscustomobject]@{id='flagship-approved';root=$packageRoot;manifest=[pscustomobject]@{files=$rows};digest=('a'*64)}
 function Resolve-HubPackage($Id){if($Id -ne 'flagship-approved'){throw 'Package not approved'};foreach($file in $script:fixturePackage.manifest.files){if((HashFile (Join-Path $script:fixturePackage.root $file.path)) -ne $file.sha256){throw 'Package changed'}};return $script:fixturePackage}
 function Get-HubUserRoot {$path=Join-Path $work 'user';[void][IO.Directory]::CreateDirectory($path);return $path}
 function Get-HubInspection {throw 'Dumbfire must never inspect old installation history'}
 function Get-HubAntiCheat {throw 'Dumbfire must never scan anti-cheat'}
 function Initialize-HubAntiCheatCollector {throw 'Dumbfire must never initialize anti-cheat work'}
 function New-DumbfireFixture($name){
  $root=Join-Path $work $name;[void][IO.Directory]::CreateDirectory((Join-Path $root 'NeuRotic/Installer'))
  Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $root 'Fixture.exe')
  foreach($entry in @{'dxgi.dll'='foreign proxy';'OptiScaler.ini'='edited settings';'other.dll'='unrelated';'ReShade64.dll'='foreign ReShade';'NeuRotic/Installer/Current-Install.json'='corrupt old history';'NeuRotic/Installer/Uninstall-Transaction.json'='old pending journal'}.GetEnumerator()){[IO.File]::WriteAllText((Join-Path $root $entry.Key),$entry.Value)}
  return $root
 }
 $game=New-DumbfireFixture 'success';$request=[pscustomobject]@{kind='Plan';operation='DumbfireInstall';gameExecutable=(Join-Path $game 'Fixture.exe');packageId='flagship-approved';proxyName='dxgi.dll'}
 $x86Game=New-DumbfireFixture 'x86-incompatible'
 Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'SysWOW64/cmd.exe') -Destination (Join-Path $x86Game 'Fixture.exe') -Force
 $x86Refused=$false
 $x86Plan=New-HubPlan ([pscustomobject]@{kind='Plan';operation='DumbfireInstall';gameExecutable=(Join-Path $x86Game 'Fixture.exe');packageId='flagship-approved';proxyName='dxgi.dll'})
 $x86Refused=$x86Plan.status -eq 'FailedWithoutMutation' -and $x86Plan.reason -match '32-bit' -and $x86Plan.reason -match 'NR Anything'
 Assert $x86Refused 'Dumbfire refuses an x64 payload for a 32-bit executable and recommends NR Anything'
 Assert ([IO.File]::ReadAllText((Join-Path $x86Game 'dxgi.dll')) -eq 'foreign proxy') 'Architecture refusal preserves the original proxy'
 foreach($appName in @('NeuRotic.exe','NeuRotic.Hub.exe')){
  $marker=Join-Path $game $appName;[IO.File]::WriteAllText($marker,'app marker')
  try{$refused=$false;try{Get-HubDumbfireProjection (Get-HubTarget $request.gameExecutable) $script:fixturePackage 'dxgi.dll'|Out-Null}catch{$refused=$_.Exception.Message -eq 'Application or volume root cannot receive Dumbfire installation'};Assert $refused ('Dumbfire protects App folder marked by '+$appName)}finally{[IO.File]::Delete($marker)}
 }
 $plan=New-HubPlan $request
 Assert ($plan.status -eq 'Planned' -and @($plan.changes).Count -eq 3 -and $plan.dumbfire) 'Dumbfire plans only finite verified payload destinations despite corrupt history'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'foreign proxy') 'Dumbfire preview never writes'
 $result=Invoke-HubExecute ([pscustomobject]@{planId=$plan.planId;planFingerprint=$plan.planFingerprint})
 Assert ($result.status -eq 'Succeeded' -and $result.dumbfire -and -not $result.managedHistoryChanged) 'Explicit Dumbfire installs verified payload without ordinary checks'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'verified package OptiScaler.dll' -and [IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini')) -eq 'verified package OptiScaler.ini') 'Required proxy and user INI are explicitly overwritten with package bytes'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'NeuRotic/Installer/Current-Install.json')) -eq 'corrupt old history' -and [IO.File]::ReadAllText((Join-Path $game 'NeuRotic/Installer/Uninstall-Transaction.json')) -eq 'old pending journal') 'Old managed history and pending recovery bytes remain untouched'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'other.dll')) -eq 'unrelated' -and [IO.File]::ReadAllText((Join-Path $game 'ReShade64.dll')) -eq 'foreign ReShade') 'Unrelated files and ReShade remain untouched'
 Assert (Test-Path -LiteralPath $result.receipt) 'Independent durable Dumbfire receipt is retained'
 function Plan-Case($name){$root=New-DumbfireFixture $name;return New-HubPlan ([pscustomobject]@{kind='Plan';operation='DumbfireInstall';gameExecutable=(Join-Path $root 'Fixture.exe');packageId='flagship-approved';proxyName='dxgi.dll'})}
 function Execute-Case($plan){return Invoke-HubExecute ([pscustomobject]@{planId=$plan.planId;planFingerprint=$plan.planFingerprint})}
 $stale=Plan-Case 'stale';[IO.File]::WriteAllText((Join-Path $stale.target.directory 'dxgi.dll'),'external change')
 $r=Execute-Case $stale;Assert ($r.status -eq 'PreconditionChanged' -and $r.dumbfire -and [IO.File]::ReadAllText((Join-Path $stale.target.directory 'dxgi.dll')) -eq 'external change') 'Changed required destination refuses entire install before mutation'
 $directory=New-DumbfireFixture 'directory';Remove-Item -LiteralPath (Join-Path $directory 'dxgi.dll');[void][IO.Directory]::CreateDirectory((Join-Path $directory 'dxgi.dll'))
 $r=New-HubPlan ([pscustomobject]@{operation='DumbfireInstall';gameExecutable=(Join-Path $directory 'Fixture.exe');packageId='flagship-approved';proxyName='dxgi.dll'})
 Assert ($r.status -eq 'FailedWithoutMutation' -and $r.dumbfire) 'Directory occupying required payload file is refused'
 $script:realCopy=${function:CopyVerified};$script:inject=''
 function CopyVerified([string]$Source,[string]$Destination,[string]$Hash,[string]$ExpectedCurrentHash){
  if($script:inject -eq 'afterwrite' -and $Source.StartsWith($packageRoot,[StringComparison]::OrdinalIgnoreCase) -and $Destination.EndsWith('asset.bin')){& $script:realCopy $Source $Destination $Hash $ExpectedCurrentHash;throw 'Injected interruption after native write before completion journal'}
  if($script:inject -in @('failure','outside') -and $Source.StartsWith($packageRoot,[StringComparison]::OrdinalIgnoreCase) -and $Destination.EndsWith('OptiScaler.ini')){
   if($script:inject -eq 'outside'){[IO.File]::WriteAllText((Join-Path (Split-Path -Parent $Destination) 'dxgi.dll'),'external after first write')}
   throw 'Injected payload failure'
  }
  & $script:realCopy $Source $Destination $Hash $ExpectedCurrentHash
 }
 $failed=Plan-Case 'failure';$script:inject='failure';$r=Execute-Case $failed;$script:inject=''
 Assert ($r.status -eq 'FailedWithChanges' -and -not $r.rollbackAvailable -and $r.remainingCreatedDirectories -contains 'OptiScaler') 'Failure restores this attempt and truthfully reports remaining created directory'
 Assert ([IO.File]::ReadAllText((Join-Path $failed.target.directory 'dxgi.dll')) -eq 'foreign proxy' -and [IO.File]::ReadAllText((Join-Path $failed.target.directory 'OptiScaler.ini')) -eq 'edited settings') 'Automatic rollback restores exact immediate preimages'
 $interrupted=Plan-Case 'interrupted';$script:inject='afterwrite';$r=Execute-Case $interrupted;$script:inject=''
 Assert ($r.status -eq 'FailedWithChanges' -and -not (Test-Path -LiteralPath (Join-Path $interrupted.target.directory 'OptiScaler/asset.bin')) -and [IO.File]::ReadAllText((Join-Path $interrupted.target.directory 'dxgi.dll')) -eq 'foreign proxy') 'Write intent recovers the native-write/completion-journal interruption boundary'
 $partial=Plan-Case 'outside';$script:inject='outside';$r=Execute-Case $partial;$script:inject=''
 Assert ($r.status -eq 'RecoveryRequired' -and $r.rollbackAvailable -and $r.dumbfire) 'Outside edit during failure leaves dedicated recovery pending'
 Assert ([IO.File]::ReadAllText((Join-Path $partial.target.directory 'dxgi.dll')) -eq 'external after first write') 'Automatic rollback preserves outside edits'
 $pendingReceipt=$r.receipt;$pendingHash=HashFile $pendingReceipt
 $blocked=New-HubPlan $partial.request;Assert ($blocked.status -eq 'RecoveryRequired' -and $blocked.rollbackAvailable) 'Own pending transaction blocks another direct install'
 $rollbackRequest=[pscustomobject]@{kind='Plan';operation='DumbfireRollback';gameExecutable=$partial.target.executable}
 $blocked=New-HubPlan $rollbackRequest;Assert ($blocked.status -eq 'RecoveryRequired' -and (HashFile $pendingReceipt) -eq $pendingHash) 'Recovery review preserves outside edits and original journal'
 $script:realClosed=${function:CheckGameClosed};function CheckGameClosed {throw 'Fixture game is running'}
 $prior=Invoke-HubDumbfireExecute ([pscustomobject]$partial) $partial.target (Join-Path (Get-HubUserRoot) ($partial.planId+'.plan.json'))
 Assert ($prior.status -eq 'RecoveryRequired' -and (HashFile $pendingReceipt) -eq $pendingHash -and [IO.File]::ReadAllText((Join-Path $partial.target.directory 'dxgi.dll')) -eq 'external after first write') 'Running-game guard never rolls back a prior transaction without confirmation'
 Set-Item Function:CheckGameClosed $script:realClosed
 [IO.File]::WriteAllText((Join-Path $partial.target.directory 'dxgi.dll'),'verified package OptiScaler.dll')
 $rollback=New-HubPlan $rollbackRequest;Assert ($rollback.status -eq 'Planned' -and $rollback.dumbfire -and @($rollback.changes).Count -eq 1) 'Dedicated recovery preview binds the pending transaction and exact files'
 $recovered=Execute-Case $rollback;Assert ($recovered.status -eq 'Succeeded' -and $recovered.dumbfire -and -not $recovered.rollbackAvailable -and [IO.File]::ReadAllText((Join-Path $partial.target.directory 'dxgi.dll')) -eq 'foreign proxy') 'Explicit fallback rollback restores preimage without managed history'
 $retry=New-HubPlan $partial.request;Assert ($retry.status -eq 'Planned') 'Successful independent rollback clears its pending marker'
 Set-Item Function:CopyVerified $script:realCopy
 $script:realSave=${function:SaveRecord};$script:failReady=$true
 function SaveRecord([string]$Path,$Value){if($script:failReady -and (Get-HubProperty $Value 'status') -eq 'Ready'){throw 'Injected journal failure after required directory creation'};& $script:realSave $Path $Value}
 $dirFailure=Plan-Case 'directory-failure';$r=Execute-Case $dirFailure;$script:failReady=$false;Set-Item Function:SaveRecord $script:realSave
 Assert ($r.status -eq 'FailedWithChanges' -and -not $r.rollbackAvailable -and $r.remainingCreatedDirectories -contains 'OptiScaler' -and [IO.File]::ReadAllText((Join-Path $dirFailure.target.directory 'dxgi.dll')) -eq 'foreign proxy') 'Journal failure after parent creation reports directory changes without claiming payload writes'
 $retained=@($result.backupRoot)
 for($i=0;$i -lt 3;$i++){
  [IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),('explicit revision '+$i))
  $p=New-HubPlan $request;$r=Execute-Case $p;Assert ($r.status -eq 'Succeeded' -and -not (Get-HubProperty $r 'retentionWarning')) ('Successful preimage retention iteration '+$i);$retained+=@($r.backupRoot)
 }
 $old=Read-HubDumbfireJson (Join-Path $retained[0] 'Transaction.json')
 Assert ($old.preimagesPruned -and -not (Test-Path -LiteralPath (Join-Path $retained[0] 'before/0000.bin'))) 'Fourth success prunes only oldest successful preimages and retains its receipt'
 foreach($root in @($retained|Select-Object -Skip 1)){Assert (Test-Path -LiteralPath (Join-Path $root 'before/0000.bin')) 'Last three successful preimage sets remain available'}
 $p=New-HubPlan $request;$r=Execute-Case $p;Assert ($r.status -eq 'Succeeded') 'Already verified payload succeeds without needless overwrites'
 $overlap=Get-HubTarget (Join-Path $game 'Fixture.exe');$badPackage=[pscustomobject]@{root=$game;manifest=$script:fixturePackage.manifest;digest=$script:fixturePackage.digest};$refused=$false
 try{Get-HubDumbfireProjection $overlap $badPackage 'dxgi.dll'|Out-Null}catch{$refused=$true}
 Assert $refused 'Selected target and package overlap is refused'
 $jsonSupport=Join-Path $support 'NeuRotic-HubJson.cs';if(-not ('NeuRoticHubJson' -as [type])){Add-Type -Path $jsonSupport}
 $typed=[ordered]@{protocolVersion=1;requestId='fixture';kind='Plan';operation='DumbfireInstall';gameExecutable=$request.gameExecutable;packageId='flagship-approved';proxyName='dxgi.dll';gameRoot=$game;steamRoot='C:\Steam';steamAppId='123'}
 Assert ((Read-HubRequestText ($typed|ConvertTo-Json -Compress)).operation -eq 'DumbfireInstall') 'Typed Dumbfire plan accepts standard game identity metadata'
 $typed.existingProxyAction='keep';$refused=$false;try{Read-HubRequestText ($typed|ConvertTo-Json -Compress)|Out-Null}catch{$refused=$true};Assert $refused 'Dumbfire rejects unrelated conflict policy fields'
 $badPackage=[pscustomobject]@{root=$packageRoot;manifest=[pscustomobject]@{files=@($script:fixturePackage.manifest.files)+@($script:fixturePackage.manifest.files[0])};digest=$script:fixturePackage.digest};$refused=$false
 try{Get-HubDumbfireProjection $overlap $badPackage 'dxgi.dll'|Out-Null}catch{$refused=$true};Assert $refused 'Duplicate mapped payload destinations are refused'
 $badPackage.manifest.files=@([pscustomobject]@{path='payload\OptiScaler\..\..\outside.dll';sha256=('a'*64)});$refused=$false
 try{Get-HubDumbfireProjection $overlap $badPackage 'dxgi.dll'|Out-Null}catch{$refused=$true};Assert $refused 'Payload traversal is refused before any destination mutation'
 $linkGame=New-DumbfireFixture 'linked';$link=Join-Path $linkGame 'OptiScaler';$linkOutside=Join-Path $work 'link-outside';[void][IO.Directory]::CreateDirectory($linkOutside)
 try{
  New-Item -ItemType Junction -Path $link -Target $linkOutside|Out-Null
  $r=New-HubPlan ([pscustomobject]@{operation='DumbfireInstall';gameExecutable=(Join-Path $linkGame 'Fixture.exe');packageId='flagship-approved';proxyName='dxgi.dll'})
  Assert ($r.status -eq 'FailedWithoutMutation' -and @(Get-ChildItem -LiteralPath $linkOutside).Count -eq 0) 'Linked destination parent is refused and outside directory stays untouched'
 }finally{if(Test-Path -LiteralPath $link){[IO.Directory]::Delete($link)}}
 $exeChanged=Plan-Case 'executable-change';[IO.File]::AppendAllText($exeChanged.target.executable,'fixture change')
 $r=Execute-Case $exeChanged;Assert ($r.status -eq 'FailedWithoutMutation' -and $r.dumbfire -and [IO.File]::ReadAllText((Join-Path $exeChanged.target.directory 'dxgi.dll')) -eq 'foreign proxy') 'Executable identity change refuses before any write'
}finally{
 Release-NeuroticTargetLocks
 $resolved=[IO.Path]::GetFullPath($work);if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/dumbfire-fixture'))){throw 'Unexpected Dumbfire fixture root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
