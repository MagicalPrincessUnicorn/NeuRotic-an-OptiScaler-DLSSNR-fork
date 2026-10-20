. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
function Recovery-Request($game,[string]$operation='Update'){
 return [ordered]@{kind='Plan';gameExecutable=(Join-Path $game 'FixtureGame.exe');operation=$operation;packageId='flagship-approved';continueWithoutRuntime=$true;adoptModifiedInstall=$true;existingProxyAction='Replace'}
}
function Snapshot($game){
 return (@(Get-ChildItem -LiteralPath $game -File -Recurse | Sort-Object FullName | ForEach-Object {$_.FullName+' '+(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}) -join "`n")
}
try{
 $game=New-GameFixture 'conflict-recovery'
 Assert ((Execute-Plan (Plan-Game $game 'Install' 'winmm.dll')).status -eq 'Succeeded') 'CONFLICT setup installs approved package'
 $statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json'
 $oldState=Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
 $oldRecords=@($oldState.restore_chain | ForEach-Object {[pscustomobject]@{path=$_;hash=(Get-FileHash -LiteralPath $_).Hash}})
 $manual='Manually updated DLL to preserve as recovery baseline'
 [IO.File]::WriteAllText((Join-Path $game 'winmm.dll'),$manual)
 [IO.File]::AppendAllText((Join-Path $game 'OptiScaler.ini'),"`r`n; user conflict recovery settings`r`n")
 $iniHash=(Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash
 $modelHash=(Get-FileHash (Join-Path $game 'nvngx_dlssnr.dll')).Hash
 $before=Snapshot $game
 $refusal=Plan-Game $game 'Update' 'winmm.dll'
 Assert ($refusal.status -eq 'NeedsDecision' -and $refusal.target -eq (Join-Path $game 'FixtureGame.exe') -and $refusal.recordedProxy -eq 'winmm.dll' -and $refusal.conflicts -contains 'winmm.dll' -and $refusal.reason -match 'review') 'CONFLICT ordinary update exposes target, recorded proxy, conflicts and review guidance'
 Assert ((Snapshot $game) -ceq $before) 'CONFLICT ordinary refusal changes no game bytes'
 foreach($operation in @('Install','ChangeProxy','Uninstall','SaveSettings','Restore')){
  $result=HubCall (Recovery-Request $game $operation)
  Assert ($result.status -eq 'FailedWithoutMutation') ('CONFLICT adoption rejected for '+$operation)
 }
 foreach($value in @('true',1,$null)){
  $request=Recovery-Request $game;$request.adoptModifiedInstall=$value
  Assert ((HubCall $request).status -eq 'FailedWithoutMutation') 'CONFLICT adoption Boolean type enforced'
 }
 foreach($kind in @('Inspect','Execute')){
  $request=Recovery-Request $game;$request.kind=$kind
  Assert ((HubCall $request).status -eq 'FailedWithoutMutation') ('CONFLICT adoption rejected for request kind '+$kind)
 }
 foreach($action in @('Cancel','RenameReShade','')){
  $request=Recovery-Request $game;$request.existingProxyAction=$action
  Assert ((HubCall $request).status -eq 'FailedWithoutMutation') ('CONFLICT explicit Replace required: '+$action)
 }
 $request=Recovery-Request $game;$request.existingDxgiAction='Delete'
 Assert ((HubCall $request).status -eq 'FailedWithoutMutation') 'CONFLICT legacy dxgi deletion combination rejected'
 $request=Recovery-Request $game;$request.proxyName='dxgi.dll'
 Assert ((HubCall $request).status -eq 'FailedWithoutMutation') 'CONFLICT recorded proxy mismatch rejected'
 $unmanaged=New-GameFixture 'unmanaged-conflict'
 Assert ((HubCall (Recovery-Request $unmanaged)).status -eq 'FailedWithoutMutation') 'CONFLICT active verified managed receipt required'
 Assert ((Snapshot $game) -ceq $before) 'CONFLICT invalid recovery choices change no game bytes'
 $plan=HubCall (Recovery-Request $game)
 Assert ($plan.status -eq 'Planned' -and $plan.request.proxyName -eq 'winmm.dll') 'CONFLICT explicit review defaults to recorded proxy'
 Assert ($plan.recovery.adoptModifiedInstall -eq $true -and $plan.recovery.conflicts -contains 'winmm.dll' -and ($plan.changes.operation -join ' ') -match 'new recovery baseline') 'CONFLICT review explains approved replacement and recovery baseline'
 Assert ((Snapshot $game) -ceq $before) 'CONFLICT recovery Plan is read only'
 [IO.File]::WriteAllText((Join-Path $game 'winmm.dll'),'changed after review')
 Assert ((Execute-Plan $plan).status -eq 'PreconditionChanged') 'CONFLICT stale review rejected'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'winmm.dll')) -eq 'changed after review') 'CONFLICT stale review never overwrites current DLL'
 [IO.File]::WriteAllText((Join-Path $game 'winmm.dll'),$manual)
 # A second review also exercises Repair admission without executing its plan.
 $repair=HubCall (Recovery-Request $game 'Repair')
 Assert ($repair.status -eq 'Planned') 'CONFLICT explicit Repair review supported'
 $plan=HubCall (Recovery-Request $game)
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded') ('CONFLICT reviewed Update completed: '+$result.reason)
 $package=Join-Path $script:Repo 'builds/hub/bin/packages/current/payload/OptiScaler.dll'
 Assert ((Get-FileHash (Join-Path $game 'winmm.dll')).Hash -eq (Get-FileHash $package).Hash) 'CONFLICT exact approved replacement installed'
 Assert ((Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash -eq $iniHash -and (Get-FileHash (Join-Path $game 'nvngx_dlssnr.dll')).Hash -eq $modelHash) 'CONFLICT current settings and private model preserved'
 $adopted=Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
 Assert ($adopted.adopted_modified_install -eq $true -and @($adopted.restore_chain).Count -eq 1) 'CONFLICT current files became new active restore baseline'
 foreach($record in $oldRecords){Assert ((Get-FileHash -LiteralPath $record.path).Hash -eq $record.hash) 'CONFLICT historical restore record untouched'}
 $newRecord=Get-Content -Raw -LiteralPath $adopted.restore_chain[0] | ConvertFrom-Json
 $newBackup=Join-Path (Split-Path -Parent $adopted.restore_chain[0]) 'previous/winmm.dll'
 Assert ([IO.File]::ReadAllText($newBackup) -eq $manual) 'CONFLICT manual DLL preserved in verified recovery backup'
 Assert ((Execute-Plan (Plan-Game $game 'Uninstall' 'winmm.dll')).status -eq 'Succeeded') 'CONFLICT uninstall completes new restore chain'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'winmm.dll')) -eq $manual) 'CONFLICT uninstall restores manually updated DLL baseline'
 Assert ((Get-FileHash (Join-Path $game 'OptiScaler.ini')).Hash -eq $iniHash -and (Get-FileHash (Join-Path $game 'nvngx_dlssnr.dll')).Hash -eq $modelHash) 'CONFLICT uninstall preserves settings and private model'
 foreach($record in $oldRecords){Assert ((Get-FileHash -LiteralPath $record.path).Hash -eq $record.hash) 'CONFLICT uninstall retains historical restore record'}
}finally{End-HubFixtures}
