$ErrorActionPreference='Stop'
$script:Repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:Work=Join-Path $script:Repo 'builds/hub/fixtures'
$script:Support=Join-Path $script:Repo 'builds/hub/bin/support'
$script:PowerShell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
function Assert($condition,$message){if(-not $condition){throw $message};Write-Output "PASS $message"}
function Init-HubFixtures {
 if(Test-Path -LiteralPath $script:Work){throw 'Previous Hub fixtures remain; inspect before reuse'}
 New-Item -ItemType Directory -Path $script:Work -Force|Out-Null
 $env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $script:Work 'user-data';New-Item -ItemType Directory -Path $env:NEUROTIC_HUB_FIXTURE_ROOT|Out-Null
 # Exercise this checkout's small native installer authority. Never copy a
 # delivered runtime/Inspector cohort into every installer test game.
 $script:Support=Join-Path $script:Work 'app/support';New-Item -ItemType Directory -Path $script:Support -Force|Out-Null
 foreach($file in Get-ChildItem -LiteralPath (Join-Path $script:Repo 'tools/packaging/customer/support') -File){if($file.Extension -in @('.ps1','.cs','.json','.cmd','.txt')){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $script:Support $file.Name)}}
 . (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1');$script:SimpleWork=$script:Work
 $package=New-SimplePackageFixture;New-Item -ItemType Directory -Path (Join-Path $package.root 'support')|Out-Null
 foreach($file in $package.manifest.files){$info=Get-Item -LiteralPath (Join-Path $package.root $file.path);$file|Add-Member -NotePropertyName bytes -NotePropertyValue $info.Length;$file|Add-Member -NotePropertyName sha256 -NotePropertyValue (Get-FileHash -LiteralPath $info.FullName).Hash}
 $manifest=Join-Path $package.root 'support/PACKAGE-MANIFEST.json';[IO.File]::WriteAllText($manifest,($package.manifest|ConvertTo-Json -Depth 10))
 [IO.File]::WriteAllText((Join-Path $script:Support 'Hub-Packages.json'),([ordered]@{schemaVersion=1;packages=@([ordered]@{id='flagship-approved';relativeRoot='../../package';manifestSha256=(Get-FileHash -LiteralPath $manifest).Hash})}|ConvertTo-Json -Depth 5))
}
function New-GameFixture([string]$Name){
 $game=Join-Path $script:Work $Name;New-Item -ItemType Directory -Path $game|Out-Null
 Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/cmd.exe') -Destination (Join-Path $game 'FixtureGame.exe')
 [IO.File]::WriteAllText((Join-Path $game 'private.txt'),'untouched')
 [IO.File]::WriteAllText((Join-Path $game 'nvngx_dlssnr.dll'),'private model fixture')
 return $game
}
function HubCall($request,[string]$LocalApproval=$null){
 $id=[Guid]::NewGuid().ToString('N');$input=Join-Path $script:Work ($id+'.request.json');$output=Join-Path $script:Work ($id+'.result.json')
 if(-not $request.Contains('protocolVersion')){$request.protocolVersion=1}
 [IO.File]::WriteAllText($input,($request|ConvertTo-Json -Depth 12),(New-Object Text.UTF8Encoding($false)))
 $log=Join-Path $script:Work 'engine.log'
 if($LocalApproval){
  # Emulate the Hub's private inherited pipe. Approval is never request JSON.
  $start=New-Object Diagnostics.ProcessStartInfo
  $start.FileName=$script:PowerShell;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
  # Match the native Hub owner rather than inheriting a PowerShell 7 module path.
  $start.EnvironmentVariables['PSModulePath']=Join-Path (Split-Path -Parent $script:PowerShell) 'Modules'
  $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
  $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $script:Support 'NeuRotic-Setup-Engine.ps1')+'" -HubNonInteractive -HubRequestPath "'+$input+'" -HubResultPath "'+$output+'"'
  $child=[Diagnostics.Process]::Start($start)
  try{$stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$child.StandardInput.Write($LocalApproval);$child.StandardInput.Close();$child.WaitForExit();[IO.File]::WriteAllText($log,$stdout.Result+$stderr.Result)}finally{$child.Dispose()}
 }else{& $script:PowerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $script:Support 'NeuRotic-Setup-Engine.ps1') -HubNonInteractive -HubRequestPath $input -HubResultPath $output *> $log}
 if(-not (Test-Path -LiteralPath $output)){throw ('No typed result: '+(Get-Content -Raw $log))}
 $result=Get-Content -Raw -LiteralPath $output|ConvertFrom-Json
 if($result.diagnostic){Write-Output $result.diagnostic | Out-Host}
 if($result.status -in @('FailedWithoutMutation','FailedWithChanges','FailedRolledBack','RecoveryRequired')){Write-Output ($result.status+': '+$result.reason) | Out-Host}
 return $result
}
function Plan-Game([string]$Game,[string]$Operation,[string]$Proxy='dxgi.dll'){
 $request=[ordered]@{kind='Plan';gameExecutable=(Join-Path $Game 'FixtureGame.exe');operation=$Operation}
 if($Operation -eq 'Install'){$request.packageId='flagship-approved';$request.proxyName=$Proxy;$request.continueWithoutRuntime=$true}
 return HubCall $request
}
function Execute-Plan($plan){if($plan.status -ne 'Planned'){throw ('Plan refused: '+($plan|ConvertTo-Json -Depth 4))};$approval=$null;if($plan.antiCheat -and $plan.antiCheat.acknowledgementRequired){$approval="NRAC1`n"+$plan.planId+"`n"+$plan.planFingerprint+"`n"+$plan.antiCheat.fingerprint+"`n"};return HubCall ([ordered]@{kind='Execute';planId=$plan.planId;planFingerprint=$plan.planFingerprint}) $approval}
function End-HubFixtures {
 $resolved=[IO.Path]::GetFullPath($script:Work)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $script:Repo 'builds/hub/fixtures'))){throw 'Unexpected fixture cleanup root'}
 foreach($entry in Get-ChildItem -LiteralPath $resolved -Recurse -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'}}
 Remove-Item -LiteralPath $resolved -Recurse -Force
 Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT
}
