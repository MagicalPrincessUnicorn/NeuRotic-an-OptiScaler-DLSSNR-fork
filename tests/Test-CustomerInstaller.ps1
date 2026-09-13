[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PackageRoot,
      [string]$EvidenceRoot = 'C:\OptiScaler-NR-Dev\logs\neurotic-simple-installer',
      [switch]$HardeningOnly)
$ErrorActionPreference='Stop'
$PackageRoot=(Resolve-Path -LiteralPath $PackageRoot).Path
$testRoot=Join-Path $EvidenceRoot ('installer-fixtures-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$engine=Join-Path $PackageRoot 'support\NeuRotic-Setup-Engine.ps1'
$launcher=Join-Path $PackageRoot 'NeuRotic-Setup.cmd'
$uninstaller=Join-Path $PackageRoot 'NeuRotic-Uninstall.cmd'
$manifest=Get-Content -Raw -LiteralPath (Join-Path $PackageRoot 'support\PACKAGE-MANIFEST.json') | ConvertFrom-Json
$psExe='C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
$cmdExe='C:\Windows\System32\cmd.exe'
$env:PSModulePath=''
$unicode=[char]0xE9

function Check([bool]$Value,[string]$Message) {
    if(-not $Value){throw $Message}; Write-Output "PASS: $Message"
}
function HashFile([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function BytesEqual([byte[]]$A,[byte[]]$B){
    if($A.Length -ne $B.Length){return $false}
    for($i=0;$i -lt $A.Length;$i++){if($A[$i] -ne $B[$i]){return $false}}
    return $true
}
function Fixture([string]$Name,[bool]$Model=$true){
    $folder=Join-Path $testRoot $Name
    New-Item -ItemType Directory -Path $folder | Out-Null
    Copy-Item -LiteralPath $cmdExe -Destination (Join-Path $folder 'FixtureGame.exe')
    if($Model){[IO.File]::WriteAllText((Join-Path $folder 'nvngx_dlssnr.dll'),'private model fixture')}
    return $folder
}
function Inventory([string]$Folder){
    return (@(Get-ChildItem -LiteralPath $Folder -Recurse -File | Where-Object {$_.FullName -notmatch '\\NeuRotic-(?:test-)?backups\\' -and $_.FullName -notmatch '\\NeuRotic\\Installer\\'} |
        Sort-Object FullName | ForEach-Object {$_.FullName.Substring($Folder.Length)+':'+(HashFile $_.FullName)}) -join "`n")
}
function Backup([string]$Folder){
    $folders=@()
    foreach($name in @('NeuRotic-backups','NeuRotic-test-backups')){
        $root=Join-Path $Folder $name
        if(Test-Path -LiteralPath $root -PathType Container){$folders+=@(Get-ChildItem -LiteralPath $root -Directory)}
    }
    return @($folders | Sort-Object CreationTime -Descending)[0].FullName
}
function RunEngine([string]$Name,[string[]]$Arguments,[bool]$Success=$true,[string[]]$InputLines=@()){
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{
        if($InputLines.Count){$output=$InputLines | & $psExe -NoProfile -ExecutionPolicy Bypass -File $engine @Arguments 2>&1}
        else{$output=& $psExe -NoProfile -ExecutionPolicy Bypass -File $engine @Arguments 2>&1}
        $code=$LASTEXITCODE
    }finally{$ErrorActionPreference=$old}
    $output | Out-File -LiteralPath (Join-Path $testRoot ($Name+'.log')) -Encoding UTF8
    if(($code -eq 0) -ne $Success){throw "$Name returned $code : $output"}
    return ($output -join "`n")
}
function RunCmd([string]$Name,[string]$Script,[string[]]$Arguments,[bool]$Success=$true,[string[]]$InputLines=@()){
    $quotedArgs=($Arguments | ForEach-Object {if($_ -match '\s'){'"'+$_+'"'}else{$_}}) -join ' '
    $command='""'+$Script+'" '+$quotedArgs+'"'
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$cmdExe;$start.Arguments='/d /s /c '+$command
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=[Diagnostics.Process]::Start($start)
    foreach($line in $InputLines){$process.StandardInput.WriteLine($line)}
    $process.StandardInput.Close()
    $outTask=$process.StandardOutput.ReadToEndAsync();$errTask=$process.StandardError.ReadToEndAsync()
    $process.WaitForExit();$code=$process.ExitCode;$output=$outTask.Result+$errTask.Result;$process.Dispose()
    $output | Out-File -LiteralPath (Join-Path $testRoot ($Name+'.log')) -Encoding UTF8
    if(($code -eq 0) -ne $Success){throw "$Name returned $code : $output"}
    return $output
}
function Encoded([string]$Text,[string]$Kind){
    switch($Kind){
        'utf8' {$encoding=New-Object Text.UTF8Encoding($false,$true);$bom=New-Object byte[] 0}
        'utf8bom' {$encoding=New-Object Text.UTF8Encoding($true,$true);$bom=$encoding.GetPreamble()}
        'utf16le' {$encoding=New-Object Text.UnicodeEncoding($false,$true,$true);$bom=$encoding.GetPreamble()}
        'utf16be' {$encoding=New-Object Text.UnicodeEncoding($true,$true,$true);$bom=$encoding.GetPreamble()}
        default {throw 'Unknown encoding fixture'}
    }
    $body=$encoding.GetBytes($Text);$bytes=New-Object byte[] ($bom.Length+$body.Length)
    if($bom.Length){[Array]::Copy($bom,0,$bytes,0,$bom.Length)}
    [Array]::Copy($body,0,$bytes,$bom.Length,$body.Length);return $bytes
}

foreach($file in $manifest.files){Check ((HashFile (Join-Path $PackageRoot $file.path)) -eq $file.sha256) ('Package hash: '+$file.path)}
Check (Test-Path -LiteralPath $uninstaller -PathType Leaf) 'Public package contains NeuRotic-Uninstall.cmd'
$candidateHash=HashFile (Join-Path $PackageRoot 'payload\OptiScaler.dll')
$supported=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
if (-not $HardeningOnly) {

# No occupied target: the actual customer launcher asks for the proxy name, then installs with no INSTALL text.
$fresh=Fixture ('fresh unicode '+$unicode) $false
$freshBefore=Inventory $fresh
RunEngine 'fresh-preflight' @('-GameExecutable',(Join-Path $fresh 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-CheckOnly') | Out-Null
Check ((Inventory $fresh) -eq $freshBefore) 'Fresh check-only changes no game files'
$output=RunCmd 'fresh-actual-setup' $launcher @('-GameExecutable',(Join-Path $fresh 'FixtureGame.exe')) $true @('1')
Check ($output -notmatch 'Type INSTALL' -and $output -match 'Choose the filename' -and $output -match 'No existing dxgi.dll: install immediately') 'Fresh Setup asks for proxy name and requires no typed INSTALL'
Check ((HashFile (Join-Path $fresh 'dxgi.dll')) -eq $candidateHash) 'Fresh Setup installs NeuRotic as dxgi.dll'
$backup=Backup $fresh;$record=Get-Content -Raw -LiteralPath (Join-Path $backup 'INSTALL-MANIFEST.json') | ConvertFrom-Json
Check ($record.selected_proxy -eq 'dxgi.dll' -and $record.existing_proxy_action -eq 'None' -and -not $record.original_proxy.existed) 'Fresh manifest records selected proxy and no original file'
Check (-not $record.original_ini.existed -and $null -eq $record.original_ini.bytes_base64) 'Fresh manifest records no original INI bytes'
Check ((Get-Content -Raw -LiteralPath (Join-Path $fresh 'OptiScaler.ini')) -match '(?m)^LogToFile\s*=\s*false\s*$') 'Fresh installation explicitly disables the file logging checkbox'
[IO.File]::AppendAllText((Join-Path $fresh 'OptiScaler.ini'),"`r`n; test-session change")
$changedIni=HashFile (Join-Path $fresh 'OptiScaler.ini')
$restoreOutput=RunCmd 'fresh-actual-restore' (Join-Path $backup 'Restore.cmd') @()
Check ($restoreOutput -notmatch 'Type RESTORE' -and $restoreOutput -match 'exact pre-install files restored') 'Restore is one-click with no typed confirmation'
Check (-not (Test-Path -LiteralPath (Join-Path $fresh 'dxgi.dll')) -and -not (Test-Path -LiteralPath (Join-Path $fresh 'OptiScaler.ini'))) 'Fresh restore removes newly installed dxgi.dll and INI'
$undo=@(Get-ChildItem -LiteralPath $backup -Directory | Where-Object Name -Like 'restore-*')[0].FullName
Check ((HashFile (Join-Path $undo 'OptiScaler.ini')) -eq $changedIni) 'Restore retains the replaced test-session INI in its undo folder'

$freshMissingIni=Fixture 'fresh-ini-already-absent' $false
RunEngine 'fresh-missing-ini-install' @('-GameExecutable',(Join-Path $freshMissingIni 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
$backup=Backup $freshMissingIni
Remove-Item -LiteralPath (Join-Path $freshMissingIni 'OptiScaler.ini') -Force
RunCmd 'fresh-missing-ini-restore' (Join-Path $backup 'Restore.cmd') @() | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $freshMissingIni 'dxgi.dll')) -and -not (Test-Path -LiteralPath (Join-Path $freshMissingIni 'OptiScaler.ini'))) 'Restore accepts an already-absent fresh INI and returns exact absent state'

# Every loader-supported name can be selected, installed, recorded, and restored.
$supported=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
foreach($proxy in $supported){
    $folder=Fixture ('proxy-'+$proxy.Replace('.','-')) $false
    RunEngine ('proxy-install-'+$proxy) @('-GameExecutable',(Join-Path $folder 'FixtureGame.exe'),'-ProxyName',$proxy) | Out-Null
    Check ((HashFile (Join-Path $folder $proxy)) -eq $candidateHash) ("Supported proxy installs as $proxy")
    $backup=Backup $folder;$record=Get-Content -Raw -LiteralPath (Join-Path $backup 'INSTALL-MANIFEST.json') | ConvertFrom-Json
    Check ($record.selected_proxy -eq $proxy -and $record.original_proxy.name -eq $proxy) ("Manifest records selected proxy $proxy")
    RunCmd ('proxy-restore-'+$proxy) (Join-Path $backup 'Restore.cmd') @() | Out-Null
    Check (-not (Test-Path -LiteralPath (Join-Path $folder $proxy))) ("Restore removes fresh proxy $proxy")
}

# An occupied ordinary proxy offers replace / choose another / cancel. Choosing another loops to the full menu.
$chooseAnother=Fixture 'choose-another-proxy' $false
[IO.File]::WriteAllText((Join-Path $chooseAnother 'version.dll'),'occupied version proxy')
$versionHash=HashFile (Join-Path $chooseAnother 'version.dll')
$output=RunEngine 'choose-another-proxy' @('-GameExecutable',(Join-Path $chooseAnother 'FixtureGame.exe')) $true @('3','2','2')
Check ($output -match 'A version.dll already exists' -and $output -match 'Choose a different filename') 'Occupied ordinary proxy displays replace, choose-another, and cancel choices'
Check ((HashFile (Join-Path $chooseAnother 'version.dll')) -eq $versionHash -and (HashFile (Join-Path $chooseAnother 'winmm.dll')) -eq $candidateHash) 'Choose-another preserves occupied proxy and installs the new selection'
$backup=Backup $chooseAnother;RunCmd 'choose-another-restore' (Join-Path $backup 'Restore.cmd') @() | Out-Null
Check ((HashFile (Join-Path $chooseAnother 'version.dll')) -eq $versionHash -and -not (Test-Path -LiteralPath (Join-Path $chooseAnother 'winmm.dll'))) 'Choose-another restore returns exact pre-install state'

$replaceOrdinary=Fixture 'replace-ordinary-proxy' $false
[IO.File]::WriteAllText((Join-Path $replaceOrdinary 'winmm.dll'),'occupied Vulkan proxy')
$oldOrdinary=HashFile (Join-Path $replaceOrdinary 'winmm.dll')
RunEngine 'replace-ordinary-proxy' @('-GameExecutable',(Join-Path $replaceOrdinary 'FixtureGame.exe'),'-ProxyName','winmm.dll','-ExistingProxyAction','Replace') | Out-Null
$backup=Backup $replaceOrdinary;$record=Get-Content -Raw -LiteralPath (Join-Path $backup 'INSTALL-MANIFEST.json') | ConvertFrom-Json
Check ((HashFile (Join-Path $replaceOrdinary 'winmm.dll')) -eq $candidateHash -and $record.original_proxy.sha256 -eq $oldOrdinary) 'Ordinary occupied proxy is backed up and replaced'
RunCmd 'replace-ordinary-restore' (Join-Path $backup 'Restore.cmd') @() | Out-Null
Check ((HashFile (Join-Path $replaceOrdinary 'winmm.dll')) -eq $oldOrdinary) 'Ordinary proxy restore returns the exact original file'

$legacy=Fixture 'legacy-dxgi-parameter' $false
[IO.File]::WriteAllText((Join-Path $legacy 'dxgi.dll'),'legacy parameter target')
RunEngine 'legacy-dxgi-parameter' @('-GameExecutable',(Join-Path $legacy 'FixtureGame.exe'),'-ExistingDxgiAction','Delete') | Out-Null
Check ((HashFile (Join-Path $legacy 'dxgi.dll')) -eq $candidateHash) 'Legacy ExistingDxgiAction remains compatible'

# Existing dxgi.dll delete: exact backup, no INI edit, changed runtime refusal, exact restore.
$delete=Fixture 'delete-existing'
[IO.File]::WriteAllText((Join-Path $delete 'dxgi.dll'),'original non-ReShade dxgi')
$deleteIni=Encoded ("[Plugins]`r`nLoadReshade = false`r`n; preserve me "+$unicode+"`r`n") 'utf8bom'
[IO.File]::WriteAllBytes((Join-Path $delete 'OptiScaler.ini'),$deleteIni)
$oldDxgi=HashFile (Join-Path $delete 'dxgi.dll');$oldIni=HashFile (Join-Path $delete 'OptiScaler.ini')
RunEngine 'delete-install' @('-GameExecutable',(Join-Path $delete 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-ExistingProxyAction','Replace') | Out-Null
Check ((HashFile (Join-Path $delete 'dxgi.dll')) -eq $candidateHash) 'Delete choice installs candidate dxgi.dll'
Check ((HashFile (Join-Path $delete 'OptiScaler.ini')) -eq $oldIni) 'Delete choice does not enable LoadReshade or alter INI bytes'
$backup=Backup $delete;$record=Get-Content -Raw -LiteralPath (Join-Path $backup 'INSTALL-MANIFEST.json') | ConvertFrom-Json
Check ($record.existing_proxy_action -eq 'Replace' -and $record.original_proxy.name -eq 'dxgi.dll' -and $record.original_proxy.sha256 -eq $oldDxgi) 'Replace manifest records original dxgi.dll name and hash'
Check ([Convert]::ToBase64String($deleteIni) -eq $record.original_ini.bytes_base64 -and $record.original_ini.sha256 -eq $oldIni) 'Delete manifest records exact original INI bytes and hash'
[IO.File]::WriteAllText((Join-Path $delete 'dxgi.dll'),'later runtime')
$before=Inventory $delete
RunEngine 'restore-refuses-changed-runtime' @('-Restore') $false | Out-Null
Check ((Inventory $delete) -eq $before) 'Restore refuses changed runtime without mutation'
Copy-Item -LiteralPath (Join-Path $PackageRoot 'payload\OptiScaler.dll') -Destination (Join-Path $delete 'dxgi.dll') -Force
RunCmd 'delete-restore' (Join-Path $backup 'Restore.cmd') @() | Out-Null
Check ((HashFile (Join-Path $delete 'dxgi.dll')) -eq $oldDxgi -and $(BytesEqual ([IO.File]::ReadAllBytes((Join-Path $delete 'OptiScaler.ini'))) $deleteIni)) 'Delete restore returns exact original dxgi.dll and INI'

# Rename choice: test exact targeted edits and restoration across supported encodings.
foreach($kind in @('utf8','utf8bom','utf16le','utf16be')){
    $folder=Fixture ('rename-'+$kind)
    [IO.File]::WriteAllText((Join-Path $folder 'dxgi.dll'),('ReShade fixture '+$kind))
    $text="; untouched prefix $unicode`r`n[Plugins]`r`nLoadReshade = auto ; keep comment`r`nOther = 7`r`n[End]`r`nValue = yes`r`n"
    $expectedText=$text.Replace('LoadReshade = auto','LoadReshade = true')
    $original=Encoded $text $kind;$expected=Encoded $expectedText $kind
    [IO.File]::WriteAllBytes((Join-Path $folder 'OptiScaler.ini'),$original)
    $oldDxgi=HashFile (Join-Path $folder 'dxgi.dll')
    RunEngine ('rename-install-'+$kind) @('-GameExecutable',(Join-Path $folder 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-ExistingProxyAction','RenameReShade') | Out-Null
    Check ((HashFile (Join-Path $folder 'ReShade64.dll')) -eq $oldDxgi) ("$kind rename preserves existing dxgi.dll as ReShade64.dll")
    Check ((HashFile (Join-Path $folder 'dxgi.dll')) -eq $candidateHash) ("$kind rename installs new dxgi.dll")
    Check $(BytesEqual ([IO.File]::ReadAllBytes((Join-Path $folder 'OptiScaler.ini'))) $expected) ("$kind targeted LoadReshade edit preserves encoding and all other bytes")
    $backup=Backup $folder;$record=Get-Content -Raw -LiteralPath (Join-Path $backup 'INSTALL-MANIFEST.json') | ConvertFrom-Json
    Check ($record.reshade_operation.original_name -eq 'dxgi.dll' -and $record.reshade_operation.new_name -eq 'ReShade64.dll' -and $record.reshade_operation.sha256 -eq $oldDxgi) ("$kind manifest records rename names and hash")
    Check ([Convert]::ToBase64String($original) -eq $record.original_ini.bytes_base64) ("$kind manifest embeds exact original INI bytes")
    [IO.File]::AppendAllText((Join-Path $folder 'OptiScaler.ini'),'; later settings')
    RunCmd ('rename-restore-'+$kind) (Join-Path $backup 'Restore.cmd') @() | Out-Null
    Check ((HashFile (Join-Path $folder 'dxgi.dll')) -eq $oldDxgi -and -not (Test-Path -LiteralPath (Join-Path $folder 'ReShade64.dll'))) ("$kind restore returns original dxgi.dll name")
    Check $(BytesEqual ([IO.File]::ReadAllBytes((Join-Path $folder 'OptiScaler.ini'))) $original) ("$kind restore returns exact original INI")
}

# Missing key is inserted in Plugins while preserving BOM/encoding, then restored exactly.
$missing=Fixture 'rename-missing-key'
[IO.File]::WriteAllText((Join-Path $missing 'dxgi.dll'),'ReShade missing-key fixture')
$missingBytes=Encoded "[Plugins]`r`nOther = 3`r`n[Next]`r`nValue = 4`r`n" 'utf16le'
[IO.File]::WriteAllBytes((Join-Path $missing 'OptiScaler.ini'),$missingBytes)
RunEngine 'rename-inserts-key' @('-GameExecutable',(Join-Path $missing 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-ExistingProxyAction','RenameReShade') | Out-Null
$installed=[Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes((Join-Path $missing 'OptiScaler.ini')),2,((Get-Item (Join-Path $missing 'OptiScaler.ini')).Length-2))
Check ($installed -match '(?m)^LoadReshade = true\r?$' -and $installed.IndexOf('LoadReshade') -lt $installed.IndexOf('[Next]')) 'Missing LoadReshade key is inserted into Plugins'
$backup=Backup $missing;RunCmd 'missing-key-restore' (Join-Path $backup 'Restore.cmd') @() | Out-Null
Check $(BytesEqual ([IO.File]::ReadAllBytes((Join-Path $missing 'OptiScaler.ini'))) $missingBytes) 'Missing-key restore returns exact original INI'

# Interactive text and cancel behavior; no automatic classification of dxgi.dll.
$cancel=Fixture 'interactive-cancel'
[IO.File]::WriteAllText((Join-Path $cancel 'dxgi.dll'),'unknown owner')
$before=Inventory $cancel
$output=RunEngine 'interactive-choice-cancel' @('-GameExecutable',(Join-Path $cancel 'FixtureGame.exe')) $true @('1','4')
Check ($output -match 'A dxgi.dll already exists in this game folder' -and $output -match '1\. Replace the file \(Backup of original will be created\)' -and $output -match '2\. Rename to ReShade64\.dll - Choose this if you want to use NeuRotic and ReShade' -and $output -match '3\. Choose a different Filename' -and $output -match '4\. Cancel') 'Existing dxgi.dll displays the exact required choice order and wording'
Check ((Inventory $cancel) -eq $before) 'Cancel changes no game files'

$interactiveRename=Fixture 'interactive-rename'
[IO.File]::WriteAllText((Join-Path $interactiveRename 'dxgi.dll'),'interactive ReShade')
[IO.File]::WriteAllText((Join-Path $interactiveRename 'OptiScaler.ini'),"[Plugins]`r`nLoadReshade = false`r`n")
RunEngine 'interactive-choice-rename' @('-GameExecutable',(Join-Path $interactiveRename 'FixtureGame.exe')) $true @('1','2') | Out-Null
Check ((HashFile (Join-Path $interactiveRename 'dxgi.dll')) -eq $candidateHash -and (Test-Path -LiteralPath (Join-Path $interactiveRename 'ReShade64.dll'))) 'Interactive choice 2 performs ReShade rename flow'

$interactiveDelete=Fixture 'interactive-delete'
[IO.File]::WriteAllText((Join-Path $interactiveDelete 'dxgi.dll'),'interactive delete')
RunEngine 'interactive-choice-delete' @('-GameExecutable',(Join-Path $interactiveDelete 'FixtureGame.exe')) $true @('1','1') | Out-Null
Check ((HashFile (Join-Path $interactiveDelete 'dxgi.dll')) -eq $candidateHash -and -not (Test-Path -LiteralPath (Join-Path $interactiveDelete 'ReShade64.dll'))) 'Interactive choice 1 performs replacement flow'

$conflict=Fixture 'reshade64-conflict'
[IO.File]::WriteAllText((Join-Path $conflict 'dxgi.dll'),'existing dxgi')
[IO.File]::WriteAllText((Join-Path $conflict 'ReShade64.dll'),'existing reshade64')
$before=Inventory $conflict
RunEngine 'reshade64-conflict' @('-GameExecutable',(Join-Path $conflict 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-ExistingProxyAction','RenameReShade') $false | Out-Null
Check ((Inventory $conflict) -eq $before) 'Rename fails closed when ReShade64.dll exists'

$wrongAction=Fixture 'action-without-target'
$before=Inventory $wrongAction
RunEngine 'action-without-target' @('-GameExecutable',(Join-Path $wrongAction 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-ExistingProxyAction','Replace') $false | Out-Null
Check ((Inventory $wrongAction) -eq $before) 'Existing-file action without an occupied target fails before writes'

# Force a post-rename/post-INI copy failure and prove every changed file rolls back.
$rollback=Fixture 'atomic-rollback'
[IO.File]::WriteAllText((Join-Path $rollback 'dxgi.dll'),'rollback original dxgi')
$rollbackIni=Encoded "[Plugins]`r`nLoadReshade = false`r`n" 'utf8'
[IO.File]::WriteAllBytes((Join-Path $rollback 'OptiScaler.ini'),$rollbackIni)
New-Item -ItemType Directory -Path (Join-Path $rollback 'OptiScaler') | Out-Null
$locked=Join-Path $rollback 'OptiScaler\amd_fidelityfx_framegeneration_dx12.dll'
[IO.File]::WriteAllText($locked,'locked original support')
$oldDxgi=HashFile (Join-Path $rollback 'dxgi.dll');$oldLocked=HashFile $locked
$stream=[IO.File]::Open($locked,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{RunEngine 'forced-copy-failure' @('-GameExecutable',(Join-Path $rollback 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-ExistingProxyAction','RenameReShade') $false | Out-Null}
finally{$stream.Dispose()}
Check ((HashFile (Join-Path $rollback 'dxgi.dll')) -eq $oldDxgi -and -not (Test-Path -LiteralPath (Join-Path $rollback 'ReShade64.dll'))) 'Failure rolls ReShade rename and dxgi replacement back'
Check $(BytesEqual ([IO.File]::ReadAllBytes((Join-Path $rollback 'OptiScaler.ini'))) $rollbackIni) 'Failure rolls targeted INI edit back exactly'
Check ((HashFile $locked) -eq $oldLocked) 'Failure retains original locked destination'
$record=Get-Content -Raw -LiteralPath (Join-Path (Backup $rollback) 'INSTALL-MANIFEST.json') | ConvertFrom-Json
Check ($record.status -eq 'failed-rolled-back') 'Failure manifest records complete rollback'

# Package integrity and duplicate-install protections remain active.
$other=Fixture 'other-optiscaler-proxy'
Copy-Item -LiteralPath (Join-Path $PackageRoot 'payload\OptiScaler.dll') -Destination (Join-Path $other 'winmm.dll')
$before=Inventory $other
RunEngine 'other-optiscaler-proxy' @('-GameExecutable',(Join-Path $other 'FixtureGame.exe'),'-ProxyName','dxgi.dll') $false | Out-Null
Check ((Inventory $other) -eq $before) 'Existing OptiScaler under another proxy fails before writes'

$tampered=Join-Path $testRoot 'tampered-package'
Copy-Item -LiteralPath $PackageRoot -Destination $tampered -Recurse
[IO.File]::AppendAllText((Join-Path $tampered 'payload\OptiScaler.ini'),'; tampered')
$tamperedEngine=Join-Path $tampered 'support\NeuRotic-Setup-Engine.ps1'
$oldEngine=$engine;$engine=$tamperedEngine
$before=Inventory $wrongAction
RunEngine 'tampered-package' @('-GameExecutable',(Join-Path $wrongAction 'FixtureGame.exe'),'-ProxyName','dxgi.dll') $false | Out-Null
$engine=$oldEngine
Check ((Inventory $wrongAction) -eq $before) 'Tampered package fails before destination writes'

# Managed public install state drives both uninstaller entry points without asking for the proxy again.
$public=Fixture 'public-uninstall-download' $true
RunEngine 'public-install-download' @('-GameExecutable',(Join-Path $public 'FixtureGame.exe'),'-ProxyName','winmm.dll') | Out-Null
$statePath=Join-Path $public 'NeuRotic\Installer\Current-Install.json'
$installedUninstaller=Join-Path $public 'Uninstall NeuRotic.cmd'
Check ((Test-Path -LiteralPath $statePath -PathType Leaf) -and (Test-Path -LiteralPath $installedUninstaller -PathType Leaf)) 'Setup installs managed state and an in-game uninstaller'
Check ((Test-Path -LiteralPath (Join-Path $public 'NeuRotic-backups') -PathType Container) -and
    -not (Test-Path -LiteralPath (Join-Path $public 'NeuRotic-test-backups'))) 'Public install uses the public backup folder, not the retired test folder'
$state=Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json
Check ($state.schema_version -eq 2 -and $state.selected_proxy -eq 'winmm.dll' -and $state.restore_chain.Count -eq 1) 'Managed state records proxy, version, time and restore chain'
[IO.File]::AppendAllText((Join-Path $public 'OptiScaler.ini'),"`r`n; keep-public-settings")
$modelHash=HashFile (Join-Path $public 'nvngx_dlssnr.dll')
$output=RunCmd 'public-download-uninstall' $uninstaller @('-GameExecutable',(Join-Path $public 'FixtureGame.exe'),'-UninstallMode','KeepSettings','-ConfirmUninstall') $true @('')
Check ($output -match 'Installed as: winmm.dll' -and $output -match 'PASS: NeuRotic was uninstalled') 'Downloaded uninstaller detects and reports the managed proxy'
Check (-not (Test-Path -LiteralPath (Join-Path $public 'winmm.dll')) -and -not (Test-Path -LiteralPath (Join-Path $public 'OptiScaler.ini')) -and (Get-Content -Raw -LiteralPath (Join-Path $public 'NeuRotic\UserData\OptiScaler.ini')) -match 'keep-public-settings') 'Recommended uninstall restores the clean game root and saves settings for reinstalling'
Check ((HashFile (Join-Path $public 'nvngx_dlssnr.dll')) -eq $modelHash) 'Recommended uninstall preserves the private model'
Check (-not (Test-Path -LiteralPath $installedUninstaller)) 'Downloaded uninstaller removes the installed launcher after success'
RunEngine 'public-reinstall-reuses-settings' @('-GameExecutable',(Join-Path $public 'FixtureGame.exe'),'-ProxyName','winmm.dll') | Out-Null
Check ((Get-Content -Raw -LiteralPath (Join-Path $public 'OptiScaler.ini')) -match 'keep-public-settings') 'Reinstall automatically reuses settings retained by the uninstaller'
RunEngine 'public-reinstall-cleanup' @('-Uninstall','-GameExecutable',(Join-Path $public 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null

$installed=Fixture 'public-uninstall-installed-copy' $true
RunEngine 'public-install-installed-copy' @('-GameExecutable',(Join-Path $installed 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
$installedUninstaller=Join-Path $installed 'Uninstall NeuRotic.cmd'
$output=RunCmd 'public-installed-uninstall' $installedUninstaller @('-UninstallMode','RemoveSettings','-ConfirmUninstall') $true @('')
Check ($output -notmatch 'select your game executable' -and $output -match 'Installed as: dxgi.dll') 'In-game uninstaller uses its own game folder without asking for an executable'
Check (-not (Test-Path -LiteralPath (Join-Path $installed 'dxgi.dll')) -and -not (Test-Path -LiteralPath (Join-Path $installed 'OptiScaler.ini')) -and -not (Test-Path -LiteralPath $installedUninstaller)) 'In-game uninstaller removes a fresh runtime, settings and itself'
Check (Test-Path -LiteralPath (Join-Path $installed 'nvngx_dlssnr.dll')) 'Remove Settings still preserves the private model'

# Updates append to a restore chain; one uninstall reaches the first pre-NeuRotic state.
$stacked=Fixture 'public-stacked-updates' $true
RunEngine 'stacked-install-1' @('-GameExecutable',(Join-Path $stacked 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
RunEngine 'stacked-install-2' @('-GameExecutable',(Join-Path $stacked 'FixtureGame.exe'),'-ExistingInstallAction','Update') | Out-Null
$state=Get-Content -Raw -LiteralPath (Join-Path $stacked 'NeuRotic\Installer\Current-Install.json') | ConvertFrom-Json
Check ($state.restore_chain.Count -eq 2 -and $state.install_history.Count -eq 2) 'Update records a two-entry installation lineage'
RunEngine 'stacked-uninstall-preview' @('-Uninstall','-GameExecutable',(Join-Path $stacked 'FixtureGame.exe'),'-UninstallMode','RemoveSettings','-CheckOnly') | Out-Null
Check (Test-Path -LiteralPath (Join-Path $stacked 'dxgi.dll')) 'Uninstall preview makes no changes'
RunEngine 'stacked-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $stacked 'FixtureGame.exe'),'-UninstallMode','RemoveSettings','-ConfirmUninstall') | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $stacked 'dxgi.dll')) -and -not (Test-Path -LiteralPath (Join-Path $stacked 'OptiScaler.ini'))) 'One uninstall walks stacked updates back to the original fresh state'

# A deliberate external update must not silently merge into a managed restore chain.
# Adoption creates a new, one-entry lineage that restores the exact current files on uninstall.
$adopted=Fixture 'public-adopt-modified-install' $true
RunEngine 'adopted-install-original' @('-GameExecutable',(Join-Path $adopted 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
$oldBackup=Backup $adopted
[IO.File]::WriteAllText((Join-Path $adopted 'dxgi.dll'),'externally updated proxy')
[IO.File]::WriteAllText((Join-Path $adopted 'nvngx.dll_dlssnr.dll'),'externally updated forwarder')
$adoptedProxyHash=HashFile (Join-Path $adopted 'dxgi.dll')
$adoptedForwarderHash=HashFile (Join-Path $adopted 'nvngx.dll_dlssnr.dll')
$adoptedBefore=Inventory $adopted
RunEngine 'adopted-modified-refusal' @('-GameExecutable',(Join-Path $adopted 'FixtureGame.exe'),'-ExistingInstallAction','Update') $false | Out-Null
Check ((Inventory $adopted) -eq $adoptedBefore) 'Modified managed files are refused without an explicit adoption choice'
RunEngine 'adopted-modified-install' @('-GameExecutable',(Join-Path $adopted 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-AdoptModifiedInstall','-ExistingProxyAction','Replace') | Out-Null
$state=Get-Content -Raw -LiteralPath (Join-Path $adopted 'NeuRotic\Installer\Current-Install.json') | ConvertFrom-Json
Check ($state.adopted_modified_install -and $state.restore_chain.Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $oldBackup 'INSTALL-MANIFEST.json'))) 'Explicit adoption preserves earlier records and starts a verified recovery lineage'
RunEngine 'adopted-modified-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $adopted 'FixtureGame.exe'),'-UninstallMode','RemoveSettings','-ConfirmUninstall') | Out-Null
Check ((HashFile (Join-Path $adopted 'dxgi.dll')) -eq $adoptedProxyHash -and (HashFile (Join-Path $adopted 'nvngx.dll_dlssnr.dll')) -eq $adoptedForwarderHash) 'Adopted install uninstall restores the exact externally updated files'

# A modified managed state/record must never redirect restore work to another game folder.
$tamperedState=Fixture 'public-tampered-state' $true
$otherGame=Fixture 'public-tampered-state-other-game' $true
RunEngine 'tampered-state-install' @('-GameExecutable',(Join-Path $tamperedState 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
$tamperedStatePath=Join-Path $tamperedState 'NeuRotic\Installer\Current-Install.json'
$tamperedStateRecord=Get-Content -Raw -LiteralPath $tamperedStatePath | ConvertFrom-Json
$tamperedRecordPath=[string]$tamperedStateRecord.restore_chain[0]
$originalRecordBytes=[IO.File]::ReadAllBytes($tamperedRecordPath)
$tamperedRecord=Get-Content -Raw -LiteralPath $tamperedRecordPath | ConvertFrom-Json
$tamperedRecord.game_executable=Join-Path $otherGame 'FixtureGame.exe'
[IO.File]::WriteAllText($tamperedRecordPath,($tamperedRecord | ConvertTo-Json -Depth 20),(New-Object Text.UTF8Encoding($false)))
$beforeSelected=Inventory $tamperedState;$beforeOther=Inventory $otherGame
RunEngine 'tampered-state-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $tamperedState 'FixtureGame.exe'),'-UninstallMode','RemoveSettings','-ConfirmUninstall') $false | Out-Null
Check ((Inventory $tamperedState) -eq $beforeSelected -and (Inventory $otherGame) -eq $beforeOther) 'Tampered restore chain is rejected before either game folder changes'
[IO.File]::WriteAllBytes($tamperedRecordPath,$originalRecordBytes)
RunEngine 'tampered-state-cleanup' @('-Uninstall','-GameExecutable',(Join-Path $tamperedState 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null

$changedProxy=Fixture 'public-change-proxy' $true
RunEngine 'change-proxy-install' @('-GameExecutable',(Join-Path $changedProxy 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
RunEngine 'change-proxy-transaction' @('-GameExecutable',(Join-Path $changedProxy 'FixtureGame.exe'),'-ProxyName','winmm.dll','-ExistingInstallAction','ChangeProxy') | Out-Null
$state=Get-Content -Raw -LiteralPath (Join-Path $changedProxy 'NeuRotic\Installer\Current-Install.json') | ConvertFrom-Json
Check (-not (Test-Path -LiteralPath (Join-Path $changedProxy 'dxgi.dll')) -and (HashFile (Join-Path $changedProxy 'winmm.dll')) -eq $candidateHash -and $state.selected_proxy -eq 'winmm.dll') 'Managed proxy change removes the old loader and records the new one atomically'
RunEngine 'change-proxy-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $changedProxy 'FixtureGame.exe'),'-UninstallMode','RemoveSettings','-ConfirmUninstall') | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $changedProxy 'dxgi.dll')) -and -not (Test-Path -LiteralPath (Join-Path $changedProxy 'winmm.dll'))) 'Uninstall after a proxy change returns to the original fresh state'

# Existing proxy and settings are restored while current NeuRotic settings are retained separately.
$baseline=Fixture 'public-existing-baseline' $true
[IO.File]::WriteAllText((Join-Path $baseline 'dxgi.dll'),'original dxgi baseline')
[IO.File]::WriteAllText((Join-Path $baseline 'OptiScaler.ini'),"[Plugins]`r`nLoadReshade = false`r`n; original")
$baselineProxy=HashFile (Join-Path $baseline 'dxgi.dll');$baselineIni=HashFile (Join-Path $baseline 'OptiScaler.ini')
RunEngine 'baseline-install' @('-GameExecutable',(Join-Path $baseline 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-ExistingProxyAction','Replace') | Out-Null
[IO.File]::AppendAllText((Join-Path $baseline 'OptiScaler.ini'),"`r`n; current-neurotic-settings")
RunEngine 'baseline-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $baseline 'FixtureGame.exe'),'-UninstallMode','KeepSettings','-ConfirmUninstall') | Out-Null
Check ((HashFile (Join-Path $baseline 'dxgi.dll')) -eq $baselineProxy -and (HashFile (Join-Path $baseline 'OptiScaler.ini')) -eq $baselineIni) 'Keep Settings restores an existing proxy and original root INI exactly'
Check ((Get-Content -Raw -LiteralPath (Join-Path $baseline 'NeuRotic\UserData\OptiScaler.ini')) -match 'current-neurotic-settings') 'Keep Settings saves current NeuRotic settings separately when an original INI existed'
RunEngine 'baseline-reinstall-settings' @('-GameExecutable',(Join-Path $baseline 'FixtureGame.exe'),'-ProxyName','dxgi.dll','-ExistingProxyAction','Replace') | Out-Null
Check ((Get-Content -Raw -LiteralPath (Join-Path $baseline 'OptiScaler.ini')) -match 'current-neurotic-settings') 'Reinstall reuses saved settings when the restored original INI is still unchanged'
RunEngine 'baseline-reinstall-remove' @('-Uninstall','-GameExecutable',(Join-Path $baseline 'FixtureGame.exe'),'-UninstallMode','RemoveSettings','-ConfirmUninstall') | Out-Null
Check ((HashFile (Join-Path $baseline 'OptiScaler.ini')) -eq $baselineIni) 'Retained-settings reinstall still restores the exact original INI'

$full=Fixture 'public-full-cleanup' $true
RunEngine 'full-install' @('-GameExecutable',(Join-Path $full 'FixtureGame.exe'),'-ProxyName','version.dll') | Out-Null
RunEngine 'full-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $full 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $full 'version.dll')) -and -not (Test-Path -LiteralPath (Join-Path $full 'NeuRotic')) -and
    -not (Test-Path -LiteralPath (Join-Path $full 'NeuRotic-backups')) -and (Test-Path -LiteralPath (Join-Path $full 'nvngx_dlssnr.dll'))) 'Full cleanup removes managed installer state and empty backup roots but preserves the private model'

$manual=Fixture 'public-manual-recovery' $true
Copy-Item -LiteralPath (Join-Path $PackageRoot 'payload\OptiScaler.dll') -Destination (Join-Path $manual 'dbghelp.dll')
RunEngine 'manual-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $manual 'FixtureGame.exe'),'-ManualProxyName','dbghelp.dll','-UninstallMode','KeepSettings','-ConfirmUninstall') | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $manual 'dbghelp.dll')) -and (Test-Path -LiteralPath (Join-Path $manual 'nvngx_dlssnr.dll'))) 'Manual recovery removes only the explicitly selected verified proxy and preserves the model'

Write-Output "PASS: public Setup, intelligent uninstall, exact recovery and atomic conflict flows. Evidence: $testRoot"
}

# Regression gates from public-package review. These use the actual CMD entry point.
$cancelled=Fixture 'cancel-uninstall'
RunEngine 'cancel-install' @('-GameExecutable',(Join-Path $cancelled 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
$cancelLauncher=Join-Path $cancelled 'Uninstall NeuRotic.cmd'
$before=Inventory $cancelled
RunCmd 'cancel-uninstall-menu' $cancelLauncher @() $true @('0','') | Out-Null
Check ((Inventory $cancelled) -eq $before) 'Cancelling uninstall preserves the launcher and game files'
RunCmd 'cancel-uninstall-confirmation' $cancelLauncher @('-UninstallMode','KeepSettings') $true @('NO','') | Out-Null
RunCmd 'installed-uninstall-preview' $cancelLauncher @('-UninstallMode','Full','-CheckOnly') $true @('') | Out-Null
Check ((Inventory $cancelled) -eq $before) 'Declined confirmation and installed preview preserve the launcher'

# Missing managed files can be repaired, then uninstalled through the original baseline.
Remove-Item -LiteralPath (Join-Path $cancelled 'dxgi.dll') -Force
RunEngine 'repair-missing-proxy' @('-GameExecutable',(Join-Path $cancelled 'FixtureGame.exe'),'-ExistingInstallAction','Repair') | Out-Null
Check ((HashFile (Join-Path $cancelled 'dxgi.dll')) -eq $candidateHash) 'Repair recreates a missing managed proxy'
RunEngine 'repair-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $cancelled 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $cancelled 'dxgi.dll'))) 'Repaired installation uninstalls to the original absent proxy'

$repeat=Fixture 'repeat-proxy'
RunEngine 'repeat-proxy-first' @('-GameExecutable',(Join-Path $repeat 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
foreach($name in @('winmm.dll','version.dll','dxgi.dll')) {
    RunEngine ('repeat-proxy-'+$name) @('-GameExecutable',(Join-Path $repeat 'FixtureGame.exe'),'-ProxyName',$name,'-ExistingInstallAction','ChangeProxy') | Out-Null
}
RunEngine 'repeat-proxy-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $repeat 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null
Check (-not @(Get-ChildItem -LiteralPath $repeat -File | Where-Object Name -in @('dxgi.dll','winmm.dll','version.dll')).Count) 'Repeated proxy changes and change-back restore the original baseline'

$unknown=Fixture 'preserve-untracked'
RunEngine 'unknown-install' @('-GameExecutable',(Join-Path $unknown 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
$unknownBackup=Backup $unknown
[IO.File]::WriteAllText((Join-Path $unknown 'NeuRotic\personal-notes.txt'),'user notes')
[IO.File]::WriteAllText((Join-Path $unknownBackup 'personal-backup.txt'),'user backup')
RunEngine 'unknown-full-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $unknown 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null
Check ((Get-Content -Raw -LiteralPath (Join-Path $unknown 'NeuRotic\personal-notes.txt')) -eq 'user notes' -and
    (Get-Content -Raw -LiteralPath (Join-Path $unknownBackup 'personal-backup.txt')) -eq 'user backup') 'Full cleanup preserves user-added files inside product and backup folders'

# Distinct legacy generations are represented by a historical proxy transition.
$legacy=Fixture 'legacy-migration'
RunEngine 'legacy-first' @('-GameExecutable',(Join-Path $legacy 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
RunEngine 'legacy-second' @('-GameExecutable',(Join-Path $legacy 'FixtureGame.exe'),'-ProxyName','winmm.dll','-ExistingInstallAction','ChangeProxy') | Out-Null
$legacyState=Join-Path $legacy 'NeuRotic\Installer\Current-Install.json'
Remove-Item -LiteralPath $legacyState -Force
RunEngine 'legacy-upgrade' @('-GameExecutable',(Join-Path $legacy 'FixtureGame.exe'),'-ProxyName','winmm.dll','-ExistingProxyAction','Replace') | Out-Null
$migrated=Get-Content -Raw -LiteralPath $legacyState|ConvertFrom-Json
Check ($migrated.restore_chain.Count -eq 3) 'Legacy upgrade retains all three generations'
RunEngine 'legacy-migrated-uninstall' @('-Uninstall','-GameExecutable',(Join-Path $legacy 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $legacy 'dxgi.dll')) -and -not (Test-Path -LiteralPath (Join-Path $legacy 'winmm.dll'))) 'Legacy history is imported in the correct uninstall order'

# Load only function definitions for deterministic failure injection. No product hooks or source rewrites.
$tokens=$null;$errors=$null
$engineAst=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
foreach($definition in $engineAst.EndBlock.Statements | Where-Object {$_ -is [Management.Automation.Language.FunctionDefinitionAst]}) {
    . ([scriptblock]::Create($definition.Extent.Text))
}
$allowedProxies=$supported
$GameDirectory=$null;$GameExecutable=$null;$UninstallMode='KeepSettings';$ConfirmUninstall=$true
$CheckOnly=$false;$Installed=$false;$RemovePrivateModel=$false;$LegacyManifest=$null;$ManualProxyName=$null
$tx=Fixture 'transaction-failure'
RunEngine 'tx-install-first' @('-GameExecutable',(Join-Path $tx 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
RunEngine 'tx-install-second' @('-GameExecutable',(Join-Path $tx 'FixtureGame.exe'),'-ExistingInstallAction','Update') | Out-Null
$txStatePath=Join-Path $tx 'NeuRotic\Installer\Current-Install.json'
$txState=Get-Content -Raw -LiteralPath $txStatePath|ConvertFrom-Json
$txChain=@($txState.restore_chain);[array]::Reverse($txChain)
$stateHash=HashFile $txStatePath
$iniHash=HashFile (Join-Path $tx 'OptiScaler.ini')
$olderBytes=[IO.File]::ReadAllBytes($txChain[1])
$older=Read-InstallRecord $txChain[1]
($older.files | Where-Object path -eq 'dxgi.dll').installed_hash='0'*64
SaveRecord $txChain[1] $older
RunEngine 'whole-chain-preflight-refusal' @('-Uninstall','-GameExecutable',(Join-Path $tx 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') $false | Out-Null
Check ((HashFile $txStatePath) -eq $stateHash -and (Read-InstallRecord $txChain[0]).status -eq 'installed-verified' -and
    -not (Test-Path -LiteralPath (Join-Path $tx 'NeuRotic\Installer\Uninstall-Transaction.json'))) 'Invalid older history fails preflight before the newest generation changes'
[IO.File]::WriteAllBytes($txChain[1],$olderBytes)
$saveRecordOriginal=${function:SaveRecord}
$script:failRecord=$txChain[1]
function SaveRecord([string]$Path,$Value) {
    if($Path -eq $script:failRecord -and $Value.status -eq 'restored'){throw 'Injected late record failure'}
    & $saveRecordOriginal $Path $Value
}
$GameDirectory=$tx;$failed=$false
try { Invoke-PublicUninstall | Out-Null } catch {$failed=$true} finally {Set-Item Function:SaveRecord $saveRecordOriginal}
Check ($failed -and (HashFile $txStatePath) -eq $stateHash -and (HashFile (Join-Path $tx 'dxgi.dll')) -eq $candidateHash -and
    (HashFile (Join-Path $tx 'OptiScaler.ini')) -eq $iniHash) 'Late uninstall failure restores runtime, settings and managed state across the whole chain'
foreach($path in $txChain){Check ((Read-InstallRecord $path).status -eq 'installed-verified') 'Rollback restores each installation record status'}

# Model an abrupt exit after a restore completed, then recover in a fresh PowerShell process.
$journal=New-UninstallTransaction $tx $txChain
[void](Invoke-RestoreRecord $txChain[0])
RunEngine 'interrupted-uninstall-recovery' @('-Uninstall','-GameExecutable',(Join-Path $tx 'FixtureGame.exe'),'-UninstallMode','KeepSettings','-ConfirmUninstall') | Out-Null
Check ((HashFile $txStatePath) -eq $stateHash -and (Read-InstallRecord $txChain[0]).status -eq 'installed-verified') 'A fresh process recovers an interrupted uninstall before retry'
RunEngine 'recovered-uninstall-retry' @('-Uninstall','-GameExecutable',(Join-Path $tx 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $tx 'dxgi.dll'))) 'Uninstall succeeds after recovery'

$cleanupFixture=Fixture 'cleanup-retry'
RunEngine 'cleanup-retry-install' @('-GameExecutable',(Join-Path $cleanupFixture 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
$removeOriginal=${function:Remove-OwnedFile}
function Remove-OwnedFile([string]$Root,[string]$Relative,[string]$ExpectedHash) {
    if($Relative -match '^NeuRotic-backups\\.*INSTALL-MANIFEST.json$'){throw 'Injected cleanup interruption'}
    & $removeOriginal $Root $Relative $ExpectedHash
}
$GameDirectory=$cleanupFixture;$UninstallMode='Full';$failed=$false
try {Invoke-PublicUninstall|Out-Null} catch {$failed=$true} finally {Set-Item Function:Remove-OwnedFile $removeOriginal}
Check ($failed -and -not (Test-Path -LiteralPath (Join-Path $cleanupFixture 'dxgi.dll')) -and
    (Test-Path -LiteralPath (Join-Path $cleanupFixture 'NeuRotic\Installer\Cleanup-Pending.json'))) 'Cleanup interruption retains a receipt after restoring the game'
RunEngine 'cleanup-resume' @('-Uninstall','-GameExecutable',(Join-Path $cleanupFixture 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') | Out-Null
Check (-not (Test-Path -LiteralPath (Join-Path $cleanupFixture 'NeuRotic')) -and -not (Test-Path -LiteralPath (Join-Path $cleanupFixture 'NeuRotic-backups'))) 'Cleanup resumes in a fresh process and removes only remaining recorded files'

$linked=Fixture 'linked-settings'
$outside=Fixture 'linked-settings-target'
RunEngine 'linked-settings-install' @('-GameExecutable',(Join-Path $linked 'FixtureGame.exe'),'-ProxyName','dxgi.dll') | Out-Null
[IO.File]::WriteAllText((Join-Path $outside 'OptiScaler.ini'),'user data outside game')
New-Item -ItemType Junction -Path (Join-Path $linked 'NeuRotic\UserData') -Value $outside | Out-Null
RunEngine 'linked-settings-refusal' @('-Uninstall','-GameExecutable',(Join-Path $linked 'FixtureGame.exe'),'-UninstallMode','Full','-ConfirmUninstall') $false | Out-Null
Check ((Get-Content -Raw -LiteralPath (Join-Path $outside 'OptiScaler.ini')) -eq 'user data outside game' -and
    (HashFile (Join-Path $linked 'dxgi.dll')) -eq $candidateHash) 'Linked settings paths are refused before uninstall or outside-file changes'

foreach($name in @('DirectX_LICENSE.txt','FidelityFX_v2_LICENSE.md','XeSS_LICENSE.txt','RenoDX_ATTRIBUTION.txt')) {
    Check (Test-Path -LiteralPath (Join-Path $PackageRoot ('payload\Licenses\'+$name)) -PathType Leaf) ('Redistribution license included: '+$name)
}
Write-Output "PASS: public package review regression gates. Evidence: $testRoot"
