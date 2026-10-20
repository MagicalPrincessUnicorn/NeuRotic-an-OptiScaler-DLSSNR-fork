param([Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference='Stop';Set-StrictMode -Version 2
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work=Join-Path $repo 'builds/hub/dumbfire-package-fixture';if(Test-Path -LiteralPath $work){throw 'Prior Dumbfire package fixture remains'}
$engine=Join-Path ([IO.Path]::GetFullPath($PackageRoot)) 'support/NeuRotic-Setup-Engine.ps1'
if(-not (Test-Path -LiteralPath $engine)){throw 'Select the sealed package root'}
$oldFixture=$env:NEUROTIC_HUB_FIXTURE_ROOT;$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user'
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
function Call-Package($request){
 $request.protocolVersion=1;$request.requestId=[Guid]::NewGuid().ToString('N')
 $inputPath=Join-Path $work 'request.json';$outputPath=Join-Path $work 'result.json'
 [IO.File]::WriteAllText($inputPath,($request|ConvertTo-Json -Depth 12));if(Test-Path $outputPath){Remove-Item -LiteralPath $outputPath}
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $engine -HubRequestPath $inputPath -HubResultPath $outputPath -HubNonInteractive *> (Join-Path $work 'worker.log')
 if(-not (Test-Path $outputPath)){throw ('Worker produced no receipt: '+[IO.File]::ReadAllText((Join-Path $work 'worker.log')))}
 return (Get-Content -Raw -LiteralPath $outputPath|ConvertFrom-Json)
}
try{
 $game=Join-Path $work 'game';[void][IO.Directory]::CreateDirectory((Join-Path $game 'NeuRotic/Installer'))
 Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $game 'Fixture.exe')
 foreach($entry in @{'dxgi.dll'='prior foreign proxy';'OptiScaler.ini'='prior edited settings';'ReShade64.dll'='foreign ReShade';'private.txt'='untouched';'NeuRotic/Installer/Current-Install.json'='broken old history';'NeuRotic/Installer/Uninstall-Transaction.json'='old journal'}.GetEnumerator()){[IO.File]::WriteAllText((Join-Path $game $entry.Key),$entry.Value)}
 $plan=Call-Package ([ordered]@{kind='Plan';operation='DumbfireInstall';gameExecutable=(Join-Path $game 'Fixture.exe');packageId='flagship-approved';proxyName='dxgi.dll'})
 Assert ($plan.status -eq 'Planned' -and $plan.dumbfire -and @($plan.changes).Count -gt 2) ('Sealed package plans finite payload despite broken history: '+$plan.status)
 $result=Call-Package ([ordered]@{kind='Execute';planId=$plan.planId;planFingerprint=$plan.planFingerprint})
 Assert ($result.status -eq 'Succeeded' -and $result.dumbfire -and $result.planId -eq $plan.planId) ('Sealed package direct install succeeds: '+$result.status+' '+$result.reason)
 foreach($file in $plan.changes){Assert ((Get-FileHash -LiteralPath (Join-Path $game $file.path)).Hash -eq $file.sha256) ('Verified packaged destination '+$file.path)}
 Assert ([IO.File]::ReadAllText((Join-Path $game 'NeuRotic/Installer/Current-Install.json')) -eq 'broken old history' -and [IO.File]::ReadAllText((Join-Path $game 'NeuRotic/Installer/Uninstall-Transaction.json')) -eq 'old journal') 'Sealed worker preserves old history and old pending journal'
 # Convert this fixture-only completed record into an interruption immediately
 # after the final native write, before completion was durably acknowledged.
 $journal=Get-Content -Raw -LiteralPath $result.receipt|ConvertFrom-Json;$journal.status='Writing';$journal.pending=$journal.files[-1].path;$journal.files[-1].written=$false
 [IO.File]::WriteAllText($result.receipt,($journal|ConvertTo-Json -Depth 12))
 $store=Split-Path -Parent $result.backupRoot
 [IO.File]::WriteAllText((Join-Path $store 'Pending.json'),([ordered]@{kind='neurotic-dumbfire-pending';transactionId=$plan.planId;directory=$game}|ConvertTo-Json))
 Remove-Item -LiteralPath (Join-Path $store 'Completed.json')
 $rollback=Call-Package ([ordered]@{kind='Plan';operation='DumbfireRollback';gameExecutable=(Join-Path $game 'Fixture.exe')})
 Assert ($rollback.status -eq 'Planned' -and $rollback.dumbfire -and @($rollback.changes).Count -eq @($journal.files).Count) 'Sealed worker independently reviews interrupted direct payload recovery'
 $restored=Call-Package ([ordered]@{kind='Execute';planId=$rollback.planId;planFingerprint=$rollback.planFingerprint})
 Assert ($restored.status -eq 'Succeeded' -and $restored.dumbfire -and -not $restored.rollbackAvailable) ('Sealed worker executes dedicated fallback rollback: '+$restored.status)
 Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'prior foreign proxy' -and [IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini')) -eq 'prior edited settings') 'Sealed worker restores exact immediate preimages'
 foreach($file in $journal.files){if(-not $file.existed){Assert (-not (Test-Path -LiteralPath (Join-Path $game $file.path))) ('Rollback removes only its created file '+$file.path)}}
 Assert ([IO.File]::ReadAllText((Join-Path $game 'ReShade64.dll')) -eq 'foreign ReShade' -and [IO.File]::ReadAllText((Join-Path $game 'private.txt')) -eq 'untouched') 'Sealed worker leaves unrelated content intact'
}finally{
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$oldFixture
 if([IO.Path]::GetFullPath($work) -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/dumbfire-package-fixture'))){throw 'Unexpected package fixture path'}
 if(Test-Path -LiteralPath $work){foreach($entry in Get-ChildItem -LiteralPath $work -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}};Remove-Item -LiteralPath $work -Recurse -Force}
}
