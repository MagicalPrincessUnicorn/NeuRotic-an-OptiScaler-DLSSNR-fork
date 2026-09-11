[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PackageRoot,
      [string]$EvidenceRoot = 'C:\OptiScaler-NR-Dev\logs\neurotic-simple-installer')
$ErrorActionPreference='Stop'
$PackageRoot=(Resolve-Path -LiteralPath $PackageRoot).Path
$testRoot=Join-Path $EvidenceRoot ('installer-fixtures-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$engine=Join-Path $PackageRoot 'support\NeuRotic-Setup-Engine.ps1'
$launcher=Join-Path $PackageRoot 'NeuRotic-Setup.cmd'
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
    return (@(Get-ChildItem -LiteralPath $Folder -Recurse -File | Where-Object {$_.FullName -notmatch '\\NeuRotic-test-backups\\'} |
        Sort-Object FullName | ForEach-Object {$_.FullName.Substring($Folder.Length)+':'+(HashFile $_.FullName)}) -join "`n")
}
function Backup([string]$Folder){
    return @(Get-ChildItem -LiteralPath (Join-Path $Folder 'NeuRotic-test-backups') -Directory | Sort-Object CreationTime -Descending)[0].FullName
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
$candidateHash=HashFile (Join-Path $PackageRoot 'payload\OptiScaler.dll')

# No dxgi.dll: the actual customer launcher installs immediately with no INSTALL text.
$fresh=Fixture ('fresh unicode '+$unicode) $false
$freshBefore=Inventory $fresh
RunEngine 'fresh-preflight' @('-GameExecutable',(Join-Path $fresh 'FixtureGame.exe'),'-CheckOnly') | Out-Null
Check ((Inventory $fresh) -eq $freshBefore) 'Fresh check-only changes no game files'
$output=RunCmd 'fresh-actual-setup' $launcher @('-GameExecutable',(Join-Path $fresh 'FixtureGame.exe'))
Check ($output -notmatch 'Type INSTALL' -and $output -match 'No existing dxgi.dll: install immediately') 'Fresh Setup requires no typed INSTALL'
Check ((HashFile (Join-Path $fresh 'dxgi.dll')) -eq $candidateHash) 'Fresh Setup installs NeuRotic as dxgi.dll'
$backup=Backup $fresh;$record=Get-Content -Raw -LiteralPath (Join-Path $backup 'INSTALL-MANIFEST.json') | ConvertFrom-Json
Check ($record.existing_dxgi_action -eq 'None' -and -not $record.original_dxgi.existed) 'Fresh manifest records no original dxgi.dll'
Check (-not $record.original_ini.existed -and $null -eq $record.original_ini.bytes_base64) 'Fresh manifest records no original INI bytes'
[IO.File]::AppendAllText((Join-Path $fresh 'OptiScaler.ini'),"`r`n; test-session change")
$changedIni=HashFile (Join-Path $fresh 'OptiScaler.ini')
$restoreOutput=RunCmd 'fresh-actual-restore' (Join-Path $backup 'Restore.cmd') @()
Check ($restoreOutput -notmatch 'Type RESTORE' -and $restoreOutput -match 'exact pre-install files restored') 'Restore is one-click with no typed confirmation'
Check (-not (Test-Path -LiteralPath (Join-Path $fresh 'dxgi.dll')) -and -not (Test-Path -LiteralPath (Join-Path $fresh 'OptiScaler.ini'))) 'Fresh restore removes newly installed dxgi.dll and INI'
$undo=@(Get-ChildItem -LiteralPath $backup -Directory | Where-Object Name -Like 'restore-*')[0].FullName
Check ((HashFile (Join-Path $undo 'OptiScaler.ini')) -eq $changedIni) 'Restore retains the replaced test-session INI in its undo folder'

# Existing dxgi.dll delete: exact backup, no INI edit, changed runtime refusal, exact restore.
$delete=Fixture 'delete-existing'
[IO.File]::WriteAllText((Join-Path $delete 'dxgi.dll'),'original non-ReShade dxgi')
$deleteIni=Encoded ("[Plugins]`r`nLoadReshade = false`r`n; preserve me "+$unicode+"`r`n") 'utf8bom'
[IO.File]::WriteAllBytes((Join-Path $delete 'OptiScaler.ini'),$deleteIni)
$oldDxgi=HashFile (Join-Path $delete 'dxgi.dll');$oldIni=HashFile (Join-Path $delete 'OptiScaler.ini')
RunEngine 'delete-install' @('-GameExecutable',(Join-Path $delete 'FixtureGame.exe'),'-ExistingDxgiAction','Delete') | Out-Null
Check ((HashFile (Join-Path $delete 'dxgi.dll')) -eq $candidateHash) 'Delete choice installs candidate dxgi.dll'
Check ((HashFile (Join-Path $delete 'OptiScaler.ini')) -eq $oldIni) 'Delete choice does not enable LoadReshade or alter INI bytes'
$backup=Backup $delete;$record=Get-Content -Raw -LiteralPath (Join-Path $backup 'INSTALL-MANIFEST.json') | ConvertFrom-Json
Check ($record.existing_dxgi_action -eq 'Delete' -and $record.original_dxgi.name -eq 'dxgi.dll' -and $record.original_dxgi.sha256 -eq $oldDxgi) 'Delete manifest records original dxgi.dll name and hash'
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
    RunEngine ('rename-install-'+$kind) @('-GameExecutable',(Join-Path $folder 'FixtureGame.exe'),'-ExistingDxgiAction','Rename') | Out-Null
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
RunEngine 'rename-inserts-key' @('-GameExecutable',(Join-Path $missing 'FixtureGame.exe'),'-ExistingDxgiAction','Rename') | Out-Null
$installed=[Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes((Join-Path $missing 'OptiScaler.ini')),2,((Get-Item (Join-Path $missing 'OptiScaler.ini')).Length-2))
Check ($installed -match '(?m)^LoadReshade = true\r?$' -and $installed.IndexOf('LoadReshade') -lt $installed.IndexOf('[Next]')) 'Missing LoadReshade key is inserted into Plugins'
$backup=Backup $missing;RunCmd 'missing-key-restore' (Join-Path $backup 'Restore.cmd') @() | Out-Null
Check $(BytesEqual ([IO.File]::ReadAllBytes((Join-Path $missing 'OptiScaler.ini'))) $missingBytes) 'Missing-key restore returns exact original INI'

# Interactive text and cancel behavior; no automatic classification of dxgi.dll.
$cancel=Fixture 'interactive-cancel'
[IO.File]::WriteAllText((Join-Path $cancel 'dxgi.dll'),'unknown owner')
$before=Inventory $cancel
$output=RunEngine 'interactive-choice-cancel' @('-GameExecutable',(Join-Path $cancel 'FixtureGame.exe')) $true @('3')
Check ($output -match 'A dxgi.dll already exists in this game folder' -and $output -match 'Rename it to ReShade64.dll and keep it' -and $output -match 'Delete the existing dxgi.dll') 'Existing dxgi.dll displays the required explicit choice'
Check ((Inventory $cancel) -eq $before) 'Cancel changes no game files'

$interactiveRename=Fixture 'interactive-rename'
[IO.File]::WriteAllText((Join-Path $interactiveRename 'dxgi.dll'),'interactive ReShade')
[IO.File]::WriteAllText((Join-Path $interactiveRename 'OptiScaler.ini'),"[Plugins]`r`nLoadReshade = false`r`n")
RunEngine 'interactive-choice-rename' @('-GameExecutable',(Join-Path $interactiveRename 'FixtureGame.exe')) $true @('1') | Out-Null
Check ((HashFile (Join-Path $interactiveRename 'dxgi.dll')) -eq $candidateHash -and (Test-Path -LiteralPath (Join-Path $interactiveRename 'ReShade64.dll'))) 'Interactive choice 1 performs ReShade rename flow'

$interactiveDelete=Fixture 'interactive-delete'
[IO.File]::WriteAllText((Join-Path $interactiveDelete 'dxgi.dll'),'interactive delete')
RunEngine 'interactive-choice-delete' @('-GameExecutable',(Join-Path $interactiveDelete 'FixtureGame.exe')) $true @('2') | Out-Null
Check ((HashFile (Join-Path $interactiveDelete 'dxgi.dll')) -eq $candidateHash -and -not (Test-Path -LiteralPath (Join-Path $interactiveDelete 'ReShade64.dll'))) 'Interactive choice 2 performs delete flow'

$conflict=Fixture 'reshade64-conflict'
[IO.File]::WriteAllText((Join-Path $conflict 'dxgi.dll'),'existing dxgi')
[IO.File]::WriteAllText((Join-Path $conflict 'ReShade64.dll'),'existing reshade64')
$before=Inventory $conflict
RunEngine 'reshade64-conflict' @('-GameExecutable',(Join-Path $conflict 'FixtureGame.exe'),'-ExistingDxgiAction','Rename') $false | Out-Null
Check ((Inventory $conflict) -eq $before) 'Rename fails closed when ReShade64.dll exists'

$wrongAction=Fixture 'action-without-dxgi'
$before=Inventory $wrongAction
RunEngine 'action-without-dxgi' @('-GameExecutable',(Join-Path $wrongAction 'FixtureGame.exe'),'-ExistingDxgiAction','Delete') $false | Out-Null
RunEngine 'non-dxgi-proxy-refused' @('-GameExecutable',(Join-Path $wrongAction 'FixtureGame.exe'),'-ProxyName','winmm.dll') $false | Out-Null
Check ((Inventory $wrongAction) -eq $before) 'Invalid action and non-dxgi proxy fail before writes'

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
try{RunEngine 'forced-copy-failure' @('-GameExecutable',(Join-Path $rollback 'FixtureGame.exe'),'-ExistingDxgiAction','Rename') $false | Out-Null}
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
RunEngine 'other-optiscaler-proxy' @('-GameExecutable',(Join-Path $other 'FixtureGame.exe')) $false | Out-Null
Check ((Inventory $other) -eq $before) 'Existing OptiScaler under another proxy fails before writes'

$tampered=Join-Path $testRoot 'tampered-package'
Copy-Item -LiteralPath $PackageRoot -Destination $tampered -Recurse
[IO.File]::AppendAllText((Join-Path $tampered 'payload\OptiScaler.ini'),'; tampered')
$tamperedEngine=Join-Path $tampered 'support\NeuRotic-Setup-Engine.ps1'
$oldEngine=$engine;$engine=$tamperedEngine
$before=Inventory $wrongAction
RunEngine 'tampered-package' @('-GameExecutable',(Join-Path $wrongAction 'FixtureGame.exe')) $false | Out-Null
$engine=$oldEngine
Check ((Inventory $wrongAction) -eq $before) 'Tampered package fails before destination writes'

Write-Output "PASS: simple customer installer, exact recovery and atomic conflict flows. Evidence: $testRoot"
