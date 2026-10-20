param([Parameter(Mandatory=$true)][string]$Bundle,[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
. (Join-Path $Bundle 'support/NRAdvanced-Harness.ps1')
$root=Split-Path $PSScriptRoot
$work=Join-Path $root 'builds/nr-advanced-harness-fixture'
$dir=Join-Path $work 'locked-write'
if(Test-Path -LiteralPath $dir){throw 'Rollback fixture must be absent before testing.'}
New-Item -ItemType Directory -Path $dir|Out-Null
$exe=Join-Path $dir 'InertGame.exe'
Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination $exe
Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination (Join-Path $dir 'nvngx_dlssnr.dll')
Copy-Item -LiteralPath (Join-Path $work 'forwarder.dll') -Destination (Join-Path $dir 'nvngx.dll_dlssnr.dll')
$proxy=Join-Path $dir 'dxgi.dll'
Copy-Item -LiteralPath (Join-Path $Bundle 'payload/OptiScaler.dll') -Destination $proxy
$stream=[IO.File]::Open($proxy,[IO.FileMode]::Append);try{$stream.WriteByte(37)}finally{$stream.Dispose()}
$ini=Join-Path $dir 'OptiScaler.ini';[IO.File]::WriteAllText($ini,"[DlssNr]`r`nEnabled=false`r`nRoute=0`r`nStrength=0.47`r`n")
$old=Get-NraHash $proxy;$settings=Get-NraHash $ini
$plan=New-NraPlan $Bundle $exe
$paths=@($plan.files|ForEach-Object{$_.path})
if([Array]::IndexOf($paths,'NeuRotic-PreparedGuides.addon64') -ge [Array]::IndexOf($paths,'dxgi.dll')){throw 'Fixture must install the addon before reaching the locked proxy.'}
# Reads and backup creation succeed, but the real guarded proxy write fails.
$lock=[IO.File]::Open($proxy,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
$failure=$null
try{try{Install-NraPlan $plan|Out-Null}catch{$failure=$_.Exception.Message}}finally{$lock.Dispose()}
if(-not $failure -or $failure -notmatch 'original files were restored'){throw "Automatic rollback did not complete: $failure"}
if((Get-NraHash $proxy) -ne $old -or (Get-NraHash $ini) -ne $settings){throw 'Rollback changed original proxy or settings.'}
foreach($file in $plan.files){if($file.before.hash -eq 'Absent' -and (Test-Path -LiteralPath (Join-Path $dir $file.path))){throw "Rollback left an added file: $($file.path)"}}
$receipts=@(Get-ChildItem -LiteralPath (Join-Path $dir 'NeuRotic-test-backups') -Directory)
if($receipts.Count -ne 1){throw 'Missing rollback receipt.'}
$record=Get-Content -LiteralPath (Join-Path $receipts[0].FullName 'INSTALL-MANIFEST.json') -Raw|ConvertFrom-Json
if($record.status -ne 'restored'){throw 'Rollback receipt is not restored.'}
$result=[ordered]@{passed=$true;failure=$failure;status=$record.status;proxyPreserved=$true;settingsPreserved=$true;addedFilesRemoved=$true;gameExecuted=$false}
$evidence=if($EvidenceRoot){$EvidenceRoot}else{Join-Path $root 'docs/implementation-evidence/nr-advanced-harness-20261008'}
$result|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence 'rollback-result.json') -Encoding UTF8
Write-Output 'PASS: actual payload write failure after support installation; automatic rollback verified.'
