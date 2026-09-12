[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$LogPath,
    [Parameter(Mandatory=$true)][string]$HarnessManifest,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{32}$')][string]$SessionId,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$traceLog = Get-Item -LiteralPath $LogPath
$traceHarness = Get-Item -LiteralPath $HarnessManifest
if ($traceLog.Length -gt 512MB -or $traceHarness.Length -gt 16MB) { throw 'Evidence exceeds bounded input size.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists; evidence is never overwritten.' }
$manifest = Get-Content -LiteralPath $traceHarness.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
$frequency = [long]$manifest.timing.qpc_frequency
if ($frequency -le 0) { throw 'Harness has no usable QPC frequency.' }
$logHash = (Get-FileHash -LiteralPath $traceLog.FullName -Algorithm SHA256).Hash
$harnessHash = (Get-FileHash -LiteralPath $traceHarness.FullName -Algorithm SHA256).Hash
$records = New-Object System.Collections.Generic.List[object]
$seen = New-Object 'System.Collections.Generic.HashSet[long]'
$counts = @{}
$issues = New-Object System.Collections.Generic.List[string]
$pattern = 'NR_FRAME_TRACE v=1 session=([0-9a-f]{32}) pid=(\d+) tid=(\d+) qpc=(\d+) frequency=(\d+) seq=(\d+) native=(\d+) present=(\d+) kind=([a-z0-9-]+) classification=unknown (.*)$'
foreach ($line in [IO.File]::ReadLines($traceLog.FullName)) {
    if ($line -notmatch ('NR_FRAME_TRACE .*session=' + $SessionId + ' ')) { continue }
    if ($line -notmatch $pattern) { throw 'Malformed or unsupported trace record in selected session.' }
    if ($Matches[1] -ne $SessionId) { continue }
    if ($records.Count -ge 65536) { throw 'Trace exceeds its declared event budget.' }
    $sequence = [long]$Matches[6]
    if ($sequence -lt 1 -or $sequence -gt 65536 -or !$seen.Add($sequence)) { throw 'Invalid/duplicate event ID; mixed or damaged evidence.' }
    if ([long]$Matches[5] -ne $frequency) { throw 'QPC frequency differs from the harness.' }
    $kind = $Matches[9]
    $qpc = [long]$Matches[4]
    $records.Add([pscustomobject][ordered]@{
        sequence=$sequence;pid=[long]$Matches[2];tid=[long]$Matches[3];qpc=$qpc
        native_observation=[long]$Matches[7];present_observation=[long]$Matches[8];kind=$kind
        classification='unknown';details=$Matches[10]
        in_harness_interval=($qpc -ge [long]$manifest.timing.started_qpc -and $qpc -le [long]$manifest.timing.ended_qpc)
    })
    if (!$counts.ContainsKey($kind)) { $counts[$kind] = 0 }
    $counts[$kind]++
}
if ($records.Count -eq 0) { throw 'No selected-session events; logger/arming evidence unavailable.' }
if (@($records | Select-Object -ExpandProperty pid -Unique).Count -ne 1) { throw 'Multiple processes used one session; collect separately.' }
$maxSequence = ($records | Measure-Object -Property sequence -Maximum).Maximum
if ($seen.Count -ne $maxSequence) { $issues.Add('Event sequence has gaps: truncated, filtered, or lost records.') }
if ($counts.ContainsKey('trace-ended')) { $issues.Add('Capture reached its event budget; later frames are unobserved.') }
if (@($records | Where-Object in_harness_interval).Count -eq 0) { $issues.Add('No QPC overlap with the harness; do not correlate the sessions.') }
if ((Get-FileHash -LiteralPath $traceLog.FullName -Algorithm SHA256).Hash -ne $logHash -or
    (Get-FileHash -LiteralPath $traceHarness.FullName -Algorithm SHA256).Hash -ne $harnessHash) {
    throw 'Inputs changed during analysis. Stop capture and retry with stable evidence.'
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
Copy-Item -LiteralPath $traceLog.FullName -Destination (Join-Path $OutputDirectory 'raw-neurotic.log')
Copy-Item -LiteralPath $traceHarness.FullName -Destination (Join-Path $OutputDirectory 'harness-manifest.json')
if ((Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'raw-neurotic.log')).Hash -ne $logHash -or
    (Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'harness-manifest.json')).Hash -ne $harnessHash) {
    throw 'Retained evidence hash mismatch. Partial output retained; do not use it.'
}
$records | Sort-Object qpc,sequence | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'events.csv') -NoTypeInformation -Encoding UTF8
$report = [ordered]@{
    schema_version=1;phase='A observation only';session_id=$SessionId;qpc_frequency=$frequency
    log_sha256=$logHash;harness_manifest_sha256=$harnessHash;event_count=$records.Count;counts=$counts
    result='Inconclusive';decision='keep experimental';issues=$issues.ToArray()
    limitations=@('CPU observations are not GPU dependencies.','Native observation IDs are not real-frame tokens.',
        'Provider tags are not proof of input capture or consumption.','Generated presentations remain unclassified.',
        'No packet/admission/history/FG ordering behavior was changed.',
        'PresentMon rows may corroborate timing; this tool does not assign frame ownership by proximity.',
        'Retain the complete original harness evidence directory, including PresentMon/raw artifacts.')
}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'TRACE-MANIFEST.json'), ($report | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
Write-Host ('Retained {0} observations. Runtime result: Inconclusive. Review TRACE-MANIFEST.json and events.csv.' -f $records.Count)
