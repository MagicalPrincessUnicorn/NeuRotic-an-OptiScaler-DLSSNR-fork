. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
try {
 $engine=Join-Path $script:Repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1'
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
 foreach($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){. ([scriptblock]::Create($definition.Extent.Text))}
 . (Join-Path (Split-Path $engine) 'NeuRotic-InstallCore.ps1')
 . (Join-Path (Split-Path $engine) 'NeuRotic-HubSettings.ps1')
 $game=New-GameFixture 'settings-write-boundary';$path=Join-Path $game 'OptiScaler.ini'
 [IO.File]::WriteAllText($path,"[Menu]`r`nOverlayMenu=false`r`n")
 $revision=HashFile $path
 $original=(Get-Command WriteVerifiedBytes).ScriptBlock
 function WriteVerifiedBytes($Bytes,$Path,$NewDigest,$Revision){
  [IO.File]::AppendAllText($Path,'; outside edit at exclusive write boundary')
  $script:ExternalHash=HashFile $Path
  return & $original $Bytes $Path $NewDigest $Revision
 }
 $result=$null;try{$result=Invoke-HubSettingsSave ([pscustomobject]@{directory=$game;bitness=64}) ([pscustomobject]@{configRevision=$revision;settings=@([pscustomobject]@{section='Menu';key='OverlayMenu';value='true'})})}catch{Write-Output $_.Exception.Message|Out-Host}
 Assert ((HashFile $path) -eq $script:ExternalHash) 'NH-06-WRITE-BOUNDARY: outside edit survives exclusive write race'
 Assert ($result.status -eq 'PreconditionChanged') 'NH-06-WRITE-BOUNDARY: stale save returns typed refusal'
 Assert (-not (Test-Path (Join-Path $game 'NeuRotic/Installer/Uninstall-Transaction.json'))) 'NH-06-WRITE-BOUNDARY: unused journal removed'
}finally{End-HubFixtures}
