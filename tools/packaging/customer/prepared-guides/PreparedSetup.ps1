[CmdletBinding()]
param([ValidateSet('Setup','Uninstall','Restore','ReShade')][string]$Action='Setup',[string]$GameExecutable,[string]$ProxyName,[switch]$CheckOnly,[switch]$ConfirmInstall,[switch]$ConfirmUninstall,[switch]$ContinueWithoutRuntime)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'PreparedProfile.ps1')
$root=Split-Path $PSScriptRoot -Parent
$last=Join-Path $root 'prepared-game.local.json'
if(-not $GameExecutable){
    if($Action -ne 'Setup' -and (Test-Path -LiteralPath $last)){$GameExecutable=(Get-Content -LiteralPath $last -Raw|ConvertFrom-Json).gameExecutable}
    if(-not $GameExecutable){Add-Type -AssemblyName System.Windows.Forms;$picker=New-Object Windows.Forms.OpenFileDialog;$picker.Title='Select the game executable for this experimental test';$picker.Filter='Game (*.exe)|*.exe';if($picker.ShowDialog() -ne 'OK'){return};$GameExecutable=$picker.FileName}
}
$exe=(Resolve-Path -LiteralPath $GameExecutable).Path;$game=Split-Path $exe -Parent;$null=NrSafe $game ([IO.Path]::GetFileName($exe))
if([IO.Path]::GetExtension($exe) -ine '.exe'){throw 'Select the game EXE'}
if(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue){throw 'Close the game before setup or recovery'}
if($Action -eq 'Restore'){if(-not $CheckOnly){NrProfileRestore $game};Write-Output 'Test profile recovery complete';return}
if($Action -eq 'ReShade'){
    $installer=Join-Path $root 'Optional\ReShade\ReShade_Setup_6.8.0_Addon.exe'
    if($CheckOnly){Write-Output 'Official ReShade setup is available; no installer executed';return}
    Write-Host 'In the official ReShade window, choose this game and its graphics API (Detroit: Vulkan).'
    Write-Host 'Use this for Vulkan layer registration. Then return to the prepared launch button.'
    & $installer $exe;return
}
if($Action -eq 'Uninstall' -and -not $CheckOnly){NrProfileRestore $game}
$engine=Join-Path $root 'packages\current\support\NeuRotic-Setup-Engine.ps1'
$parameters=@{GameExecutable=$exe};if($ProxyName){$parameters.ProxyName=$ProxyName};if($CheckOnly){$parameters.CheckOnly=$true};if($ContinueWithoutRuntime){$parameters.ContinueWithoutRuntime=$true}
if($Action -eq 'Uninstall'){$parameters.Uninstall=$true;$parameters.UninstallMode='Full';if($ConfirmUninstall){$parameters.ConfirmUninstall=$true}}
elseif($ConfirmInstall){$parameters.ConfirmInstall=$true}
$global:LASTEXITCODE=0
& $engine @parameters
if($LASTEXITCODE -and $LASTEXITCODE -ne 0){throw 'Installer did not complete'}
if($Action -eq 'Setup' -and -not $CheckOnly){
    [IO.File]::WriteAllText($last,(@{gameExecutable=$exe}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    Write-Host 'Installed. Detroit/Vulkan: run ReShade-Setup.cmd, then Start-Vulkan-Prepared-Test.cmd.'
    Write-Host 'The NVIDIA NR model must be beside the EXE as nvngx_dlssnr.dll.'
}
