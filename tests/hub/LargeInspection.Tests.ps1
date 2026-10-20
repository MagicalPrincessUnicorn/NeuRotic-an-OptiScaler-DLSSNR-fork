. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
try {
 $game=New-GameFixture 'large-history';[IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),"[Menu]`nOverlayMenu=false`n");$result=Execute-Plan (Plan-Game $game 'Install')
 Assert ($result.status -eq 'Succeeded') 'NH-HISTORY: inert managed installation created'
 Assert (-not ($result.inspection.PSObject.Properties.Name -contains 'inventory')) 'NH-HISTORY: successful mutation returns a compact inspection too'
 $baseline=HubCall ([ordered]@{kind='Inspect';gameExecutable=(Join-Path $game 'FixtureGame.exe')})
 Assert ($baseline.status -eq 'Inspected') 'NH-HISTORY: same-schema inspection baseline captured before old history'
 $baselineBytes=[Text.Encoding]::UTF8.GetByteCount(($baseline|ConvertTo-Json -Depth 20))
 $baselineWireBytes=(Get-ChildItem -LiteralPath $script:Work -Filter '*.result.json'|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1).Length
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$savedState=Get-Content -Raw -LiteralPath $statePath|ConvertFrom-Json
 $savedState.install_history=@(foreach($i in 1..3000){[pscustomobject]@{version=('Retired release '+$i);notes=('historic receipt detail '*24)}})
 [IO.File]::WriteAllText($statePath,($savedState|ConvertTo-Json -Depth 20))
 Assert ((Get-Item -LiteralPath $statePath).Length -gt 1048576) 'NH-HISTORY: installation history itself exceeds the old response limit'
 $history=Join-Path $game 'NeuRotic-backups/retired-version';New-Item -ItemType Directory -Path $history|Out-Null
 # Old, unrelated version files must neither inflate the response nor be read.
 foreach($i in 1..3500){[IO.File]::WriteAllText((Join-Path $history ('old-file-'+$i.ToString('D4')+'.txt')),'old version')}
 $locked=Join-Path $history 'old-file-0001.txt';$handle=[IO.File]::Open($locked,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 try{$inspection=HubCall ([ordered]@{kind='Inspect';gameExecutable=(Join-Path $game 'FixtureGame.exe')})}finally{$handle.Dispose()}
 Assert ($inspection.status -eq 'Inspected') 'NH-HISTORY: routine check does not open retired version payloads'
 $historyBytes=[Text.Encoding]::UTF8.GetByteCount(($inspection|ConvertTo-Json -Depth 20))
 $historyWireBytes=(Get-ChildItem -LiteralPath $script:Work -Filter '*.result.json'|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1).Length
 Write-Output ('NH-HISTORY bytes: baseline='+$baselineBytes+' history='+$historyBytes+' wireBaseline='+$baselineWireBytes+' wireHistory='+$historyWireBytes+' settings='+@($inspection.inspection.settings).Count)
 # Settings are a fixed projection shared by both responses. Allow only a
 # small bounded delta for timing/counter text, not per-history-entry growth.
 Assert (($inspection.inspection.settings|ConvertTo-Json -Depth 20 -Compress) -ceq ($baseline.inspection.settings|ConvertTo-Json -Depth 20 -Compress)) 'NH-HISTORY: historical data does not change the settings projection'
 Assert ($historyBytes -le $baselineBytes+1024 -and $historyWireBytes -le $baselineWireBytes+1024) 'NH-HISTORY: thousands of old entries add at most 1 KiB over same-schema baseline'
 Assert ($historyWireBytes -lt 1048576) 'NH-HISTORY: actual inspection receipt stays within the App 1 MiB response limit'
 Assert (-not ($inspection.inspection.PSObject.Properties.Name -contains 'inventory')) 'NH-HISTORY: raw backup inventory is not sent to the App'
 Assert (-not ($inspection.inspection.state.PSObject.Properties.Name -contains 'restore_chain')) 'NH-HISTORY: UI state omits the full restore chain'
 Assert (-not ($inspection.inspection.state.PSObject.Properties.Name -contains 'install_history')) 'NH-HISTORY: UI state omits accumulated installation history'
 Assert ($inspection.inspection.conflicts.Count -eq 0 -and $inspection.inspection.missing.Count -eq 0) 'NH-HISTORY: current managed files are still verified'
 [IO.File]::AppendAllText((Join-Path $game 'dxgi.dll'),'changed')
 $changed=HubCall ([ordered]@{kind='Inspect';gameExecutable=(Join-Path $game 'FixtureGame.exe')})
 Assert ($changed.inspection.conflicts -contains 'dxgi.dll') 'NH-HISTORY: current-file modification is still reported'
 $plan=Plan-Game $game 'Repair';Assert ($plan.status -eq 'NeedsDecision') 'NH-HISTORY: mutation still requires a decision for changed current files'
 $state=Get-Content -Raw -LiteralPath (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 $record=Get-Content -Raw -LiteralPath $state.restore_chain[-1]|ConvertFrom-Json
 # Restore the loader using the exact installed package, then damage a required backup.
 Copy-Item -LiteralPath (Join-Path $script:Repo 'builds/hub/bin/packages/current/payload/OptiScaler.dll') -Destination (Join-Path $game 'dxgi.dll') -Force
 $previous=@($record.files|Where-Object {$_.existed})[0]
 Assert ($null -ne $previous) 'NH-HISTORY: fixture contains a required restore backup'
 $backup=Join-Path (Join-Path $record.backup 'previous') $previous.path;[IO.File]::AppendAllText($backup,'damaged');$plan=Plan-Game $game 'Uninstall'
 Assert ($plan.status -eq 'Planned') 'NH-HISTORY: preview checks metadata without rereading old payloads'
 $result=Execute-Plan $plan;Assert ($result.status -eq 'FailedWithoutMutation') 'NH-HISTORY: full backup validation remains mandatory before restoration'
} finally {End-HubFixtures}
