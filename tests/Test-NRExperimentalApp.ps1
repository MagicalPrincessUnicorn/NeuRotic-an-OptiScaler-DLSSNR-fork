param([Parameter(Mandatory=$true)][string]$AppRoot,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$AppRoot=[IO.Path]::GetFullPath($AppRoot);$EvidenceRoot=[IO.Path]::GetFullPath($EvidenceRoot)
$repo=Split-Path $PSScriptRoot
$work=Join-Path $repo 'builds/experimental-app-fixture'
if(Test-Path -LiteralPath $work){throw 'Fixture already exists; inspect ownership before reuse.'}
New-Item -ItemType Directory -Path $work|Out-Null
. (Join-Path $PSScriptRoot 'hub/SimpleInstallerFixtureCommon.ps1')
$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user-data'
New-Item -ItemType Directory -Path $env:NEUROTIC_HUB_FIXTURE_ROOT|Out-Null
$support=Join-Path $AppRoot 'support'
$checks=New-Object 'Collections.Generic.List[string]'
function Check($ok,[string]$message){if(-not $ok){throw $message};$checks.Add($message);Write-Output "PASS $message"}
function Request($request,[string]$approval=''){
 $request.protocolVersion=1
 $inputPath=Join-Path $work 'request.json';$outputPath=Join-Path $work 'result.json'
 [IO.File]::WriteAllText($inputPath,($request|ConvertTo-Json -Depth 12))
 if(Test-Path -LiteralPath $outputPath){Remove-Item -LiteralPath $outputPath}
 $start=New-Object Diagnostics.ProcessStartInfo
 $start.FileName=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.EnvironmentVariables['PSModulePath']=Join-Path (Split-Path $start.FileName) 'Modules'
 $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $support 'NeuRotic-Setup-Engine.ps1')+'" -HubNonInteractive -HubRequestPath "'+$inputPath+'" -HubResultPath "'+$outputPath+'"'
 $child=[Diagnostics.Process]::Start($start)
 try{$stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$child.StandardInput.Write($approval);$child.StandardInput.Close();$child.WaitForExit();[IO.File]::WriteAllText((Join-Path $work 'engine.log'),$stdout.Result+$stderr.Result)}finally{$child.Dispose()}
 if(-not(Test-Path -LiteralPath $outputPath)){throw ('No result: '+[IO.File]::ReadAllText((Join-Path $work 'engine.log')))}
 return Get-Content -LiteralPath $outputPath -Raw|ConvertFrom-Json
}
function Execute($plan){
 if($plan.status -ne 'Planned'){throw ($plan|ConvertTo-Json -Depth 8)}
 $approval=''
 if($plan.antiCheat -and $plan.antiCheat.acknowledgementRequired){$approval="NRAC1`n"+$plan.planId+"`n"+$plan.planFingerprint+"`n"+$plan.antiCheat.fingerprint+"`n"}
 return Request ([ordered]@{kind='Execute';planId=$plan.planId;planFingerprint=$plan.planFingerprint}) $approval
}
try{
 $game=Join-Path $work 'game with spaces';New-Item -ItemType Directory -Path $game|Out-Null
 $exe=Join-Path $game 'FixtureGame.exe';Write-SimplePe $exe 64
 . (Join-Path $support 'NeuRotic-InstallCore.ps1')
 $packageRoot=Join-Path $AppRoot 'packages/current'
 $realManifest=Get-Content (Join-Path $packageRoot 'support/PACKAGE-MANIFEST.json') -Raw|ConvertFrom-Json
 $profile=[pscustomobject]@{proxy='dxgi.dll';configurationPaths=@('OptiScaler.ini')}
 foreach($case in @('mainline','wrong-hash','other-addon')){
  $manifest=$realManifest|ConvertTo-Json -Depth 30|ConvertFrom-Json
  $addon=@($manifest.files|Where-Object{$_.path -ceq 'payload/NeuRotic-PreparedGuides.addon64'})[0]
  if($case -eq 'mainline'){$manifest.experimental_app.channel='Mainline'}
  if($case -eq 'wrong-hash'){$addon.sha256='0'*64}
  if($case -eq 'other-addon'){$addon.path='payload/Other.addon64'}
  $package=[pscustomobject]@{root=$packageRoot;manifest=$manifest}
  $refused=$false;try{Get-NeuroticInstallActions $game $package $profile $false $null $null|Out-Null}catch{$refused=$_.Exception.Message -match 'Retired prepared/ReShade'}
  Check $refused ('Addon admission refuses '+$case)
 }
 Check (-not(Test-Path (Join-Path $game 'dxgi.dll'))) 'Rejected addon plans do not write game files'
 # Reuse only the versioned inspector directory as the real installer allows;
 # do not replicate the large Python/model dependency tree for this fixture.
 New-Item -ItemType Directory -Path (Join-Path $game 'OptiScaler/CharacterInspector/Version 1') -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $game 'foreign-mod.txt'),'keep foreign bytes')
 $request=[ordered]@{kind='Plan';operation='Install';gameExecutable=$exe;packageId='flagship-approved';proxyName='dxgi.dll';continueWithoutRuntime=$true}
 $plan=Request $request;$result=Execute $plan
 Check ($result.status -eq 'Succeeded' -or $result.status -eq 'SucceededWithNotes') 'Fresh app-protocol install succeeds'
 $payload=Join-Path $AppRoot 'packages/current/payload'
 Check ((Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq (Get-FileHash (Join-Path $payload 'OptiScaler.dll')).Hash) 'App installs exact Experimental runtime'
 Check ((Get-FileHash (Join-Path $game 'NeuRotic-PreparedGuides.addon64')).Hash -eq (Get-FileHash (Join-Path $payload 'NeuRotic-PreparedGuides.addon64')).Hash) 'Matching prepared-input addon installed'
 $ini=Join-Path $game 'OptiScaler.ini';$saved="; user configuration`r`n[DlssNr]`r`nEnabled=true`r`nRoute=1`r`nModelStrength=0.75`r`n";[IO.File]::WriteAllText($ini,$saved)
 $model=Join-Path $game 'nvngx_dlssnr.dll';[IO.File]::WriteAllText($model,'user model fixture - do not replace')
 # Emulate an earlier standalone test overlay on an app-managed installation.
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $zip=[IO.Compression.ZipFile]::OpenRead((Join-Path $repo 'builds/NeuRotic-NRCaptureSettings-083a6c62-Setup.zip'))
 try{$entry=$zip.GetEntry('NeuRotic-NRAdvanced/payload/OptiScaler.dll');$source=$entry.Open();$dest=[IO.File]::Create((Join-Path $game 'dxgi.dll'));try{$source.CopyTo($dest)}finally{$dest.Dispose();$source.Dispose()}}finally{$zip.Dispose()}
 $oldFolder=Join-Path $game 'NeuRotic-test-backups/nr-advanced-fixture';New-Item -ItemType Directory -Path $oldFolder -Force|Out-Null
 $oldRecord=Join-Path $oldFolder 'INSTALL-MANIFEST.json'
 [IO.File]::WriteAllText($oldRecord,(@{kind='neurotic-nr-advanced-update';status='installed';game_executable=$exe;product_commit='083a6c6249236d19ae04486b344a97a047ba5263'}|ConvertTo-Json))
 $recordHash=(Get-FileHash $oldRecord).Hash
 $plan=Request $request;$result=Execute $plan
 Check ($result.status -eq 'Succeeded' -or $result.status -eq 'SucceededWithNotes') 'Earlier test overlay updates directly through app; no Restore required'
 Check ((Get-FileHash (Join-Path $game 'dxgi.dll')).Hash -eq (Get-FileHash (Join-Path $payload 'OptiScaler.dll')).Hash) 'Earlier overlay replaced by exact new runtime'
 Check ([IO.File]::ReadAllText($ini) -ceq $saved) 'Existing settings preserved byte for byte'
 Check ([IO.File]::ReadAllText($model) -ceq 'user model fixture - do not replace') 'Private model preserved without runtime deposit'
 Check ((Get-FileHash $oldRecord).Hash -eq $recordHash) 'Old test backup metadata preserved'
 $plan=Request $request;$result=Execute $plan
 Check ($result.status -eq 'Succeeded' -or $result.status -eq 'SucceededWithNotes') 'Repeat app update succeeds without uninstall or restore'
 $plan=Request ([ordered]@{kind='Plan';operation='Uninstall';gameExecutable=$exe});$result=Execute $plan
 Check ($result.status -eq 'Succeeded' -or $result.status -eq 'SucceededWithNotes') 'App removal succeeds after test-overlay migration'
 Check (-not(Test-Path -LiteralPath (Join-Path $game 'dxgi.dll'))) 'App removal removes owned proxy'
 Check ([IO.File]::ReadAllText((Join-Path $game 'foreign-mod.txt')) -ceq 'keep foreign bytes') 'Unrelated game files preserved'
 Check ((Get-FileHash $oldRecord).Hash -eq $recordHash) 'App removal preserves old test backups'
 $x86=Join-Path $work 'x86';New-Item -ItemType Directory -Path $x86|Out-Null;Write-SimplePe (Join-Path $x86 'FixtureGame.exe') 32
 $request.gameExecutable=Join-Path $x86 'FixtureGame.exe';$result=Request $request
 Check ($result.status -eq 'FailedWithoutChanges' -and -not(Test-Path -LiteralPath (Join-Path $x86 'dxgi.dll'))) 'Incompatible x86 target refused before writing a proxy'
 [IO.File]::WriteAllText((Join-Path $EvidenceRoot 'installer-checks.json'),(@{checks=@($checks);gameExecuted=$false;appProtocol=$true}|ConvertTo-Json -Depth 5))
}finally{Remove-Item Env:NEUROTIC_HUB_FIXTURE_ROOT -ErrorAction SilentlyContinue}
