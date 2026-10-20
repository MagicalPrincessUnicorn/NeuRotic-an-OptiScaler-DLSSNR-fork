[CmdletBinding()]
param([string]$Collector)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $Collector) { $Collector=Join-Path $repo 'tools\packaging\customer\support\Collect-NR-Review.ps1' }
$root = Join-Path $repo 'builds\native-mfg\collector-test'
$allowed = [IO.Path]::GetFullPath((Join-Path $repo 'builds\native-mfg')) + '\'
if (-not ([IO.Path]::GetFullPath($root) + '\').StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Fixture path is outside the expected build directory.'
}
if (Test-Path -LiteralPath $root) { throw 'Collector fixture already exists; inspect it before rerunning.' }
$game = Join-Path $root 'game'
$nested = Join-Path $game 'Engine\Binaries\ThirdParty\NVIDIA\Streamline\Win64'
$out = Join-Path $root 'review'
New-Item -ItemType Directory -Path $nested -Force | Out-Null
try {
    [IO.File]::WriteAllText((Join-Path $game 'OptiScaler.log'),'NR MFG requested=3 forwarded=3')
    [IO.File]::WriteAllText((Join-Path $game 'OptiScaler.ini'),"[DLSSG]`nNativeMfgExperimental = true`nOverrideInterpolationCount = 3")
    [IO.File]::WriteAllText((Join-Path $nested 'nvngx_dlssg.dll'),'provider fixture')
    [IO.File]::WriteAllText((Join-Path $nested 'sl.dlss_g.dll'),'wrapper fixture')
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -GameDirectory $game -OutputDirectory $out | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Collector failed.' }
    if (@(Get-ChildItem -LiteralPath $out -Directory).Count) { throw 'Successful collection left its uncompressed staging folder.' }
    $zips = @(Get-ChildItem -LiteralPath $out -File -Filter '*.zip')
    if ($zips.Count -ne 1) { throw 'Review ZIP missing.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($zips[0].FullName)
    try {
        $entry = @($zip.Entries | Where-Object { $_.Name -eq 'COLLECTION.json' })
        if ($entry.Count -ne 1) { throw 'Expected one collection report.' }
        $reader = [IO.StreamReader]::new($entry[0].Open())
        try { $report = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if (@($zip.Entries | Where-Object { $_.Name -in @('nvngx_dlssg.dll','sl.dlss_g.dll') }).Count) {
            throw 'Provider bytes were copied into review.'
        }
    } finally { $zip.Dispose() }
    if ($report.schema_version -ne 3 -or $report.kind -ne 'NR_NATIVE_ADA_MFG_REVIEW') { throw 'MFG review schema missing.' }
    $found = @($report.streamline_modules)
    if ($found.Count -ne 2) { throw "Expected both named Streamline modules; found $($found.Count)." }
    foreach ($name in @('nvngx_dlssg.dll','sl.dlss_g.dll')) {
        $row = @($found | Where-Object { $_.file -like "*$name" })
        if ($row.Count -ne 1 -or -not $row[0].sha256 -or -not ($row[0].PSObject.Properties.Name -contains 'file_version')) {
            throw "Module identity missing for $name."
        }
    }
    if (@(Get-ChildItem -LiteralPath $out -File -Filter '*.zip').Count -ne 1) { throw 'Review ZIP missing.' }
    Write-Output 'PASS: verified ZIP retains collection identities and settings, excludes provider DLLs, and removes staging.'
    foreach ($failure in @('compression','corrupt','changed')) {
        $failedOut=Join-Path $root $failure
        $thrown=$false
        try {
            # Mutate only the compression boundary in an owned fixture copy.
            # Production now uses streaming ZipFile (Compress-Archive has a
            # per-file size limit); keep exercising its real verification/cleanup.
            $call='[IO.Compression.ZipFile]::CreateFromDirectory($folder,$archive,[IO.Compression.CompressionLevel]::Optimal,$true)'
            $source=[IO.File]::ReadAllText($Collector)
            if(-not $source.Contains($call)){throw 'Compression mutation boundary missing'}
            $replacement=switch($failure){
                'compression' { "throw 'Simulated compression failure'" }
                'corrupt' { '[IO.File]::WriteAllText($archive,''invalid zip'')' }
                'changed' { $call+'; $changed=Join-Path $folder ''OptiScaler.log''; $bytes=[IO.File]::ReadAllBytes($changed); $bytes[0]=$bytes[0] -bxor 1; [IO.File]::WriteAllBytes($changed,$bytes)' }
            }
            $mutant=Join-Path $root ($failure+'.ps1')
            [IO.File]::WriteAllText($mutant,$source.Replace($call,$replacement),[Text.UTF8Encoding]::new($false))
            & $mutant -GameDirectory $game -OutputDirectory $failedOut | Out-Null
        } catch { $thrown=$true }
        if (-not $thrown) { throw "Collector accepted $failure archive." }
        $saved=@(Get-ChildItem -LiteralPath $failedOut -Directory)
        if ($saved.Count -ne 1 -or -not (Test-Path -LiteralPath (Join-Path $saved[0].FullName 'OptiScaler.log'))) {
            throw "Collector discarded evidence on $failure failure."
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $game 'OptiScaler.log'))) { throw 'Original log was removed.' }
    Write-Output 'PASS: compression failure, corrupt ZIP and content mismatch preserve staging; original game files remain.'
} finally {
    if (Test-Path -LiteralPath $root) {
        $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
        while($pending.Count){foreach($item in Get-ChildItem -LiteralPath $pending.Pop() -Force){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture; cleanup refused'};if($item.PSIsContainer){$pending.Push($item.FullName)}}}
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
