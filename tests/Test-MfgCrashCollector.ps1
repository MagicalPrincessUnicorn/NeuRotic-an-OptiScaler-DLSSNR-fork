$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../tools/character-inspector/Collect-MfgCrash.ps1')
function Check($value,$message) { if(-not $value){throw $message} }
Check (Test-MfgGameValue 'CrimsonDesert.exe') 'Application name must match'
Check (Test-MfgGameValue 'C:\Games\Crimson Desert\CrimsonDesert.exe') 'Full application path must match'
Check (-not (Test-MfgGameValue 'OtherCrimsonDesert.exe')) 'Similar unrelated application must be excluded'
Check (-not (Test-MfgGameValue 'CrimsonDesert.exe.backup')) 'Suffix must be excluded'
$crash=[xml]'<Event xmlns="http://schemas.microsoft.com/win/2004/08/events/event"><EventData><Data Name="AppName">CrimsonDesert.exe</Data><Data Name="ExceptionCode">c00000fd</Data><Data Name="ModuleName">dxgi.dll</Data></EventData></Event>'
$wer=[xml]'<Event xmlns="http://schemas.microsoft.com/win/2004/08/events/event"><EventData><Data Name="P1">CrimsonDesert.exe</Data><Data Name="P7">c0000005</Data></EventData></Event>'
$other=[xml]'<Event><EventData><Data Name="AppName">other.exe</Data><Data Name="ModuleName">CrimsonDesert.exe</Data></EventData></Event>'
Check (Test-MfgGameEvent $crash) 'Application Error event must match'
Check (Test-MfgGameEvent $wer) 'Windows Error Reporting event must match'
Check (-not (Test-MfgGameEvent $other)) 'Module name must not be mistaken for application identity'
Check (Test-MfgWerReport "AppName=Crimson Desert`nSig[0].Value=CrimsonDesert.exe`nSig[6].Value=c0000005") 'WER application signature must match'
Check (-not (Test-MfgWerReport 'AppPath=C:\Games\Other.exe')) 'Unrelated WER report must be excluded'
Write-Output 'PASS: crash event identity, WER signatures, exception fields and unrelated-app exclusion'

# Replace only the read-only event query; execute the real collector and ZIP writer.
function Get-WinEvent {
    [CmdletBinding()]param($FilterHashtable,$MaxEvents)
    foreach($pair in @(@('CrimsonDesert.exe',101),@('OtherGame.exe',102))) {
        $record=[pscustomobject]@{Id=1000;RecordId=$pair[1];TimeCreated=[DateTime]::Now;
            ProviderName='Application Error';Message='Fixture exception c00000fd in dxgi.dll';
            Xml=('<Event><EventData><Data Name="AppName">'+$pair[0]+'</Data><Data Name="ExceptionCode">c00000fd</Data></EventData></Event>')}
        $record | Add-Member -MemberType ScriptMethod -Name ToXml -Value {return $this.Xml}
        $record
    }
}
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$fixture=[IO.Path]::GetFullPath((Join-Path $repo 'builds/inspector-motion/crash-collector-fixture'))
Check ($fixture.StartsWith($repo+'\builds\',[StringComparison]::OrdinalIgnoreCase)) 'Fixture escaped checkout'
Check (-not (Test-Path -LiteralPath $fixture)) 'Fixture is already in use'
New-Item -ItemType Directory -Path $fixture | Out-Null
try {
    & (Join-Path $repo 'tools/character-inspector/Collect-MfgCrash.ps1') -OutputDirectory $fixture
    $archives=@(Get-ChildItem -LiteralPath $fixture -Filter '*.zip')
    Check ($archives.Count -eq 1) 'Collector must produce one archive'
    $zip=[IO.Compression.ZipFile]::OpenRead($archives[0].FullName)
    try {
        Check ($null -ne $zip.GetEntry('event-1000-101.xml')) 'Matching crash must be in the package'
        Check ($null -eq $zip.GetEntry('event-1000-102.xml')) 'Unrelated crash must not be exported'
        $reader=[IO.StreamReader]::new($zip.GetEntry('event-1000-101.xml').Open())
        try { $returned=[xml]$reader.ReadToEnd() } finally { $reader.Dispose() }
        Check ($returned.Event.EventData.Data[1].InnerText -eq 'c00000fd') 'Exception code must survive packaging'
        $reader=[IO.StreamReader]::new($zip.GetEntry('SUMMARY.json').Open())
        try { $summary=$reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        Check ($summary.event_count -eq 1 -and -not $summary.settings_changed) 'Summary must describe actual collection'
    } finally { $zip.Dispose() }
} finally {
    if((Get-Item -LiteralPath $fixture).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Refuse junction cleanup'}
    foreach($item in Get-ChildItem -LiteralPath $fixture -File) { Remove-Item -LiteralPath $item.FullName }
    Remove-Item -LiteralPath $fixture
}
Write-Output 'PASS: actual collector packages matching crash XML and preserves the fault code; unrelated event excluded'
