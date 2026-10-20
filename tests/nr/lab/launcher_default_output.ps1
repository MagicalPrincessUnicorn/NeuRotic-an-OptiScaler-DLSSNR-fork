[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Launcher,
    [Parameter(Mandatory)][string]$FreshRoot
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(Test-Path -LiteralPath $FreshRoot){throw 'TEST_ROOT_MUST_BE_FRESH'}
$fixture=Join-Path $FreshRoot 'package'
New-Item -ItemType Directory -Path $fixture | Out-Null
Copy-Item -LiteralPath $Launcher -Destination (Join-Path $fixture 'Run-OwnedDiagnostics.ps1')
[IO.File]::WriteAllText(
    (Join-Path $fixture 'OWNED_PACKAGE_MANIFEST.json'),
    '{"schema_version":1,"files":[]}',
    [Text.UTF8Encoding]::new($false)
)
$output=& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixture 'Run-OwnedDiagnostics.ps1') -VerifyOnly 2>&1
if($LASTEXITCODE -ne 0){throw "DEFAULT_OUTPUT_BINDING_FAILED: $($output -join ' ')"}
if(($output -join "`n") -notmatch 'OWNED_PACKAGE_VERIFIED'){throw 'VERIFY_RESULT_MISSING'}
Write-Output 'PASS: omitted Output binds after PSScriptRoot initialization in Windows PowerShell'
