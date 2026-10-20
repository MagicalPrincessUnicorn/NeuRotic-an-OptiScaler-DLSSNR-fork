param([string]$SupportRoot='')
. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
if($SupportRoot){$script:Support=(Resolve-Path -LiteralPath $SupportRoot).Path}
Init-HubFixtures
try{
 $game=New-GameFixture 'portal-x86'
 Copy-Item -LiteralPath "$env:SystemRoot/SysWOW64/cmd.exe" -Destination (Join-Path $game 'FixtureGame.exe') -Force
 $before=(Get-FileHash (Join-Path $game 'FixtureGame.exe')).Hash
 $inspection=HubCall ([ordered]@{kind='Inspect';gameExecutable=(Join-Path $game 'FixtureGame.exe')})
 Assert ($inspection.status -eq 'Inspected' -and $inspection.inspection.target.bitness -eq 32) 'typed Hub Inspect accepts actual32 executable'
 $plan=Plan-Game $game 'Install'
 Assert ($plan.status -eq 'Planned' -and $plan.request.packageId -eq 'prepared-x86' -and $plan.target.bitness -eq 32) 'Hub Plan selects prepared32 package rather than x64 proxy'
 Assert (-not (Test-Path (Join-Path $game 'dxgi.dll'))) 'Hub Plan changes no game files'
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded' -and $result.inspection.state.status -eq 'installed-verified') 'reviewed Hub Execute verifies real x86 install'
 Assert (-not (Test-Path (Join-Path $game 'OptiScaler.dll'))) 'Hub installs no root x64 core into32 game'
 $ini=Join-Path $game 'NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini'
 $plan=HubCall ([ordered]@{kind='Plan';operation='SaveSettings';gameExecutable=(Join-Path $game 'FixtureGame.exe');configRevision=$result.inspection.configRevision;settings=@([ordered]@{section='DlssNr';key='Enabled';value='false'})})
 Assert ($plan.status -eq 'Planned') 'Hub reviews renderer helper setting edit'
 Assert (@($plan.changes | Where-Object {$_.path -eq 'NeuRotic\Prepared\NeuRotic.GpuHost\OptiScaler.ini'}).Count -eq 1) 'Settings preview names actual helper config destination'
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded' -and (Get-Content -Raw $ini) -match 'Enabled=false') 'Hub saves settings to actual64 renderer helper'
 Assert (-not (Test-Path (Join-Path $game 'OptiScaler.ini'))) 'Hub settings do not create unrelated root config'
 $defaults=HubCall ([ordered]@{kind='RestoreDefaults';gameExecutable=(Join-Path $game 'FixtureGame.exe');configRevision=(Get-FileHash -LiteralPath $ini).Hash})
 Assert ($defaults.status -eq 'DefaultsResolved' -and $defaults.defaultsSource.packageId -eq 'prepared-x86' -and @($defaults.defaultSettings|Where-Object{$_.section -eq 'DlssNr' -and $_.key -eq 'Enabled' -and $_.value -eq 'true'}).Count -eq 1) 'Hub restores defaults from actual prepared helper package'
 Add-Content -LiteralPath (Join-Path $game 'ReShade.ini') -Value "`r`n[UserCustom]`r`nOverlayKey=36`r`n"
 $inspected=HubCall ([ordered]@{kind='Inspect';gameExecutable=(Join-Path $game 'FixtureGame.exe')})
 Assert ($inspected.status -eq 'Inspected' -and -not $inspected.inspection.conflicts.Count) 'Live ReShade edits remain user settings during current-file inspection'
 $blocked=HubCall ([ordered]@{kind='Plan';operation='SaveSettings';gameExecutable=(Join-Path $game 'FixtureGame.exe');configRevision=(Get-FileHash -LiteralPath $ini).Hash;settings=@([ordered]@{section='FrameGen';key='Enabled';value='true'})})
 Assert ($blocked.status -eq 'FailedWithoutMutation' -and $blocked.reason -match 'unavailable in the 32-bit prepared renderer') 'Unsupported game FG controls cannot be saved into helper'
 $plan=Plan-Game $game 'Update';Assert ($plan.status -eq 'Planned') 'Settings edits do not falsely conflict with x86 update'
 Assert ((Execute-Plan $plan).status -eq 'Succeeded') 'Hub32 update retains saved renderer settings'
 $plan=Plan-Game $game 'Uninstall';Assert ($plan.status -eq 'Planned') 'Hub32 uninstall has reviewed plan'
 Assert ((Execute-Plan $plan).status -eq 'Succeeded') 'Hub32 uninstall completes existing managed transaction'
 Assert (-not (Test-Path (Join-Path $game 'dxgi.dll')) -and (Get-FileHash (Join-Path $game 'FixtureGame.exe')).Hash -eq $before) 'Hub32 uninstall restores absence and preserves original executable'
 Write-Output 'PASS typed Hub x86 inspect/plan/execute/settings/update/uninstall with actual architecture binaries; no game/private renderer launched'
}finally{End-HubFixtures}
