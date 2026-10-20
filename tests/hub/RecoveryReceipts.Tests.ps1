. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
try{
 $game=New-GameFixture 'recovery-receipts';$result=Execute-Plan (Plan-Game $game 'Install')
 Assert ($result.status -eq 'Succeeded') 'NH-05-RECOVERY: setup'
 $engine=Join-Path $script:Support 'NeuRotic-Setup-Engine.ps1';$tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
 foreach($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){. ([scriptblock]::Create($definition.Extent.Text))}
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 $chain=@($state.restore_chain);[array]::Reverse($chain)
 $journal=New-UninstallTransaction $game $chain
 # Reproduce the durable receipt state between New-UninstallCleanup and journal deletion.
 $cleanup=New-UninstallCleanup $game $chain $state $journal 'Full'
 $recordHashes=@{};foreach($record in $chain){$recordHashes[$record]=HashFile $record}
 $plan=Plan-Game $game 'Restore';$result=Execute-Plan $plan
 Assert ($result.status -eq 'Succeeded') 'NH-05-RECOVERY: dual receipt restore succeeds'
 Assert (-not (Test-Path (Join-Path $game 'NeuRotic/Installer/Cleanup-Pending.json'))) 'NH-05-RECOVERY: rollback discards stale cleanup'
 foreach($record in $chain){Assert ((HashFile $record) -eq $recordHashes[$record]) 'NH-05-RECOVERY: original restore history retained'}
 $plan=Plan-Game $game 'Restore'
 Assert ($plan.status -ne 'Planned') 'NH-05-RECOVERY: second recovery cannot run stale Full cleanup'
 $inspection=HubCall ([ordered]@{kind='Inspect';gameExecutable=(Join-Path $game 'FixtureGame.exe')})
 Assert (-not $inspection.inspection.recoveryRequired -and $inspection.inspection.state.status -eq 'installed-verified') 'NH-05-RECOVERY: fresh inspection confirms recovered installation'
}finally{End-HubFixtures}
