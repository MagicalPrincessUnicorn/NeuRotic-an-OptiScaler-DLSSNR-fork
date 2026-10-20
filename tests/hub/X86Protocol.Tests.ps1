$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine=Join-Path $repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Setup engine parse errors'}
foreach($function in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)){
    Invoke-Expression $function.Extent.Text
}
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-HubProtocol.ps1')
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-HubSettings.ps1')
function Assert($ok,$message){if(-not $ok){throw $message};Write-Output ('PASS '+$message)}
$work=Join-Path $repo 'builds/universal-connections/x86-protocol'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$x86=Join-Path $work 'Portal2Fixture.exe';$x64=Join-Path $work 'ModernFixture.exe'
Copy-Item -LiteralPath "$env:SystemRoot/SysWOW64/cmd.exe" -Destination $x86 -Force
Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination $x64 -Force
$target=Get-HubTarget $x86
Assert ($target.bitness -eq 32 -and $target.installationRoute -eq 'prepared-x86') 'actual PE32 target selects prepared x86 route'
Assert ((Get-HubTarget $x64).bitness -eq 64) 'actual PE32+ target retains x64 route'
$bad=Join-Path $work 'WrongMachine.exe'
$raw=[IO.File]::ReadAllBytes($x86);$pe=[BitConverter]::ToInt32($raw,60);$raw[$pe+4]=0x64;$raw[$pe+5]=0x86
[IO.File]::WriteAllBytes($bad,$raw)
try{[void](Get-HubTarget $bad);throw 'accepted machine-magic mismatch'}catch{Assert ($_.Exception.Message -ne 'accepted machine-magic mismatch') 'mismatched architecture and optional header refused'}
$raw=[IO.File]::ReadAllBytes($x86);$pe=[BitConverter]::ToInt32($raw,60);$raw[$pe+23]=$raw[$pe+23] -bor 0x20
[IO.File]::WriteAllBytes($bad,$raw)
try{[void](Get-HubTarget $bad);throw 'accepted DLL'}catch{Assert ($_.Exception.Message -ne 'accepted DLL') 'DLL renamed EXE refused'}
$helper=Join-Path $work 'NeuRotic/Prepared/NeuRotic.GpuHost'
New-Item -ItemType Directory -Path $helper -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $helper 'OptiScaler.ini'),"[DlssNr]`r`nEnabled=true`r`nRoute=2`r`nInputSource=3`r`nInputTransport=1`r`n")
[IO.File]::WriteAllText((Join-Path $work 'OptiScaler.ini'),"[DlssNr]`r`nEnabled=false`r`n")
$inspection=Get-HubInspection $target -CurrentOnly
Assert ($inspection.configRevision -eq (HashFile (Join-Path $helper 'OptiScaler.ini'))) 'x86 config revision binds helper config rather than game root'
$enabled=@($inspection.settings | Where-Object {$_.section -eq 'DlssNr' -and $_.key -eq 'Enabled'})
Assert ($enabled.Count -eq 1 -and $enabled[0].value -eq 'true') 'x86 settings project actual renderer helper configuration'
$projection=@($inspection.settings | Where-Object {$_.section -eq 'FrameGen' -and $_.key -eq 'Enabled'})
Assert ($projection.Count -eq 1 -and -not $projection[0].available) 'x86 helper does not advertise game frame generation controls'
$before=(HashFile (Join-Path $work 'OptiScaler.ini'))
$bytes=Get-HubSettingsBytes (Get-HubSettingsRoot $target) @([pscustomobject]@{section='DlssNr';key='Enabled';value='false'})
Assert ([Text.Encoding]::UTF8.GetString($bytes) -match 'Enabled=false') 'x86 draft edits renderer helper settings'
Assert ((HashFile (Join-Path $work 'OptiScaler.ini')) -eq $before) 'game root config remains untouched by helper draft'
Write-Output 'PASS x86 executable route, malformed PE rejection, helper settings projection and draft'
