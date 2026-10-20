. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
try{
 foreach($proxy in @('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')){
  $game=New-GameFixture ('game-'+$proxy);$model=(Get-FileHash (Join-Path $game 'nvngx_dlssnr.dll')).Hash
  $plan=Plan-Game $game 'Install' $proxy
  Assert (-not (Test-Path (Join-Path $game $proxy))) 'NH-03-PLAN-READONLY: preview creates no loader'
  $result=Execute-Plan $plan
  Assert ($result.status -eq 'Succeeded') ("NH-04-INSTALL-ALL-PROXIES: $proxy ($($result.status): $($result.reason))")
  Assert ((Get-FileHash (Join-Path $game 'nvngx_dlssnr.dll')).Hash -eq $model) 'NH-04-INSTALL-ALL-PROXIES: private model preserved'
 }
 $game=New-GameFixture 'stale';$plan=Plan-Game $game 'Install'
 [IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'external updater')
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'PreconditionChanged') 'NH-04-TOCTOU: changed occupied file invalidates plan'
 Assert ([IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -eq 'external updater') 'NH-04-TOCTOU: foreign bytes preserved'
 $game=New-GameFixture 'tampered-plan';$plan=Plan-Game $game 'Install'
 $planPath=Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT ($plan.planId+'.plan.json')
 $saved=Get-Content -Raw $planPath|ConvertFrom-Json;$saved.request.proxyName='winmm.dll';$saved|ConvertTo-Json -Depth 12|Set-Content $planPath
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'PreconditionChanged' -and -not (Test-Path (Join-Path $game 'winmm.dll'))) 'NH-03-STALE-PLAN: edited server plan does not change reviewed operation'
}finally{End-HubFixtures}
