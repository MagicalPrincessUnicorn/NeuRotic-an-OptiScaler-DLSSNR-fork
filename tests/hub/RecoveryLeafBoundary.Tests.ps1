. (Join-Path $PSScriptRoot 'FixtureCommon.ps1')
Init-HubFixtures
try{
 $engine=Join-Path $script:Repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1';$tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
 foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
 $allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
 $originalCopy=(Get-Command CopyVerified).ScriptBlock
 $game=New-GameFixture 'recovery-leaf';$path=Join-Path $game 'OptiScaler.ini';[IO.File]::WriteAllText($path,'original settings')
 $journal=New-UninstallTransaction $game @() @('OptiScaler.ini')
 [IO.File]::WriteAllText($path,'interrupted transaction settings')
 $script:InjectPath=$path;$script:Injected=$false
 function CopyVerified([string]$Source,[string]$Destination,[string]$Hash,[string]$ExpectedCurrentHash){
  if($Destination -eq $script:InjectPath -and -not $script:Injected){
   if(-not $ExpectedCurrentHash){throw 'Recovery copy is not bound to its observed precondition'}
   [IO.File]::WriteAllText($Destination,'outside edit at recovery open');$script:Injected=$true;$script:OutsideHash=HashFile $Destination
  }
  & $originalCopy $Source $Destination $Hash $ExpectedCurrentHash
 }
 $refused=$false;try{Restore-UninstallTransaction $game $journal}catch{$refused=$true}
 Assert ($refused -and $script:Injected -and (HashFile $path) -eq $script:OutsideHash) 'NH-05-RECOVERY-LEAF: snapshot restore refuses replacement at mutation open'
 Set-Item Function:CopyVerified $originalCopy
 $game=New-GameFixture 'restore-record-leaf';$path=Join-Path $game 'OptiScaler.ini';[IO.File]::WriteAllText($path,"[Menu]`r`nOverlayMenu=false`r`n")
 $result=Execute-Plan (Plan-Game $game 'Install');Assert ($result.status -eq 'Succeeded') 'NH-05-RECOVERY-LEAF: restore chain setup'
 $state=Get-Content -Raw (Join-Path $game 'NeuRotic/Installer/Current-Install.json')|ConvertFrom-Json
 [IO.File]::WriteAllText($path,'settings during restore')
 $script:InjectPath=$path;$script:Injected=$false
 function CopyVerified([string]$Source,[string]$Destination,[string]$Hash,[string]$ExpectedCurrentHash){
  if($Destination -eq $script:InjectPath -and -not $script:Injected){
   if(-not $ExpectedCurrentHash){throw 'Restore copy is not bound to its saved live preimage'}
   [IO.File]::WriteAllText($Destination,'outside edit during record restore');$script:Injected=$true;$script:OutsideHash=HashFile $Destination
  }
  & $originalCopy $Source $Destination $Hash $ExpectedCurrentHash
 }
 $CheckOnly=$false;$refused=$false
 try{[void](Invoke-RestoreRecord $state.restore_chain[-1])}catch{$refused=$true;Write-Output $_.Exception.Message|Out-Host}
 Assert ($refused -and $script:Injected -and (HashFile $path) -eq $script:OutsideHash) 'NH-05-RECOVERY-LEAF: record restore and its rollback preserve outside edit'
}finally{End-HubFixtures}
