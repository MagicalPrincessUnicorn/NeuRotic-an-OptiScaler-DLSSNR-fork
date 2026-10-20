. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
try{
 $game=New-GameFixture 'lifecycle'
 $result=Execute-Plan (Plan-Game $game 'Install');Assert ($result.status -eq 'Succeeded') 'NH-05 setup'
 Remove-Item -LiteralPath (Join-Path $game 'dxgi.dll')
 $result=Execute-Plan (Plan-Game $game 'Repair');Assert ($result.status -eq 'Succeeded' -and (Test-Path (Join-Path $game 'dxgi.dll'))) 'NH-05-REPAIR: missing managed loader restored'
 $result=Execute-Plan (Plan-Game $game 'ChangeProxy' 'winmm.dll');Assert ($result.status -eq 'Succeeded' -and -not (Test-Path (Join-Path $game 'dxgi.dll')) -and (Test-Path (Join-Path $game 'winmm.dll'))) ('NH-05-PROXY-CHANGE: owner transitions proxy: '+$result.status+' '+$result.reason)
 [IO.File]::WriteAllText((Join-Path $game 'winmm.dll'),'game update')
 $changedHash=(Get-FileHash -LiteralPath (Join-Path $game 'winmm.dll')).Hash
 $plan=Plan-Game $game 'Uninstall' 'winmm.dll';Assert ($plan.status -eq 'Planned') 'NH-05-CONFLICT-RESTORE: owner reviews modified-loader preservation and original restore'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'winmm.dll')) -eq 'game update') 'NH-05-CONFLICT-RESTORE: review leaves current bytes intact'
 $result=Execute-Plan $plan;Assert ($result.status -eq 'Succeeded') 'NH-05-UNINSTALL-MODES: KeepSettings owner completes'
 # User edits are retained outside disposable restore backups; FILES.json maps
 # their original names to bounded snapshot filenames.
 $changedCopies=@()
 foreach($record in Get-ChildItem -LiteralPath (Join-Path $game 'NeuRotic-preserved-files') -File -Recurse -Filter 'FILES.json'){
  $manifest=Get-Content -Raw -LiteralPath $record.FullName|ConvertFrom-Json
  Assert ($manifest.kind -eq 'neurotic-preserved-files' -and $manifest.game_directory -eq $game) 'NH-05-CONFLICT-RESTORE: preserved-file manifest belongs to this game'
  foreach($file in @($manifest.files|Where-Object path -eq 'winmm.dll')){
   $snapshot=[IO.Path]::GetFullPath((Join-Path $record.DirectoryName $file.snapshot))
   Assert ($snapshot.StartsWith($record.DirectoryName+'\',[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $snapshot -PathType Leaf)) 'NH-05-CONFLICT-RESTORE: manifest maps changed loader to an existing local snapshot'
   Assert ($file.sha256 -eq $changedHash -and (Get-FileHash -LiteralPath $snapshot).Hash -eq $changedHash -and [IO.File]::ReadAllText($snapshot) -eq 'game update') 'NH-05-CONFLICT-RESTORE: preserved snapshot retains exact changed bytes and hash'
   $changedCopies+=$snapshot
  }
 }
 Assert ($changedCopies.Count -eq 1 -and -not (Test-Path (Join-Path $game 'winmm.dll'))) 'NH-05-CONFLICT-RESTORE: changed loader preserved separately before original absence restored'
 Assert ((Test-Path (Join-Path $game 'nvngx_dlssnr.dll')) -and (Test-Path (Join-Path $game 'private.txt'))) 'NH-05-UNINSTALL-MODES: private/unknown files preserved'
 Assert (Test-Path (Join-Path $game 'NeuRotic/UserData/OptiScaler.ini')) 'NH-05-UNINSTALL-MODES: settings retained'
}finally{End-HubFixtures}
