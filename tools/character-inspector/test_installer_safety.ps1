# Exercise real installer helpers without launching setup or touching a game.
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$checkout=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$engine=Join-Path $checkout 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$null,[ref]$null)
foreach ($definition in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]},$true)) {
    Invoke-Expression $definition.Extent.Text
}
$fixture='C:\NeuRotic\_ci-installer-safety'
if (Test-Path -LiteralPath $fixture) { throw 'Safety fixture already exists.' }
New-Item -ItemType Directory -Path $fixture | Out-Null
$link=Join-Path $fixture 'linked'
try {
    $gameRoot=Join-Path $fixture 'Game'
    New-Item -ItemType Directory -Path $gameRoot | Out-Null
    $target=SafePath $gameRoot 'missing\nested\file.bin'
    if ($target -ne (Join-Path $gameRoot 'missing\nested\file.bin')) { throw 'Missing path changed.' }
    foreach ($relative in @('..\outside','a\..\outside','.\file','a\\file','C:\outside','file:stream','file*','file?','')) {
        $rejected=$false
        try { [void](SafePath $gameRoot $relative) } catch { $rejected=$true }
        if (-not $rejected) { throw "Unsafe path accepted: $relative" }
    }
    New-Item -ItemType Junction -Path $link -Target $gameRoot | Out-Null
    $rejected=$false
    try { [void](SafePath $fixture 'linked\absent\file.bin') } catch { $rejected=$true }
    if (-not $rejected) { throw 'Junction ancestor accepted.' }
    $rejected=$false
    try { [void](SafePath $link 'file.bin') } catch { $rejected=$true }
    if (-not $rejected) { throw 'Junction root accepted.' }
    # Unlink only the junction, never its target tree.
    [IO.Directory]::Delete($link)
    Write-Output 'PASS relative path and missing/junction ancestor checks'

    $exe=Join-Path $gameRoot 'FixtureGame.exe'
    $ini=Join-Path $gameRoot 'OptiScaler.ini'
    $proxy=Join-Path $gameRoot 'dxgi.dll'
    [IO.File]::WriteAllText($ini,'PREVIEW SETTINGS')
    [IO.File]::WriteAllText($proxy,'LOADER')
    $backup=SafePath $gameRoot 'NeuRotic-backups\install-safety'
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $recordPath=Join-Path $backup 'INSTALL-MANIFEST.json'
    $record=[pscustomobject]@{kind='neurotic-customer-candidate-install';status='installed-verified';game_executable=$exe;backup=$backup;selected_proxy='dxgi.dll';files=@(
        [pscustomobject]@{path='dxgi.dll';existed=$false;installed_hash=(HashFile $proxy)},
        [pscustomobject]@{path='OptiScaler.ini';existed=$false;installed_hash=(HashFile $ini)})}
    SaveRecord $recordPath $record
    SaveRecord (SafePath $gameRoot 'NeuRotic\Installer\Current-Install.json') ([pscustomobject]@{
        kind='neurotic-public-install';schema_version=2;status='installed-verified';game_directory=$gameRoot;
        game_executable=$exe;restore_chain=@($recordPath);last_install_utc='test';uninstalled_utc=$null;uninstall_mode=$null})
    # Keep the actual snapshot, preservation, live rechecks and cleanup helpers.
    # Only the UI/restore seam is isolated; the race occurs at transaction entry.
    $transaction=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'New-UninstallTransaction'},$true)
    Invoke-Expression ($transaction.Extent.Text.Replace('function New-UninstallTransaction','function Original-UninstallTransaction'))
    function New-UninstallTransaction([string]$GameRoot,[string[]]$Chain,[string[]]$AdditionalPaths=@()) {
        [IO.File]::WriteAllText((Join-Path $GameRoot 'OptiScaler.ini'),'LATEST SETTINGS AFTER PREVIEW')
        Original-UninstallTransaction $GameRoot $Chain $AdditionalPaths
    }
    function Get-GameForUninstall { return @{Root=$gameRoot;Executable=$exe} }
    function CheckGameClosed([string]$Executable) {}
    function Test-RestoreChain([string]$GameRoot,[string[]]$Chain,[switch]$ReportLiveFiles) {
        foreach ($name in @('dxgi.dll','OptiScaler.ini')) { [pscustomobject]@{path=$name;existed=$true;changed=$false;sha256=(HashFile (Join-Path $GameRoot $name))} }
    }
    function Get-RelocatedProxies([string]$GameRoot,[string[]]$Chain) {}
    function Invoke-RestoreRecord([string]$RecordPath,[switch]$AllowChangedFiles,$ExpectedFiles=@()) {
        Remove-Item -LiteralPath $ini,$proxy -Force
        return $record
    }
    $UninstallMode='KeepSettings'; $ConfirmUninstall=$true; $CheckOnly=$false
    $RemovePrivateModel=$false; $Installed=$false; $LegacyManifest=$null
    $allowedProxies=@('dxgi.dll','winmm.dll','version.dll','dbghelp.dll','d3d12.dll','wininet.dll','winhttp.dll','OptiScaler.asi','OptiScaler.dll')
    Invoke-PublicUninstall
    $saved=SafePath $gameRoot 'NeuRotic\UserData\OptiScaler.ini'
    if ([IO.File]::ReadAllText($saved) -ne 'LATEST SETTINGS AFTER PREVIEW') { throw 'KeepSettings lost an edit made after preview.' }
    if (Test-Path -LiteralPath (SafePath $gameRoot 'NeuRotic\Installer\Uninstall-Transaction.json')) { throw 'Journal not cleaned.' }
    Write-Output 'PASS KeepSettings retains the transaction preimage after a preview-time edit'
} finally {
    if (Test-Path -LiteralPath $link) { [IO.Directory]::Delete($link) }
    if ([IO.Path]::GetFullPath($fixture) -ne 'C:\NeuRotic\_ci-installer-safety') { throw 'Unexpected cleanup root.' }
    foreach ($item in Get-ChildItem -LiteralPath $fixture -Force -Recurse) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unexpected link in cleanup.' }
    }
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
