$ErrorActionPreference = 'Stop'
$source = 'C:/NeuRotic/flagship-all-merges'
$target = 'C:/NeuRotic/experimental-lab'
if ((git -C $target rev-parse --show-toplevel) -ne $target -or (git -C $target branch --show-current) -ne 'codex/experimental-lab') { throw 'Wrong checkout' }
if (git -C $target status --porcelain -- OptiScaler localization addons tests) { throw 'Experimental source already modified' }
# The runtime is one linked DLL: its pending ownership/configuration changes are
# coupled. Import that source closure, not desktop/worker/evidence/build trees.
$scope = @('OptiScaler','localization/bundled','localization/english.json','tools/localization')
$paths = @(git -c "safe.directory=$source" -C $source diff --name-only HEAD -- $scope)
$paths += @(git -c "safe.directory=$source" -C $source ls-files --others --exclude-standard -- $scope)
$paths = @($paths | Where-Object { $_ } | Sort-Object -Unique)
$records = @()
foreach ($relative in $paths) {
    $from = Join-Path $source $relative
    $to = Join-Path $target $relative
    if (!(Test-Path -LiteralPath $from -PathType Leaf)) { throw "Missing source: $relative" }
    $preimage = if (Test-Path -LiteralPath $to) { (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash } else { $null }
    $records += [pscustomobject]@{ path=$relative; baseSha256=$preimage; importedSha256=(Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash }
}
foreach ($entry in $records) {
    $from = Join-Path $source $entry.path
    $to = Join-Path $target $entry.path
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
    Copy-Item -LiteralPath $from -Destination $to
    if ((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -ne $entry.importedSha256) { throw "Copy changed: $($entry.path)" }
}
foreach ($entry in $records) {
    if ((Get-FileHash -LiteralPath (Join-Path $source $entry.path) -Algorithm SHA256).Hash -ne $entry.importedSha256) { throw "Source changed during snapshot: $($entry.path)" }
}
$receipt = Join-Path $target 'docs/implementation-evidence/universal-input-20261008/baseline-import.json'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $receipt) | Out-Null
[ordered]@{ base=(git -C $target rev-parse HEAD); source=$source; sourceHead=(git -c "safe.directory=$source" -C $source rev-parse HEAD); capturedUtc=[DateTime]::UtcNow.ToString('o'); scope=$scope; files=$records } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receipt -Encoding UTF8
Write-Output "Verified $($records.Count) bounded runtime/localization imports."
