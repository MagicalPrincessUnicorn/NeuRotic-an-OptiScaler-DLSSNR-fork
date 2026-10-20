param([string]$ChildDestination)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine=Join-Path $repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$null,[ref]$null)
foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
Initialize-NeuroticFileSafety
$before=[Text.Encoding]::UTF8.GetBytes('original content')
$after=[Text.Encoding]::UTF8.GetBytes(('complete staged content '*4096))
if($ChildDestination){[void][NeuRoticFileSafety]::Write($after,$ChildDestination,(HashBytes $after),(HashBytes $before));exit 0}
$root=Join-Path $repo 'builds/audit-publication';[void][IO.Directory]::CreateDirectory($root)
foreach($phase in @('stage-created','stage-flushed','before-publish','after-publish')){
 $destination=Join-Path $root ($phase+'.json');[IO.File]::WriteAllBytes($destination,$before)
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName="$env:windir/System32/WindowsPowerShell/v1.0/powershell.exe";$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.EnvironmentVariables['NEUROTIC_PUBLICATION_TEST_ROOT']=$root;$start.EnvironmentVariables['NEUROTIC_PUBLICATION_TEST_BOUNDARY']=$phase
 $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -ChildDestination "'+$destination+'"'
 $child=[Diagnostics.Process]::Start($start)
 try{$o=$child.StandardOutput.ReadToEndAsync();$e=$child.StandardError.ReadToEndAsync();if(-not $child.WaitForExit(10000)){$child.Kill();throw 'Publication fixture timeout'};if($child.ExitCode -eq 0){throw 'Fixture did not terminate at the publication boundary'};[IO.File]::WriteAllText((Join-Path $root ($phase+'.log')),$o.Result+$e.Result)}finally{$child.Dispose()}
 $expected=if($phase -eq 'after-publish'){HashBytes $after}else{HashBytes $before}
 if((HashFile $destination) -ne $expected){throw ('Terminated publication exposed partial bytes: '+$phase)}
 if(-not [NeuRoticFileSafety]::Write($before,$destination,(HashBytes $before),$expected)){throw 'Verified recovery refused complete owned side'}
 if((HashFile $destination) -ne (HashBytes $before)){throw 'Publication recovery did not restore complete original'}
 Write-Output ('PASS terminated publication leaves a complete recoverable side at '+$phase)
}
$destination=Join-Path $root 'locked.json';[IO.File]::WriteAllBytes($destination,$before)
$locked=[IO.File]::Open($destination,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
$refused=$false
try{try{[void][NeuRoticFileSafety]::Write($after,$destination,(HashBytes $after),(HashBytes $before))}catch{$refused=$true}}finally{$locked.Dispose()}
if(-not $refused -or (HashFile $destination) -ne (HashBytes $before)){throw 'Locked publication changed original'}
Write-Output 'PASS locked destination refuses publication and retains exact original bytes'
[IO.File]::WriteAllText($destination,'foreign bytes')
if([NeuRoticFileSafety]::Write($after,$destination,(HashBytes $after),(HashBytes $before)) -or [IO.File]::ReadAllText($destination) -ne 'foreign bytes'){throw 'Stale publication overwrote foreign content'}
Write-Output 'PASS foreign content before publication admission is preserved'
