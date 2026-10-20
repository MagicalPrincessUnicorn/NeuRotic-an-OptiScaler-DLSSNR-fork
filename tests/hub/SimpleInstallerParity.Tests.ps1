. (Join-Path $PSScriptRoot 'SimpleInstallerFixtureCommon.ps1')
Start-SimpleFixtures
try{
 # Run the real entry point in a small source-backed application fixture.
 $app=Join-Path $script:SimpleWork 'app';$support=Join-Path $app 'support';New-Item -ItemType Directory -Path $support -Force|Out-Null
 foreach($file in Get-ChildItem -LiteralPath $script:SimpleSupport -File){if($file.Extension -in @('.ps1','.cs','.json','.txt','.cmd')){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $support $file.Name)}}
 $package=New-SimplePackageFixture;New-Item -ItemType Directory -Path (Join-Path $package.root 'support')|Out-Null
 foreach($file in $package.manifest.files){$info=Get-Item -LiteralPath (Join-Path $package.root $file.path);$file|Add-Member -NotePropertyName bytes -NotePropertyValue $info.Length;$file|Add-Member -NotePropertyName sha256 -NotePropertyValue (Get-FileHash -LiteralPath $info.FullName).Hash}
 $manifest=Join-Path $package.root 'support/PACKAGE-MANIFEST.json';[IO.File]::WriteAllText($manifest,($package.manifest|ConvertTo-Json -Depth 8))
 $registry=[ordered]@{schemaVersion=1;packages=@([ordered]@{id='flagship-approved';relativeRoot='../../package';manifestSha256=(Get-FileHash -LiteralPath $manifest).Hash})}
 [IO.File]::WriteAllText((Join-Path $support 'Hub-Packages.json'),($registry|ConvertTo-Json -Depth 5))
 $env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $script:SimpleWork 'user-data'
 $powerShell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
 function Invoke-ParityRequest($Request,[string]$Approval=''){
  $Request.protocolVersion=1;$inputPath=Join-Path $script:SimpleWork 'request.json';$outputPath=Join-Path $script:SimpleWork 'result.json'
  if(Test-Path -LiteralPath $outputPath){Remove-Item -LiteralPath $outputPath}
  [IO.File]::WriteAllText($inputPath,($Request|ConvertTo-Json -Depth 8))
  if($Approval){
   $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=$powerShell;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
   $start.EnvironmentVariables['PSModulePath']=Join-Path (Split-Path -Parent $powerShell) 'Modules'
   $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $support 'NeuRotic-Setup-Engine.ps1')+'" -HubNonInteractive -HubRequestPath "'+$inputPath+'" -HubResultPath "'+$outputPath+'"'
   $child=[Diagnostics.Process]::Start($start);try{$stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$child.StandardInput.Write($Approval);$child.StandardInput.Close();$child.WaitForExit();[IO.File]::WriteAllText((Join-Path $script:SimpleWork 'engine.log'),$stdout.Result+$stderr.Result)}finally{$child.Dispose()}
  }else{& $powerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $support 'NeuRotic-Setup-Engine.ps1') -HubNonInteractive -HubRequestPath $inputPath -HubResultPath $outputPath *> (Join-Path $script:SimpleWork 'engine.log')}
  if(-not(Test-Path -LiteralPath $outputPath)){throw ('No protocol result: '+(Get-Content -LiteralPath (Join-Path $script:SimpleWork 'engine.log') -Raw))}
  return Get-Content -LiteralPath $outputPath -Raw|ConvertFrom-Json
 }
 function Invoke-ParityPlan($Plan){$approval='';if($Plan.antiCheat -and $Plan.antiCheat.acknowledgementRequired){$approval="NRAC1`n"+$Plan.planId+"`n"+$Plan.planFingerprint+"`n"+$Plan.antiCheat.fingerprint+"`n"};return Invoke-ParityRequest ([ordered]@{kind='Execute';planId=$Plan.planId;planFingerprint=$Plan.planFingerprint}) $approval}
 $game=New-SimpleGameFixture 'app-game';$exe=Join-Path $game 'FixtureGame.exe'
 $request=[ordered]@{kind='Plan';operation='Install';gameExecutable=$exe;packageId='flagship-approved';proxyName='dxgi.dll';freshInstall=$false;continueWithoutRuntime=$true}
 $plan=Invoke-ParityRequest $request
 Assert-Simple ($plan.status -eq 'Planned' -and -not ($plan.PSObject.Properties.Name -contains 'expiresUtc')) 'freshInstall protocol1 accepted; no review expiry'
 if($plan.antiCheat.acknowledgementRequired){
  $declined=Invoke-ParityRequest ([ordered]@{kind='Execute';planId=$plan.planId;planFingerprint=$plan.planFingerprint})
  Assert-Simple ($declined.status -eq 'NeedsDecision' -and -not(Test-Path -LiteralPath (Join-Path $game 'dxgi.dll'))) 'anti-cheat acknowledgment cannot come from request fields or omission'
 }
 $result=Invoke-ParityPlan $plan
 if($result.status -notin @('Succeeded','SucceededWithNotes')){Write-Output ($result|ConvertTo-Json -Depth 6)}
 Assert-Simple ($result.status -in @('Succeeded','SucceededWithNotes') -and (Test-Path -LiteralPath (Join-Path $game 'dxgi.dll'))) 'App dispatch uses simple core'
 [IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'owned manual edit');Remove-Item -LiteralPath (Join-Path $game 'dependency.dll')
 $plan=Invoke-ParityRequest $request
 Assert-Simple ($plan.status -eq 'Planned') 'repeat Install ignores modified/missing owned files'
 $result=Invoke-ParityPlan $plan
 Assert-Simple ($result.status -in @('Succeeded','SucceededWithNotes')) 'repeat Install completes without adoption or Repair'
 foreach($operation in @('Repair','Restore','Update','ChangeProxy','Sanitize','CompactHistory','DumbfireInstall','DumbfireRollback')){
  $result=Invoke-ParityRequest ([ordered]@{kind='Plan';operation=$operation;gameExecutable=$exe})
  Assert-Simple ($result.status -eq 'UnsupportedOperation' -and $result.reason -match 'Install.*Uninstall') "retired operation $operation cannot dispatch old service"
 }
 $foreign=New-SimpleGameFixture 'app-foreign';[IO.File]::WriteAllText((Join-Path $foreign 'dxgi.dll'),'foreign mod')
 $request.gameExecutable=Join-Path $foreign 'FixtureGame.exe';$request.fileDecisions=@([ordered]@{path='dxgi.dll';action='Skip'})
 $plan=Invoke-ParityRequest $request
 Assert-Simple ($plan.status -eq 'Planned') 'three-choice decisions accepted without SHA'
 $result=Invoke-ParityPlan $plan
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and [IO.File]::ReadAllText((Join-Path $foreign 'dxgi.dll')) -eq 'foreign mod') 'App required Skip retains foreign bytes and other copies'
 $request.fileDecisions=@([ordered]@{path='dxgi.dll';action='SaveAndReplace'});$result=Invoke-ParityRequest $request
 Assert-Simple ($result.status -eq 'FailedWithoutChanges') 'Save and Replace cannot dispatch'
 $x86=New-SimpleGameFixture 'app-x86';Write-SimplePe (Join-Path $x86 'FixtureGame.exe') 32;$request.Remove('fileDecisions');$request.gameExecutable=Join-Path $x86 'FixtureGame.exe'
 $result=Invoke-ParityRequest $request
 Assert-Simple ($result.status -eq 'Planned') 'App plans user-chosen x86 without architecture refusal'
 $result=Invoke-ParityPlan $result
 Assert-Simple ($result.status -eq 'SucceededWithNotes' -and @($result.notes|Where-Object{$_.reasonCode -ceq 'desktop.installation.architecture_warning' -and $_.reason -match 'wrapper or custom setup'}).Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $x86 'dxgi.dll'))) 'App installs user-chosen x86 with non-blocking localized warning'
 $customInspect=Invoke-ParityRequest ([ordered]@{kind='Inspect';gameExecutable=$request.gameExecutable})
 Assert-Simple ($customInspect.status -eq 'Inspected' -and @($customInspect.inspection.settings).Count -gt 0) 'custom architecture inspects real root INI saved settings'
 $customSettings=[ordered]@{kind='Plan';operation='SaveSettings';gameExecutable=$request.gameExecutable;configRevision=$customInspect.inspection.configRevision;settings=@([ordered]@{section='DlssNr';key='Enabled';value='true'})}
 $customPlan=Invoke-ParityRequest $customSettings;$customResult=Invoke-ParityPlan $customPlan
 Assert-Simple ($customResult.status -eq 'Succeeded' -and [IO.File]::ReadAllText((Join-Path $x86 'OptiScaler.ini')) -match 'Enabled=true') 'custom architecture writes typed saved settings through real revision-guarded planner'
 $inspect=Invoke-ParityRequest ([ordered]@{kind='Inspect';gameExecutable=$exe})
 Assert-Simple ($inspect.status -eq 'Inspected' -and $inspect.inspection.verification -eq 'Metadata' -and $inspect.inspection.target.fileIdentity) 'selected details use bounded metadata and stat identity'
 $ini=Join-Path $game 'OptiScaler.ini'
 $settings=[ordered]@{kind='Plan';operation='SaveSettings';gameExecutable=$exe;configRevision=$inspect.inspection.configRevision;settings=@([ordered]@{section='DlssNr';key='Enabled';value='true'})}
 $plan=Invoke-ParityRequest $settings;$result=Invoke-ParityPlan $plan
 if($result.status -ne 'Succeeded'){Write-Output ($result|ConvertTo-Json -Depth 6)}
 Assert-Simple ($result.status -eq 'Succeeded' -and [IO.File]::ReadAllText($ini) -match 'Enabled=true') 'configuration edits preserve typed validation and revision guard'
 Assert-Simple (-not [IO.File]::Exists((Join-Path $game 'NeuRotic/Installer/Uninstall-Transaction.json'))) 'settings save creates no recovery transaction or backup'
 $settings.configRevision=$result.inspection.configRevision;$settings.settings[0].value='false'
 $plan=Invoke-ParityRequest $settings;[IO.File]::AppendAllText($ini,"`r`n; outside edit")
 $result=Invoke-ParityPlan $plan
 Assert-Simple ($result.status -eq 'PreconditionChanged' -and [IO.File]::ReadAllText($ini) -match 'Enabled=true') 'external configuration edit is preserved without blocking payload Install'
 $bad=Invoke-ParityRequest ([ordered]@{kind='Inspect';gameExecutable=$exe;freshInstall=$true})
 Assert-Simple ($bad.status -eq 'FailedWithoutChanges') 'action fields cannot leak into unrelated operations'
 $request.gameExecutable=$exe;$plan=Invoke-ParityRequest $request
 $planPath=Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT ($plan.planId+'.plan.json');$tampered=Get-Content -LiteralPath $planPath -Raw|ConvertFrom-Json;$tampered.packageRoot=$foreign
 [IO.File]::WriteAllText($planPath,($tampered|ConvertTo-Json -Depth 10))
 $result=Invoke-ParityPlan $plan
 Assert-Simple ($result.status -eq 'FailedWithoutChanges' -and $result.reason -match 'bound operation changed') 'private plan fingerprint binds its package root'
}finally{Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT -ErrorAction SilentlyContinue;End-SimpleFixtures}
