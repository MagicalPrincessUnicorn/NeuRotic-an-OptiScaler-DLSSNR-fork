[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$FixtureRoot
)
$ErrorActionPreference='Stop'
$PackageRoot=(Resolve-Path -LiteralPath $PackageRoot).Path
$FixtureRoot=[IO.Path]::GetFullPath($FixtureRoot).TrimEnd('\')
if (Test-Path -LiteralPath $FixtureRoot) { throw 'Use a fresh fixture directory.' }
New-Item -ItemType Directory -Path $FixtureRoot | Out-Null
$gameExe=Join-Path $FixtureRoot 'W03FixtureGame.exe'
[IO.File]::WriteAllBytes($gameExe,[byte[]](0x4d,0x5a))
$sentinel=Join-Path $FixtureRoot 'UnrelatedGameData.txt'
[IO.File]::WriteAllText($sentinel,'preserve this game file')
$before=(Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
$manifest=Get-Content -Raw -LiteralPath (Join-Path $PackageRoot 'support\PACKAGE-MANIFEST.json') | ConvertFrom-Json
$engine=Join-Path $PackageRoot 'support\NeuRotic-Setup-Engine.ps1'
$sdk=@($manifest.files | Where-Object { $_.path -eq 'payload\OptiScaler\amd_fidelityfx_dx12.dll' })
$product=@($manifest.files | Where-Object { $_.path -eq 'payload\OptiScaler.dll' })
$forwarder=@($manifest.files | Where-Object { $_.path -eq 'payload\nvngx.dll_dlssnr.dll' })
if ($sdk.Count -ne 1 -or $product.Count -ne 1 -or $forwarder.Count -ne 1) { throw 'Exact W03 payload entries are required.' }
$sdkPath=Join-Path $FixtureRoot 'OptiScaler\amd_fidelityfx_dx12.dll'
$proxyPath=Join-Path $FixtureRoot 'dxgi.dll'
$forwarderPath=Join-Path $FixtureRoot 'nvngx.dll_dlssnr.dll'
$originalFiles=@(Get-ChildItem -LiteralPath $FixtureRoot -File | Select-Object -ExpandProperty Name)
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $engine -GameExecutable $gameExe -ProxyName dxgi.dll -CheckOnly | Out-Null
if ($LASTEXITCODE -ne 0 -or @(Get-ChildItem -LiteralPath $FixtureRoot -File | Select-Object -ExpandProperty Name).Count -ne $originalFiles.Count) {
    throw 'Preflight failed or changed the fixture.'
}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $engine -GameExecutable $gameExe -ProxyName dxgi.dll | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Install failed.' }
foreach ($row in @(@{path=$sdkPath;hash=$sdk[0].sha256},@{path=$proxyPath;hash=$product[0].sha256},@{path=$forwarderPath;hash=$forwarder[0].sha256})) {
    if ((Get-FileHash -LiteralPath $row.path -Algorithm SHA256).Hash -ne $row.hash) { throw "Installed hash differs: $($row.path)" }
}
if (Test-Path -LiteralPath (Join-Path $FixtureRoot 'nvngx_dlssnr.dll')) { throw 'Private provider model was installed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $engine -Uninstall -GameExecutable $gameExe -UninstallMode Full -ConfirmUninstall | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Uninstall failed.' }
foreach ($path in @($sdkPath,$proxyPath,$forwarderPath)) {
    if (Test-Path -LiteralPath $path) { throw "Managed file remained after restore: $path" }
}
if ((Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -ne $before) { throw 'Unrelated game file changed.' }
Write-Output 'PASS: exact SDK/product/forwarder installed, private model absent, managed files restored, unrelated file preserved; no game launched.'
