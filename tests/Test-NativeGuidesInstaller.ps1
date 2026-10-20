[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ArchiveRoot,[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$ArchiveRoot=(Resolve-Path -LiteralPath $ArchiveRoot).Path
$package=Join-Path $ArchiveRoot 'packages/current'
$engine=Join-Path $package 'support/NeuRotic-Setup-Engine.ps1'
$fixture=Join-Path $repo 'builds/native-installer-fixture'
$evidence=if($EvidenceRoot){[IO.Path]::GetFullPath($EvidenceRoot)}else{Join-Path $repo 'docs/implementation-evidence/native-guides-20261004/installer'}
if(Test-Path -LiteralPath $fixture){throw 'Remove the completed native fixture after preserving its logs before repeating this test.'}
New-Item -ItemType Directory -Path $fixture,$evidence -Force | Out-Null
function Check([bool]$Value,[string]$Message){if(-not $Value){throw $Message};Write-Output "PASS: $Message"}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function Inventory(){(@(Get-ChildItem -LiteralPath $fixture -Recurse -File | Sort-Object FullName | ForEach-Object {$_.FullName.Substring($fixture.Length)+':'+(Hash $_.FullName)}) -join "`n")}
function Run([string]$Name,[string[]]$Arguments){
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$output=& "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $engine @Arguments 2>&1;$code=$LASTEXITCODE}
    finally{$ErrorActionPreference=$old}
    $output | Out-File -LiteralPath (Join-Path $evidence ($Name+'.log')) -Encoding UTF8
    Check ($code -eq 0) "$Name exit zero"
}
$env:PSModulePath=''
$manifest=Get-Content -Raw -LiteralPath (Join-Path $ArchiveRoot 'ARCHIVE-MANIFEST.json') | ConvertFrom-Json
foreach($row in $manifest.files){Check ((Hash (Join-Path $ArchiveRoot $row.path)) -eq $row.sha256) ('Archive hash '+$row.path)}
$payload=Join-Path $package 'payload'
Check (-not (Test-Path -LiteralPath (Join-Path $payload 'ReShade64.dll'))) 'No ReShade runtime installed'
Check (-not (Test-Path -LiteralPath (Join-Path $payload 'NeuRotic-PreparedGuides.addon64'))) 'No external prepared addon installed'
Check (-not (Test-Path -LiteralPath (Join-Path $payload 'OptiScaler/PreparedGuides'))) 'No external shaders or helper required'
Check (-not (Test-Path -LiteralPath (Join-Path $ArchiveRoot 'Start-Vulkan-Prepared-Test.cmd'))) 'No prepared launcher'
$build=Get-Content -Raw -LiteralPath (Join-Path $package 'support/BUILD-MANIFEST.json') | ConvertFrom-Json
Check ($build.requires_reshade -eq $false -and $build.game_qualified -eq $false) 'Native candidate provenance is explicit'
$game=Join-Path $fixture 'FixtureGame.exe'
Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination $game
$model=Join-Path $fixture 'nvngx_dlssnr.dll'
$reshade=Join-Path $fixture 'ReShade64.dll'
$ini=Join-Path $fixture 'OptiScaler.ini'
[IO.File]::WriteAllText($model,'private model fixture - must survive')
[IO.File]::WriteAllText($reshade,'foreign ReShade fixture - must survive')
[IO.File]::WriteAllText($ini,"[DlssNr]`r`nNativeGuides=true`r`nNativeDepthDirection=0`r`n; user settings must survive`r`n")
$before=Inventory
$modelHash=Hash $model;$reshadeHash=Hash $reshade;$iniHash=Hash $ini
Run 'check-only' @('-GameExecutable',$game,'-ProxyName','winmm.dll','-CheckOnly')
Check ((Inventory) -eq $before) 'Check-only preserves game files'
Run 'install' @('-GameExecutable',$game,'-ProxyName','winmm.dll')
$proxy=Join-Path $fixture 'winmm.dll';$coreHash=Hash (Join-Path $payload 'OptiScaler.dll')
Check ((Hash $proxy) -eq $coreHash) 'Native core installed under Vulkan loader proxy'
$fgPayload=Join-Path $payload 'OptiScaler/NeuRotic.Fsr3.Vulkan.dll'
$fgInstalled=Join-Path $fixture 'OptiScaler/NeuRotic.Fsr3.Vulkan.dll'
if(Test-Path -LiteralPath $fgPayload){Check ((Hash $fgInstalled) -eq (Hash $fgPayload)) 'Standalone Vulkan FG component installed with matching hash'}
Check ((Hash $ini) -eq $iniHash -and (Hash $model) -eq $modelHash -and (Hash $reshade) -eq $reshadeHash) 'Settings, private model and foreign ReShade preserved'
Run 'update' @('-GameExecutable',$game,'-ExistingInstallAction','Update')
Check ((Hash $ini) -eq $iniHash -and (Hash $proxy) -eq $coreHash) 'Update preserves native selection and core'
Remove-Item -LiteralPath $proxy
Run 'repair' @('-GameExecutable',$game,'-ExistingInstallAction','Repair')
Check ((Hash $proxy) -eq $coreHash) 'Repair restores native core'
Run 'uninstall' @('-GameExecutable',$game,'-Uninstall','-UninstallMode','KeepSettings','-ConfirmUninstall')
Check (-not (Test-Path -LiteralPath $proxy)) 'Uninstall removes owned core'
if(Test-Path -LiteralPath $fgPayload){Check (-not (Test-Path -LiteralPath $fgInstalled)) 'Uninstall removes owned FG component'}
Check ((Hash $ini) -eq $iniHash -and (Hash $model) -eq $modelHash -and (Hash $reshade) -eq $reshadeHash) 'Uninstall keeps selected settings, private model and foreign ReShade'
Write-Output 'PASS: native installer lifecycle (fixture only; game not executed)'
