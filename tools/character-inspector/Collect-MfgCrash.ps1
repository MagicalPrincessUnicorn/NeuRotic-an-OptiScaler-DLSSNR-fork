param([string]$OutputDirectory=$PSScriptRoot)

function Test-MfgGameValue([string]$value) {
    return $value.Trim().Trim('"') -match '^(?:.*[\\/])?CrimsonDesert\.exe$'
}
function Test-MfgGameEvent([xml]$eventXml) {
    foreach($node in $eventXml.Event.EventData.Data) {
        if($node.GetAttribute('Name') -in @('AppName','AppPath','P1')) {
            if(Test-MfgGameValue $node.InnerText) { return $true }
        }
    }
    return $false
}
function Test-MfgWerReport([string]$body) {
    foreach($line in ($body -split "`n")) {
        $parts=$line.Trim().Split('=',2)
        if($parts.Count -eq 2 -and $parts[0] -in @('AppName','AppPath','Sig[0].Value')) {
            if(Test-MfgGameValue $parts[1]) { return $true }
        }
    }
    return $false
}
if($MyInvocation.InvocationName -eq '.') { return }

$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$folder=(Resolve-Path -LiteralPath $OutputDirectory).Path
$archivePath=Join-Path $folder ('NeuRotic-Crimson-Crash-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)+'.zip')
$items=[ordered]@{}
$notes=[Collections.Generic.List[string]]::new()
$eventCount=0
$werCount=0
$since=(Get-Date).AddDays(-2)
Write-Host 'Reading existing Crimson Desert crash records from the last 48 hours...'
try {
    $events=Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000,1001;StartTime=$since} -MaxEvents 300 -ErrorAction Stop
    foreach($event in $events) {
        $xml=$event.ToXml()
        if(-not (Test-MfgGameEvent ([xml]$xml))) { continue }
        $eventCount++
        $items[('event-{0}-{1}.xml' -f $event.Id,$event.RecordId)]=$xml
        $items[('event-{0}-{1}.txt' -f $event.Id,$event.RecordId)]=("Time: {0:o}`r`nProvider: {1}`r`n{2}" -f $event.TimeCreated,$event.ProviderName,$event.Message)
    }
} catch { $notes.Add('Application event query: '+$_.Exception.Message) }

$roots=@(
    (Join-Path $env:LOCALAPPDATA 'Microsoft/Windows/WER/ReportArchive'),
    (Join-Path $env:LOCALAPPDATA 'Microsoft/Windows/WER/ReportQueue'),
    (Join-Path $env:ProgramData 'Microsoft/Windows/WER/ReportArchive'),
    (Join-Path $env:ProgramData 'Microsoft/Windows/WER/ReportQueue')
)
foreach($root in $roots) {
    try {
        if(-not (Test-Path -LiteralPath $root)) { continue }
        $reports=Get-ChildItem -LiteralPath $root -Directory -Filter 'AppCrash_CrimsonDesert*' -ErrorAction Stop |
            Where-Object {$_.LastWriteTime -ge $since -and -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)} |
            Sort-Object LastWriteTime -Descending | Select-Object -First 8
        foreach($report in $reports) {
            $path=Join-Path $report.FullName 'Report.wer'
            if(-not (Test-Path -LiteralPath $path)) { continue }
            $info=Get-Item -LiteralPath $path
            if($info.Length -gt 1MB -or ($info.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
            $body=Get-Content -LiteralPath $path -Raw -ErrorAction Stop
            if(-not (Test-MfgWerReport $body)) { continue }
            $werCount++
            $items[('report-{0}.wer' -f $werCount)]=$body
        }
    } catch { $notes.Add('WER query '+$root+': '+$_.Exception.Message) }
}
$summary=[ordered]@{
    kind='NeuRotic_Crimson_existing_crash_records'; collected_utc=[DateTime]::UtcNow.ToString('o')
    since_local=$since.ToString('o'); event_count=$eventCount; wer_report_count=$werCount
    query_notes=@($notes.ToArray()); game_launched=$false; settings_changed=$false; memory_dumps_collected=$false
}
$items['SUMMARY.json']=$summary | ConvertTo-Json -Depth 5
$items['READ-ME.txt']='These are existing Windows crash records for CrimsonDesert.exe only. No game launch, settings changes, registry changes or memory dump collection was performed. If no records were found, SUMMARY.json lists any query errors. Return this ZIP to the debugging chat.'
$stream=[IO.File]::Open($archivePath,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
    try {
        foreach($name in $items.Keys) {
            $entry=$zip.CreateEntry($name)
            $writer=[IO.StreamWriter]::new($entry.Open(),[Text.UTF8Encoding]::new($false))
            try { $writer.Write([string]$items[$name]) } finally { $writer.Dispose() }
        }
    } finally { $zip.Dispose() }
} finally { $stream.Dispose() }
Write-Host "Collected $eventCount event records and $werCount WER reports."
Write-Host 'Send this ZIP back to the chat:'
Write-Host $archivePath
if($eventCount -eq 0 -and $werCount -eq 0) {
    Write-Host 'No matching crash records were available. Send the ZIP anyway so query errors can be checked.'
}
