param([string]$PackageRoot,[string]$GameRoot,[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
if(-not $PackageRoot -or -not $GameRoot -or -not $EvidenceRoot){throw 'Explicit package, fresh fixture and evidence roots required'}
if(Test-Path -LiteralPath $GameRoot){throw 'Use a fresh game fixture'}
New-Item -ItemType Directory $GameRoot,$EvidenceRoot -Force|Out-Null
$exe=Join-Path $GameRoot 'NrInstallerFixture.exe';Copy-Item -LiteralPath "$env:SystemRoot/System32/cmd.exe" -Destination $exe
$saved=@{};foreach($name in @('ReShade64.dll','NeuRotic-PreparedGuides.addon64','WinPixEventRuntime.dll','OptiScaler.ini','nvngx_dlssnr.dll')){
    $path=Join-Path $GameRoot $name;[IO.File]::WriteAllText($path,"Preexisting fixture: $name",[Text.UTF8Encoding]::new($false));$saved[$name]=(Get-FileHash $path).Hash
}
function Assert($condition,[string]$message){if(-not $condition){throw $message}}
function Run([string]$label,[string]$script,$arguments){
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script @arguments *> (Join-Path $EvidenceRoot ($label+'.log'))
    if($LASTEXITCODE -ne 0){throw "$label failed; inspect its log"}
}
$setup=Join-Path $PackageRoot 'support/PreparedSetup.ps1';$engine=Join-Path $PackageRoot 'packages/current/support/NeuRotic-Setup-Engine.ps1'
Run 'setup-check' $setup @('-Action','Setup','-GameExecutable',$exe,'-ProxyName','winmm.dll','-CheckOnly','-ContinueWithoutRuntime')
Assert (-not(Test-Path (Join-Path $GameRoot 'winmm.dll'))) 'Check-only mutation'
Run 'setup-install' $setup @('-Action','Setup','-GameExecutable',$exe,'-ProxyName','winmm.dll','-ConfirmInstall','-ContinueWithoutRuntime')
Assert (Test-Path (Join-Path $PackageRoot 'prepared-game.local.json')) 'Setup remembers selection'
$manifest=Get-Content (Join-Path $PackageRoot 'packages/current/support/PACKAGE-MANIFEST.json') -Raw|ConvertFrom-Json
foreach($name in @('ReShade64.dll','NeuRotic-PreparedGuides.addon64','WinPixEventRuntime.dll')){
    $row=@($manifest.files|Where-Object path -eq ('payload\'+$name))[0];Assert ((Get-FileHash (Join-Path $GameRoot $name)).Hash -ieq $row.sha256) "Installed identity: $name"
}
Assert ((Get-FileHash (Join-Path $GameRoot 'OptiScaler.ini')).Hash -eq $saved['OptiScaler.ini']) 'Preserve existing config'
$launch=Join-Path $PackageRoot 'support/Start-PreparedTest.ps1'
foreach($mode in @('Prepared','Baseline','BuiltIn','Shared','TransportOnly','VulkanPrepared','VulkanBaseline')){Run ('launcher-'+$mode) $launch @(('Check'+$mode),$exe)}
. (Join-Path $PackageRoot 'support/PreparedProfile.ps1')
NrProfileApply $GameRoot 'Prepared' 'External' 'Vulkan' ([IO.Path]::GetFileName($exe))
Assert ((NrIniGet (NrRead (Join-Path $GameRoot 'OptiScaler.ini')).text 'Plugins' 'LoadReShade') -eq 'false') 'Packaged Vulkan profile prevents duplicate ReShade loading'
Assert ((NrIniGet (NrRead (Join-Path $GameRoot 'OptiScaler.ini')).text 'ProcessFilter' 'TargetProcessName') -eq 'NrInstallerFixture.exe') 'Packaged profile limits process hooks'
NrProfileRestore $GameRoot
Assert ((Get-FileHash (Join-Path $GameRoot 'OptiScaler.ini')).Hash -eq $saved['OptiScaler.ini']) 'Packaged Vulkan profile restores config'
Run 'repair' $engine @('-GameExecutable',$exe,'-ProxyName','winmm.dll','-ExistingInstallAction','Repair','-ConfirmInstall','-ContinueWithoutRuntime')
Run 'update' $engine @('-GameExecutable',$exe,'-ProxyName','winmm.dll','-ExistingInstallAction','Update','-ConfirmInstall','-ContinueWithoutRuntime')
Run 'reshade-check' $setup @('-Action','ReShade','-GameExecutable',$exe,'-CheckOnly')
Run 'profile-recovery-check' $setup @('-Action','Restore','-GameExecutable',$exe)
Run 'full-uninstall' $setup @('-Action','Uninstall','-GameExecutable',$exe,'-ConfirmUninstall')
Assert (-not(Test-Path (Join-Path $GameRoot 'winmm.dll'))) 'Remove managed proxy'
Assert (-not(Test-Path (Join-Path $GameRoot 'OptiScaler/PreparedGuides'))) 'Remove managed guides'
foreach($name in $saved.Keys){Assert ((Get-FileHash (Join-Path $GameRoot $name)).Hash -eq $saved[$name]) "Restore preexisting file: $name"}
Write-Output 'PASS: full setup/check-only, all seven launcher preflights, Vulkan profile, repair/update, recovery, full uninstall and original-file restoration; no game or ReShade installer executed'
