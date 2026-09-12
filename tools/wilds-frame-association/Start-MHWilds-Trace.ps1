[CmdletBinding()]
param(
    [switch]$CheckOnly,
    [switch]$SteamLaunch,
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$GameCommand
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'MHWilds-TraceCapture.ps1')
$gameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\MonsterHunterWilds'
if ($CheckOnly) { $GameCommand = @((Join-Path $gameDirectory 'MonsterHunterWilds.exe')) }
elseif (!$SteamLaunch) {
    Write-Host 'Launch Wilds through the existing Steam entry; it forwards to this diagnostic handoff.'
    exit 2
}
Invoke-MHWildsTraceCapture -GameDirectory $gameDirectory -BuildManifest (Join-Path $PSScriptRoot 'BUILD-MANIFEST.json') `
    -EvidenceRoot 'C:\OptiScaler-NR-Dev\logs\mhwilds-frame-association-sessions' `
    -GameCommand $GameCommand -CheckOnly:$CheckOnly -RequireAssociationProfile
