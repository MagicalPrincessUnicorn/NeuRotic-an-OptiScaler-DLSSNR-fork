param([Parameter(Mandatory=$true)][string]$Bundle,[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Split-Path -Parent $PSScriptRoot
$work=Join-Path $root 'builds/nr-advanced-harness-fixture'
if(Test-Path -LiteralPath $work){throw 'Fixture folder must be absent before this test.'}
. (Join-Path $Bundle 'support/NRAdvanced-Harness.ps1')
$results=New-Object 'Collections.Generic.List[string]'
function Assert($ok,[string]$message){if(-not $ok){throw $message}}
function Refuses([scriptblock]$action,[string]$pattern){
 $caught=$null;try{& $action | Out-Null}catch{$caught=$_.Exception.Message}
 Assert ($caught -and $caught -match $pattern) "Expected refusal /$pattern/; received: $caught"
}
New-Item -ItemType Directory -Path $work | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$reference=[IO.Compression.ZipFile]::OpenRead('C:/NeuRotic/_exports/NeuRotic-Flagship-InGameSettings-20261008-Full-Installer.zip')
try{
 $member=$reference.GetEntry('NeuRotic/packages/current/payload/nvngx.dll_dlssnr.dll')
 [IO.Compression.ZipFileExtensions]::ExtractToFile($member,(Join-Path $work 'forwarder.dll'))
}finally{$reference.Dispose()}
function Fixture([string]$name){
 $dir=Join-Path $work $name;New-Item -ItemType Directory -Path $dir | Out-Null
 Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination (Join-Path $dir 'InertGame.exe')
 Copy-Item -LiteralPath (Join-Path $Bundle 'payload/OptiScaler.dll') -Destination (Join-Path $dir 'dxgi.dll')
 $f=[IO.File]::Open((Join-Path $dir 'dxgi.dll'),[IO.FileMode]::Append);try{$f.WriteByte(37)}finally{$f.Dispose()}
 [IO.File]::WriteAllText((Join-Path $dir 'OptiScaler.ini'),"[DlssNr]`r`nEnabled=false`r`nRoute=0`r`nStrength=0.47`r`n; custom settings`r`n")
 Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination (Join-Path $dir 'nvngx_dlssnr.dll')
 Copy-Item -LiteralPath (Join-Path $work 'forwarder.dll') -Destination (Join-Path $dir 'nvngx.dll_dlssnr.dll')
 [IO.File]::WriteAllText((Join-Path $dir 'unrelated.txt'),'leave me alone')
 return (Join-Path $dir 'InertGame.exe')
}
$exe=Fixture 'roundtrip';$dir=Split-Path $exe
$old=Get-NraHash (Join-Path $dir 'dxgi.dll');$ini=Get-NraHash (Join-Path $dir 'OptiScaler.ini');$model=Get-NraHash (Join-Path $dir 'nvngx_dlssnr.dll')
$plan=New-NraPlan $Bundle $exe
$record=Install-NraPlan $plan
Assert ((Get-NraHash (Join-Path $dir 'dxgi.dll')) -eq (Get-NraHash (Join-Path $Bundle 'payload/OptiScaler.dll'))) 'Runtime differs'
Assert ((Get-NraHash (Join-Path $dir 'OptiScaler.ini')) -eq $ini) 'Settings changed'
Assert ((Get-NraHash (Join-Path $dir 'nvngx_dlssnr.dll')) -eq $model) 'Model changed'
Assert ([IO.File]::ReadAllText((Join-Path $dir 'unrelated.txt')) -eq 'leave me alone') 'Unrelated file changed'
Refuses {New-NraPlan $Bundle $exe} 'already installed'
[IO.File]::AppendAllText((Join-Path $dir 'OptiScaler.ini'),"; later user edit`r`n")
$later=Get-NraHash (Join-Path $dir 'OptiScaler.ini')
Restore-NraRecord $exe $record | Out-Null
Assert ((Get-NraHash (Join-Path $dir 'dxgi.dll')) -eq $old) 'Original proxy not restored'
Assert ((Get-NraHash (Join-Path $dir 'OptiScaler.ini')) -eq $later) 'Restore lost later settings'
Assert (-not (Test-Path (Join-Path $dir 'NeuRotic-PreparedGuides.addon64'))) 'Added addon not removed'
Restore-NraRecord $exe $record | Out-Null
$results.Add('install, hashes, preservation, repeated Setup, Restore, later settings, repeated Restore')
$exe=Fixture 'changed-before-install';$plan=New-NraPlan $Bundle $exe
$target=Join-Path (Split-Path $exe) 'dxgi.dll';[IO.File]::AppendAllText($target,'changed')
$hash=Get-NraHash $target;Refuses {Install-NraPlan $plan} 'changed'
Assert ((Get-NraHash $target) -eq $hash) 'Changed target overwritten'
$results.Add('changed destination refused before install')
$exe=Fixture 'changed-before-restore';$record=Install-NraPlan (New-NraPlan $Bundle $exe)
$target=Join-Path (Split-Path $exe) 'NeuRotic-PreparedGuides.addon64';[IO.File]::AppendAllText($target,'newer')
$proxy=Get-NraHash (Join-Path (Split-Path $exe) 'dxgi.dll')
Refuses {Restore-NraRecord $exe $record} 'changed'
Assert ((Get-NraHash (Join-Path (Split-Path $exe) 'dxgi.dll')) -eq $proxy) 'Partial restore on changed target'
$results.Add('changed installed file refuses entire Restore before writes')
$exe=Fixture 'x86';Copy-Item -LiteralPath "$env:SystemRoot/SysWOW64/cmd.exe" -Destination $exe -Force
Refuses {New-NraPlan $Bundle $exe} 'NR Anything';$results.Add('32-bit executable rejected')
$exe=Fixture 'ambiguous';Copy-Item (Join-Path (Split-Path $exe) 'dxgi.dll') (Join-Path (Split-Path $exe) 'winmm.dll')
Refuses {New-NraPlan $Bundle $exe} 'one existing';$results.Add('ambiguous proxies rejected')
$exe=Fixture 'foreign';Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination (Join-Path (Split-Path $exe) 'dxgi.dll') -Force
Refuses {New-NraPlan $Bundle $exe} 'one existing';$results.Add('foreign proxy rejected')
$exe=Fixture 'foreign-addon';[IO.File]::WriteAllText((Join-Path (Split-Path $exe) 'NeuRotic-PreparedGuides.addon64'),'foreign')
Refuses {New-NraPlan $Bundle $exe} 'unrecognized';$results.Add('foreign addon collision rejected')
$exe=Fixture 'dependency';[IO.File]::WriteAllText((Join-Path (Split-Path $exe) 'nvngx.dll_dlssnr.dll'),'wrong')
Refuses {New-NraPlan $Bundle $exe} 'forwarder';$results.Add('forwarder mismatch rejected')
$exe=Fixture 'custom-addon';[IO.File]::WriteAllText((Join-Path (Split-Path $exe) 'ReShade.ini'),"[ADDON]`r`nAddonPath=C:\outside`r`n")
Refuses {New-NraPlan $Bundle $exe} 'addon path';$results.Add('custom addon destination rejected')
$exe=Fixture 'tampered-backup';$record=Install-NraPlan (New-NraPlan $Bundle $exe)
$r=Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
$saved=Join-Path (Split-Path $record) ('original/'+$r.proxy)
[IO.File]::AppendAllText($saved,'corrupt')
Refuses {Restore-NraRecord $exe $record} 'Backup';$results.Add('tampered backup rejected')
$exe=Fixture 'partial';$plan=New-NraPlan $Bundle $exe
# An appearing destination is refused by preflight, before payload writes.
$blocked=Join-Path (Split-Path $exe) 'NeuRotic-PreparedGuides.addon64'
$lock=[IO.File]::Open($blocked,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try{Refuses {Install-NraPlan $plan} 'changed|used by another|File operation failed'}finally{$lock.Dispose()}
$results.Add('appearing destination refused')
$exe=Fixture 'resume-restore';$record=Install-NraPlan (New-NraPlan $Bundle $exe)
$r=Get-Content -LiteralPath $record -Raw|ConvertFrom-Json;$dir=Split-Path $exe
Copy-Item -LiteralPath (Join-Path (Split-Path $record) ('original/'+$r.proxy)) -Destination (Join-Path $dir $r.proxy) -Force
Remove-Item -LiteralPath (Join-Path $dir 'NeuRotic-PreparedGuides.addon64')
$r.status='restoring';Write-NraRecord $record $r
Restore-NraRecord $exe $record|Out-Null
Assert ((Get-NraHash (Join-Path $dir $r.proxy)) -eq @($r.files|Where-Object{$_.path -eq $r.proxy})[0].original) 'Resumed restore failed'
$results.Add('interrupted Restore resumes with mixed original/installed/absent files')
$exe=Fixture 'changed-game';$record=Install-NraPlan (New-NraPlan $Bundle $exe)
[IO.File]::AppendAllText($exe,'updated game')
Refuses {Restore-NraRecord $exe $record} 'changed';$results.Add('changed game executable refuses Restore')
$exe=Fixture 'tampered-package';$plan=New-NraPlan $Bundle $exe
$shader=Join-Path $Bundle 'payload/NeuRotic-NRAdvanced/Shaders/NeuRotic_PreparedGuides.fx'
$bytes=[IO.File]::ReadAllBytes($shader)
try{
 [IO.File]::AppendAllText($shader,'corrupt')
 Refuses {New-NraPlan $Bundle $exe} 'Package hash'
 Refuses {Install-NraPlan $plan} 'changed'
}finally{[IO.File]::WriteAllBytes($shader,$bytes)}
$results.Add('tampered package and source changed after planning refused')
$exe=Fixture 'linked';$dir=Split-Path $exe;$link=Join-Path $dir 'NeuRotic-NRAdvanced'
$elsewhere=Join-Path $work 'linked-target';New-Item -ItemType Directory -Path $elsewhere|Out-Null
New-Item -ItemType Junction -Path $link -Target $elsewhere|Out-Null
try{Refuses {New-NraPlan $Bundle $exe} 'Linked'}finally{[IO.Directory]::Delete($link)}
Assert (@(Get-ChildItem -LiteralPath $elsewhere).Count -eq 0) 'Linked target was written'
$results.Add('junction destination refused without following it')
$evidence=if($EvidenceRoot){$EvidenceRoot}else{Join-Path $root 'docs/implementation-evidence/nr-advanced-harness-20261008'}
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'fixture-results.json') -Encoding UTF8
Write-Output ("PASS: "+$results.Count+' fixture groups; no game executed.')
