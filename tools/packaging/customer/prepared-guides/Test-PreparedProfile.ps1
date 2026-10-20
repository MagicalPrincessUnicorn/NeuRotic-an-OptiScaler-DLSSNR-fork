param([string]$FixtureRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'PreparedProfile.ps1')
if(-not $FixtureRoot){throw 'Explicit disposable fixture required'}
$game=[IO.Path]::GetFullPath($FixtureRoot)
if(Test-Path -LiteralPath $game){throw 'Use a fresh fixture directory'}
New-Item -ItemType Directory $game|Out-Null
$assets=Join-Path $game 'OptiScaler/PreparedGuides';New-Item -ItemType Directory $assets -Force|Out-Null
foreach($name in @('Prepared.ini','BuiltIn.ini','Baseline.ini')){[IO.File]::WriteAllText((Join-Path $assets $name),'Techniques=')}
function Assert($condition,[string]$message){if(-not $condition){throw $message}}
function Refuses([scriptblock]$action){$refused=$false;try{& $action}catch{$refused=$true};Assert $refused 'Expected refusal'}
$ini=Join-Path $game 'OptiScaler.ini';$reshade=Join-Path $game 'ReShade.ini'
$old="; original`r`n[dlssnr]`r`nenabled=false`r`nRoute=1`r`n[User]`r`nKeep=hello`r`n"
[IO.File]::WriteAllText($ini,$old,[Text.UTF8Encoding]::new($false));$before=NrHash $ini
NrProfileApply $game 'Prepared' 'External'
Assert ((NrIniGet (NrRead $ini).text 'DlssNr' 'Enabled') -eq 'true') 'Case insensitive key update'
Assert ((NrIniGet (NrRead $ini).text 'DlssNr' 'InputSource') -eq '3') 'Prepared launch explicitly selects prepared consumer'
Assert ((NrIniGet (NrRead $ini).text 'DlssNr' 'InputTransport') -eq '1') 'Prepared launch explicitly selects GPU transport'
Assert ((NrIniGet (NrRead $reshade).text 'GENERAL' 'PreprocessorDefinitions') -eq 'NRPG_MV_PROVIDER=0') 'External flow provider'
Assert ((NrIniGet (NrRead $reshade).text 'ADDON' 'AddonPath') -eq $game.Replace(',',',,')) 'Addon discovered in game root'
$runtimePreset=(NrIniGet (NrRead $reshade).text 'GENERAL' 'PresetPath').Replace(',,',',')
[IO.File]::WriteAllText($runtimePreset,'User changes to this run')
NrProfileRestore $game
Assert ((NrHash $ini) -eq $before) 'Byte-exact restoration'
Assert (-not(Test-Path -LiteralPath $reshade)) 'Remove newly created configuration'
Assert (-not(Test-Path -LiteralPath $runtimePreset)) 'Temporary preset cleanup'
Assert ((Get-Content (Join-Path $assets 'Prepared.ini') -Raw) -eq 'Techniques=') 'Packaged preset remains unchanged'
$original="[GENERAL]`r`nPreprocessorDefinitions=RESHADE_DEPTH_INPUT_IS_REVERSED=1,USER=7`r`n[User]`r`nKeep=yes`r`n"
[IO.File]::WriteAllText($reshade,$original,[Text.UnicodeEncoding]::new($false,$true));$beforeRS=NrHash $reshade
NrProfileApply $game 'BuiltIn' 'BuiltIn'
Assert ((NrIniGet (NrRead $reshade).text 'GENERAL' 'PreprocessorDefinitions') -eq 'RESHADE_DEPTH_INPUT_IS_REVERSED=1,USER=7,NRPG_MV_PROVIDER=5') 'Preserve depth settings'
NrProfileRestore $game
Assert ((NrHash $reshade) -eq $beforeRS) 'UTF16 byte-exact restoration'
NrProfileApply $game 'Shared' 'External'
$doc=NrRead $ini;$changed=NrIniSet $doc.text 'User' 'Keep' 'edited';NrWrite $game 'OptiScaler.ini' ([byte[]]($doc.bom+$doc.encoding.GetBytes($changed))) (NrHash $ini)
NrProfileRestore $game
Assert ((NrIniGet (NrRead $ini).text 'User' 'Keep') -eq 'edited') 'Retain unrelated user edits'
Assert ((NrIniGet (NrRead $ini).text 'DlssNr' 'Enabled') -eq 'false') 'Restore owned values after user edits'
NrProfileApply $game 'TransportOnly' 'External'
Assert ((NrIniGet (NrRead $ini).text 'DlssNr' 'Enabled') -eq 'false') 'Transport-only disables model'
Assert ((NrIniGet (NrRead $reshade).text 'NeuRoticPreparedGuides' 'TransportOnly') -eq '1') 'Transport-only switch'
NrProfileRestore $game
# Simulate interruption after the first write: the journal also contains a
# second planned write which never occurred.
$before=NrHash $ini;$beforeRS=NrHash $reshade
NrProfileApply $game 'Prepared' 'External'
$state=Get-Content (Join-Path $game 'NeuRotic/prepared-test-profile.json') -Raw|ConvertFrom-Json
$rs=@($state.files|Where-Object path -eq 'ReShade.ini')[0]
NrWrite $game 'ReShade.ini' ([Convert]::FromBase64String($rs.original)) (NrHash $reshade)
NrProfileRestore $game
Assert ((NrHash $ini) -eq $before -and (NrHash $reshade) -eq $beforeRS) 'Interrupted apply recovery'
Refuses {NrIniGet "[GENERAL]`nKey=1`nkey=2`n" 'GENERAL' 'Key'}
Refuses {NrIniGet "[GENERAL]`nKey=1`n[general]`nKey=2`n" 'GENERAL' 'Key'}
Refuses {NrSafe $game '../outside.ini'}
Refuses {NrWrite $game 'OptiScaler.ini' ([byte[]]@(65)) ('0'*64)}
Assert ((NrHash $ini) -eq $before) 'Failed compare/write leaves source intact'
NrProfileApply $game 'Prepared' 'External' 'Vulkan' 'DetroitBecomeHuman.exe'
Assert ((NrIniGet (NrRead $ini).text 'Plugins' 'LoadReShade') -eq 'false') 'Vulkan must use one registered ReShade layer'
Assert ((NrIniGet (NrRead $ini).text 'ProcessFilter' 'TargetProcessName') -eq 'DetroitBecomeHuman.exe') 'Only selected game receives hooks'
NrProfileRestore $game
Assert ((NrHash $ini) -eq $before) 'Restore original process filter and plugin preference'
Write-Output 'PASS: profile restoration, absent config, UTF16, casing, user edits, depth definitions, interrupted apply, duplicates, path confinement and compare/write guards'
