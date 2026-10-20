[CmdletBinding()]
param(
    [string]$GameDirectory = "C:\Program Files (x86)\Steam\steamapps\common\No Man's Sky\Binaries",
    [string]$OutputDirectory,
    [int]$TargetProcessId = 0
)
$ErrorActionPreference = 'Stop'
$game = (Get-Item -LiteralPath $GameDirectory).FullName
if (-not (Test-Path -LiteralPath (Join-Path $game 'NMS.exe') -PathType Leaf)) {
    throw "NMS.exe was not found in $game"
}
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot 'reports' }
$name = 'NMS-loading-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$output = Join-Path $OutputDirectory $name
if (Test-Path -LiteralPath $output) { throw "Report already exists: $output" }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$expected = '110A7B1260823A35FD185D12AB65DB298D279F08135C0276113EEB807C6DAB09'
$files = @()
foreach ($file in @(Get-ChildItem -LiteralPath $game -File)) {
    if ($file.Name -notmatch '^(NMS\.exe|OptiScaler.*|dbghelp.*|winmm.*|dxgi.*|version.*|winhttp.*|wininet.*|nvngx.*|vulkan-1.*|sl\..*|.*[Nn]eu[Rr]otic.*)$') { continue }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    $files += [pscustomobject]@{name=$file.Name;bytes=$file.Length;sha256=$hash;
        experimental_product_match=($hash -eq $expected);last_write_utc=$file.LastWriteTimeUtc.ToString('o')}
}
$ini = Join-Path $game 'OptiScaler.ini'
if (Test-Path -LiteralPath $ini -PathType Leaf) {
    Copy-Item -LiteralPath $ini -Destination (Join-Path $output 'OptiScaler.ini')
}
$processes = if ($TargetProcessId -ne 0) {
    @(Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue)
} else { @(Get-Process -Name NMS -ErrorAction SilentlyContinue) }
$observations = @()
foreach ($process in $processes) {
    $modules = @()
    $moduleError = $null
    $processPath = $null
    try { $processPath = $process.MainModule.FileName } catch { $moduleError = $_.Exception.Message }
    try {
        foreach ($module in $process.Modules) {
            $modules += [pscustomobject]@{name=$module.ModuleName;path=$module.FileName}
        }
    } catch { $moduleError = $_.Exception.Message }
    $observations += [pscustomobject]@{id=$process.Id;name=$process.ProcessName;path=$processPath;
        module_enumeration_error=$moduleError;modules=$modules}
}
$report = [ordered]@{
    kind='NMS_LOADING_OBSERVATION';schema_version=1;collected_utc=[DateTime]::UtcNow.ToString('o');
    game_directory=$game;expected_experimental_product_sha256=$expected;
    installed_files=$files;running_processes=$observations;
    game_launched_by_check=$false;game_files_changed=$false;provider_loaded_by_check=$false
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'LOADING.json') -Encoding UTF8
$installedMatches = @($files | Where-Object experimental_product_match)
$loadedLocal = @($observations | ForEach-Object { $_.modules } | Where-Object {
    $_.path -and ([IO.Path]::GetDirectoryName($_.path).TrimEnd('\') -ieq $game.TrimEnd('\'))
})
$lines = @(
    "Game folder: $game"
    "NMS processes observed: $($processes.Count)"
    "Files matching the experimental product: $(($installedMatches.name -join ', '))"
    "Game-folder modules loaded: $(($loadedLocal.name -join ', '))"
    'This check observes files and loaded module paths. It does not launch the game, load a provider, or change game files.'
)
if ($processes.Count -eq 0) { $lines += 'NMS was not running. Leave it at the main menu and run this check again.' }
foreach ($observation in $observations) {
    if ($observation.module_enumeration_error) { $lines += "Module read failed: $($observation.module_enumeration_error)" }
}
$lines | Set-Content -LiteralPath (Join-Path $output 'SUMMARY.txt') -Encoding UTF8
$archive = $output + '.zip'
Compress-Archive -LiteralPath $output -DestinationPath $archive
$lines | ForEach-Object { Write-Host $_ }
Write-Host "Send this report: $archive"
