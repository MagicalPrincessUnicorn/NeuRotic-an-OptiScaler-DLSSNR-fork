param([switch]$DemonstrateLegacyRace,[switch]$AuditFixture)
. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
if($AuditFixture){
 $script:Work=Join-Path $script:Repo 'builds/audit-path-safety'
 function End-HubFixtures {
  $expected=[IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/audit-path-safety'))
  if([IO.Path]::GetFullPath($script:Work) -ine $expected){throw 'Unexpected audit fixture cleanup root'}
  $queue=New-Object Collections.Generic.Queue[string];$queue.Enqueue($expected)
  while($queue.Count){foreach($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'};if($item.PSIsContainer){$queue.Enqueue($item.FullName)}}}
  Remove-Item -LiteralPath $expected -Recurse -Force;Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
 }
}
Init-HubFixtures
try{
 $engine=Join-Path $script:Repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1';$tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
 foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
 $game=New-GameFixture 'path-safety';$owned=Join-Path $game 'owned';$outside=Join-Path $script:Work 'outside'
 New-Item -ItemType Directory -Path $owned,$outside|Out-Null
 $external=Join-Path $outside 'guard.bin';[IO.File]::WriteAllText($external,'outside bytes preserved');$before=HashFile $external
 $destination=SafePath $game 'owned/guard.bin'
 Move-Item -LiteralPath $owned -Destination ($owned+'-old')
 New-Item -ItemType Junction -Path $owned -Target $outside|Out-Null
 $bytes=[Text.Encoding]::UTF8.GetBytes('managed mutation')
 try{
  if($DemonstrateLegacyRace){[IO.File]::WriteAllBytes($destination,$bytes)}else{WriteVerifiedBytes $bytes $destination (HashBytes $bytes)}
 }catch{}
 finally{[IO.Directory]::Delete($owned,$false)}
 Assert ((HashFile $external) -eq $before) 'NH-04-PATH-BOUNDARY: junction swap after lexical validation cannot redirect write'
 Initialize-NeuroticFileSafety
 $leaf=Join-Path $game 'leaf.bin';[IO.File]::WriteAllText($leaf,'reviewed bytes');$expected=HashFile $leaf
 [IO.File]::WriteAllText($leaf,'new outside bytes');$outsideHash=HashFile $leaf
 try{Remove-NeuroticPath -LiteralPath $leaf -ExpectedHash $expected}catch{}
 Assert ((HashFile $leaf) -eq $outsideHash) 'NH-04-LEAF-BOUNDARY: changed regular file cannot be deleted'
 $source=Join-Path $game 'package.bin';[IO.File]::WriteAllBytes($source,$bytes)
 try{CopyVerified $source $leaf (HashBytes $bytes) $expected}catch{}
 Assert ((HashFile $leaf) -eq $outsideHash) 'NH-04-LEAF-BOUNDARY: changed regular file cannot be overwritten'
 try{CopyVerified $source $leaf (HashBytes $bytes) 'Absent'}catch{}
 Assert ((HashFile $leaf) -eq $outsideHash) 'NH-04-LEAF-BOUNDARY: newly occupied destination cannot be overwritten'
 $missing=Join-Path $game 'missing.ini';$written=WriteVerifiedBytes $bytes $missing (HashBytes $bytes) $expected
 Assert (-not $written -and -not (Test-Path -LiteralPath $missing)) 'NH-06-WRITE-BOUNDARY: missing revision-bound INI stays absent'
 $lease=[NeuRoticFileSafety]::PinTarget($game)
 try{
  $moved=$false;try{Move-Item -LiteralPath $game -Destination ($game+'-moved') -ErrorAction Stop;$moved=$true}catch{}
  Assert (-not $moved) 'NH-04-PATH-BOUNDARY: live root directory identity cannot be replaced'
 }finally{$lease.Dispose()}
 $alias=Join-Path $script:Work 'alias';New-Item -ItemType Junction -Path $alias -Target $game|Out-Null
 try{
  $refused=$false;try{[void][NeuRoticFileSafety]::Identity($alias)}catch{$refused=$true}
  Assert $refused 'NH-03-TARGET-LOCK: mutable directory aliases refused'
 }finally{[IO.Directory]::Delete($alias,$false)}
 # A second process must observe the same physical-directory mutex.
 $mutex=Enter-NeuroticTargetLock $game
 try{
  $lockName='Global\NeuRotic-Installer-'+[NeuRoticFileSafety]::Identity($game)
  $probe=Join-Path $script:Work 'mutex-probe.ps1'
  [IO.File]::WriteAllText($probe,"`$m=New-Object Threading.Mutex(`$false,'$lockName');try{if(`$m.WaitOne(0)){`$m.ReleaseMutex();exit 1}else{exit 0}}finally{`$m.Dispose()}")
  & $script:PowerShell -NoProfile -ExecutionPolicy Bypass -File $probe
  Assert ($LASTEXITCODE -eq 0) 'NH-03-TARGET-LOCK: second process cannot enter owned physical target'
 }finally{Release-NeuroticTargetLocks}
}finally{End-HubFixtures}
