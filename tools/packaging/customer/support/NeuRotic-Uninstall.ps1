[CmdletBinding()]
param([string]$GameDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'NeuRotic-InstallCore.ps1')
if(-not $GameDirectory){$GameDirectory=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)}
$result=Invoke-NeuroticSimpleUninstall $GameDirectory
Write-Output ($result|ConvertTo-Json -Depth 8)
if($result.status -notin @('Succeeded','SucceededWithNotes')){exit 2}
