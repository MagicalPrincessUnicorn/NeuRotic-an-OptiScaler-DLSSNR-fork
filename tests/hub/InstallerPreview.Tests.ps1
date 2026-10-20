. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
try{
 $game=New-GameFixture 'foreign-dxgi';$dxgi=Join-Path $game 'dxgi.dll';[IO.File]::WriteAllText($dxgi,'foreign dxgi preserved')
 $hash=(Get-FileHash $dxgi).Hash;$plan=Plan-Game $game 'Install' 'winmm.dll'
 Assert ($plan.status -eq 'Planned') 'NH-03-PREVIEW: alternate proxy accepts unrelated dxgi'
 $result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded' -and (Get-FileHash $dxgi).Hash -eq $hash) 'NH-03-PREVIEW: alternate proxy preserves unrelated dxgi'
 $game=New-GameFixture 'reshade';[IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'foreign ReShade fixture')
 [IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),"[Plugins]`r`nLoadReshade=false`r`n")
 $plan=HubCall ([ordered]@{kind='Plan';operation='Install';gameExecutable=(Join-Path $game 'FixtureGame.exe');packageId='flagship-approved';proxyName='dxgi.dll';existingProxyAction='RenameReShade';continueWithoutRuntime=$true})
 Assert (@($plan.changes|Where-Object{$_.path -eq 'ReShade64.dll' -and $_.operation -eq 'rename-existing-proxy'}).Count -eq 1) 'NH-03-PREVIEW: owner lists ReShade rename'
 Assert (@($plan.changes|Where-Object{$_.path -eq 'OptiScaler.ini' -and $_.operation -eq 'write-loadreshade-true'}).Count -eq 1) 'NH-03-PREVIEW: owner lists INI chaining edit'
 Assert (-not (Test-Path (Join-Path $game 'ReShade64.dll'))) 'NH-03-PREVIEW: preview leaves ReShade unmoved'
 $game=New-GameFixture 'proxy-transition';$plan=Plan-Game $game 'Install';$result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded') 'NH-03-PREVIEW: proxy transition setup'
 $plan=Plan-Game $game 'ChangeProxy' 'winmm.dll'
 Assert (@($plan.changes|Where-Object{$_.path -eq 'dxgi.dll' -and $_.operation -eq 'restore-old-managed-proxy' -and -not $_.installed_exists}).Count -eq 1) 'NH-03-PREVIEW: owner lists old proxy removal'
}finally{End-HubFixtures}
