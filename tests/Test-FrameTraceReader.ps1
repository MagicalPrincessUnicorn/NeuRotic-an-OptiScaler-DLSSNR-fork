$ErrorActionPreference = 'Stop'
$root = Join-Path 'C:\OptiScaler-NR-Dev\logs' ('frame-trace-reader-fixtures-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$reader = Join-Path $PSScriptRoot '..\tools\diagnostics\Read-FrameTrace.ps1'
$session = '0123456789abcdef0123456789abcdef'
$manifestPath = Join-Path $root 'manifest.json'
[IO.File]::WriteAllText($manifestPath, '{"timing":{"qpc_frequency":10000000,"started_qpc":100,"ended_qpc":200}}')
$first = "NR_FRAME_TRACE v=1 session=$session pid=1 tid=2 qpc=120 frequency=10000000 seq=1 native=0 present=0 kind=probe classification=unknown value=1"
$second = "NR_FRAME_TRACE v=1 session=$session pid=1 tid=3 qpc=110 frequency=10000000 seq=2 native=0 present=0 kind=probe classification=unknown value=2"
function Run-Case([string]$name, [string]$content, [bool]$expectedFailure) {
    $log = Join-Path $root ($name + '.log')
    [IO.File]::WriteAllText($log, $content)
    $before = (Get-FileHash -LiteralPath $log).Hash
    $failed = $false
    try { & $reader -LogPath $log -HarnessManifest $manifestPath -SessionId $session -OutputDirectory (Join-Path $root $name) }
    catch { $failed = $true }
    if ($failed -ne $expectedFailure) { throw "Unexpected outcome: $name" }
    if ((Get-FileHash -LiteralPath $log).Hash -ne $before) { throw 'Reader changed the source log.' }
}
Run-Case 'valid' ($first + "`n" + $second) $false
$valid = Get-Content (Join-Path $root 'valid\TRACE-MANIFEST.json') -Raw | ConvertFrom-Json
if ($valid.event_count -ne 2 -or $valid.result -ne 'Inconclusive' -or $valid.issues.Count -ne 0) { throw 'Incorrect valid report.' }
$rows = @(Import-Csv (Join-Path $root 'valid\events.csv'))
if ($rows[0].sequence -ne '2') { throw 'Cross-thread QPC order was not preserved.' }
Run-Case 'duplicate' ($first + "`n" + $first) $true
Run-Case 'absent' 'No trace data.' $true
Run-Case 'wrong-clock' ($first.Replace('frequency=10000000','frequency=42')) $true
Run-Case 'wrong-schema' ($first.Replace('v=1','v=2')) $true
Run-Case 'mixed-process' ($first + "`n" + $second.Replace('pid=1','pid=3')) $true
Run-Case 'gap' $second $false
$gap = Get-Content (Join-Path $root 'gap\TRACE-MANIFEST.json') -Raw | ConvertFrom-Json
if ($gap.issues.Count -ne 1) { throw 'Gap not reported.' }
Write-Host "PASS: raw retention, sorting, input immutability, duplicate/session/clock/schema failures, gaps. Fixtures: $root"
