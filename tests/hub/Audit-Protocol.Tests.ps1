$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$root=Join-Path $repo 'builds/audit-installer-protocol'
if(Test-Path -LiteralPath $root){
 if([IO.Path]::GetFullPath($root) -ine [IO.Path]::GetFullPath((Join-Path $repo 'builds/audit-installer-protocol'))){throw 'Unexpected fixture cleanup root'}
 $queue=New-Object Collections.Generic.Queue[string];$queue.Enqueue($root)
 while($queue.Count){foreach($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'};if($item.PSIsContainer){$queue.Enqueue($item.FullName)}}}
 Remove-Item -LiteralPath $root -Recurse -Force
}
[void][IO.Directory]::CreateDirectory($root)
$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $root 'user'
$support=Join-Path $root 'app/support';$package=Join-Path $root 'package';$game=Join-Path $root 'game'
foreach($path in @($support,(Join-Path $package 'support'),$game)){[void][IO.Directory]::CreateDirectory($path)}
Copy-Item -Path (Join-Path $repo 'tools/packaging/customer/support/*') -Destination $support -Force
function Put($path,$bytes){[void][IO.Directory]::CreateDirectory((Split-Path $path));[IO.File]::WriteAllBytes($path,$bytes)}
function Json($path,$value){Put $path ([Text.Encoding]::UTF8.GetBytes(($value|ConvertTo-Json -Depth 12)))}
function Assert($ok,$reason){if(-not $ok){throw $reason};Write-Output ('PASS '+$reason)}
$deposit=Join-Path $env:NEUROTIC_HUB_FIXTURE_ROOT 'Runtime/streamline';[void][IO.Directory]::CreateDirectory($deposit)
foreach($name in @('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')){[IO.File]::Copy((Join-Path (Split-Path $repo) ('provider-baselines/20260926-canonical/'+$name)),(Join-Path $deposit $name))}
$exe=Join-Path $game 'ProtocolFixture.exe';[IO.File]::Copy("$env:windir/SysWOW64/cmd.exe",$exe,$true)
foreach($name in @('NeuRotic-Setup-Engine.ps1','NeuRotic-Prerequisites.ps1')){[IO.File]::Copy((Join-Path $support $name),(Join-Path $package ('support/'+$name)),$true)}
foreach($name in @('ReShade32.dll','NeuRotic/Prepared/NeuRotic-PreparedGuides.addon32')){Put (Join-Path $package ('payload/'+$name)) ([IO.File]::ReadAllBytes("$env:windir/SysWOW64/cmd.exe"))}
foreach($name in @('NeuRotic.PreparedGpuHost.exe','OptiScaler.dll','WinPixEventRuntime.dll','nvngx.dll_dlssnr.dll')){Put (Join-Path $package ('payload/NeuRotic/Prepared/NeuRotic.GpuHost/'+$name)) ([IO.File]::ReadAllBytes("$env:windir/System32/cmd.exe"))}
Put (Join-Path $package 'payload/NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini') ([Text.Encoding]::UTF8.GetBytes("[DlssNr]`r`nEnabled=true`r`n"))
Put (Join-Path $package 'payload/ReShade.ini') ([Text.Encoding]::UTF8.GetBytes("[GENERAL]`r`nEffectSearchPaths=.\NeuRotic\Prepared\Shaders`r`n[ADDON]`r`nAddonPath=.\NeuRotic\Prepared`r`n"))
Put (Join-Path $package 'payload/NeuRotic/Prepared/Start-PreparedGame.ps1') ([IO.File]::ReadAllBytes((Join-Path $repo 'tools/packaging/customer/prepared-guides/Start-x86-Prepared.ps1')))
Json (Join-Path $package 'support/BUILD-MANIFEST.json') @{commit=('a'*40);status='built';build=@{exit_code=0};bit_size=64;target_bit_size=32;install_route='prepared-x86'}
$manifest=Join-Path $package 'support/PACKAGE-MANIFEST.json'
Json $manifest @{kind='neurotic-customer-candidate';name='prepared audit fixture';commit=('a'*40);files=@(Get-ChildItem -LiteralPath $package -Recurse -File|Where-Object{$_.FullName -ne $manifest}|ForEach-Object{@{path=$_.FullName.Substring($package.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})}
Json (Join-Path $support 'Hub-Packages.json') @{packages=@(@{id='prepared-x86';relativeRoot='../../package';manifestSha256=(Get-FileHash -LiteralPath $manifest).Hash})}
function Call($request,[string]$approval=''){
 $id=[Guid]::NewGuid().ToString('N');$input=Join-Path $root ($id+'.request.json');$result=Join-Path $root ($id+'.result.json');$request.protocolVersion=1;Json $input $request
 $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName="$env:windir/System32/WindowsPowerShell/v1.0/powershell.exe";$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $support 'NeuRotic-Setup-Engine.ps1')+'" -HubNonInteractive -HubRequestPath "'+$input+'" -HubResultPath "'+$result+'"'
 $child=[Diagnostics.Process]::Start($start);try{$o=$child.StandardOutput.ReadToEndAsync();$e=$child.StandardError.ReadToEndAsync();$child.StandardInput.Write($approval);$child.StandardInput.Close();if(-not $child.WaitForExit(30000)){$child.Kill();throw 'Protocol child timeout'};[IO.File]::WriteAllText((Join-Path $root 'engine.log'),$o.Result+$e.Result)}finally{$child.Dispose()}
 if(-not(Test-Path -LiteralPath $result)){throw 'Typed protocol result was not produced'}
 return Get-Content -LiteralPath $result -Raw|ConvertFrom-Json
}
$plan=Call @{kind='Plan';operation='Install';gameExecutable=$exe;packageId='flagship-approved';proxyName='dxgi.dll';continueWithoutRuntime=$true;includeInspector=$false}
Assert ($plan.status -eq 'Planned') ('Prepared plan succeeds: '+$plan.reason)
Assert ($plan.requestedPackageId -eq 'flagship-approved' -and $plan.resolvedPackage.id -eq 'prepared-x86' -and $plan.resolvedPackage.architecture -eq 32 -and $plan.resolvedPackage.digest -eq $plan.packageDigest) 'Requested intent remains separate from resolved x86 package'
$approval='';if($plan.antiCheat.acknowledgementRequired){$approval="NRAC1`n"+$plan.planId+"`n"+$plan.planFingerprint+"`n"+$plan.antiCheat.fingerprint+"`n"}
$installed=Call @{kind='Execute';planId=$plan.planId;planFingerprint=$plan.planFingerprint} $approval
Assert ($installed.status -eq 'Succeeded') ('Typed prepared install commits: '+$installed.reason)
$repair=Call @{kind='Plan';operation='Repair';gameExecutable=$exe;packageId='flagship-approved';proxyName='dxgi.dll';continueWithoutRuntime=$true;includeInspector=$false}
Assert ($repair.status -eq 'Planned') ('Typed Repair plans after committed install: '+$repair.reason)
$deposited=Join-Path $deposit 'sl.common.dll';$depositedBytes=[IO.File]::ReadAllBytes($deposited);Put $deposited ([Text.Encoding]::ASCII.GetBytes('changed deposit'))
$approval='';if($repair.antiCheat.acknowledgementRequired){$approval="NRAC1`n"+$repair.planId+"`n"+$repair.planFingerprint+"`n"+$repair.antiCheat.fingerprint+"`n"}
$changed=Call @{kind='Execute';planId=$repair.planId;planFingerprint=$repair.planFingerprint} $approval
Assert ($changed.status -eq 'FailedWithoutMutation' -and $changed.reason -like '*Runtime deposit changed*') 'Reviewed runtime identity refuses changed deposits before game mutation'
Put $deposited $depositedBytes
$refresh=Call @{kind='Inspect';gameExecutable=$exe;verifyFiles=$true}
Assert ($refresh.status -eq 'Inspected' -and $refresh.inspection.verification -eq 'Checked' -and $refresh.antiCheat) 'Explicit Refresh verifies installed files and refreshes anti-cheat status'
Assert ((Get-FileHash -LiteralPath (Join-Path $game 'NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler/streamline/sl.common.dll')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $deposit 'sl.common.dll')).Hash) 'Prepared dependencies reach the GPU host consumer directory'
$launch=Call @{kind='LaunchPreflight';gameExecutable=$exe}
Assert ($launch.status -eq 'LaunchReady' -and $launch.cohort.Count -eq 14 -and $launch.launcherPath.StartsWith($package)) 'Launch admission verifies core and deposited dependency cohort and selects trusted package launcher'
$helper=Join-Path $game 'NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.dll';$original=[IO.File]::ReadAllBytes($helper);Put $helper ([Text.Encoding]::ASCII.GetBytes('outside edit'))
$launch=Call @{kind='LaunchPreflight';gameExecutable=$exe};Assert ($launch.status -eq 'FailedWithoutMutation') 'Changed prepared helper refuses launch'
Put $helper $original
$pending=Join-Path $game 'NeuRotic/Installer/Install-Transaction.json';Json $pending @{}
$launch=Call @{kind='LaunchPreflight';gameExecutable=$exe};Assert ($launch.status -eq 'RecoveryRequired') 'Pending mutation refuses launch'
$statePath=Join-Path $game 'NeuRotic/Installer/Current-Install.json';$stateBytes=[IO.File]::ReadAllBytes($statePath);Put $statePath ([Text.Encoding]::UTF8.GetBytes('{interrupted legacy metadata'))
$inspection=Call @{kind='Inspect';gameExecutable=$exe}
Assert ($inspection.status -eq 'Inspected' -and $inspection.inspection.recoveryRequired) 'Pending recovery stays discoverable when ordinary metadata cannot be parsed'
Put $statePath $stateBytes
[IO.File]::Delete($pending)
