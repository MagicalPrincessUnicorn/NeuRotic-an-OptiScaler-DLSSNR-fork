$ErrorActionPreference = 'Stop'
$nrTestRoot = Split-Path -Parent $PSScriptRoot
$nrConfigText = Get-Content -Raw -LiteralPath (Join-Path $nrTestRoot 'OptiScaler\Config.h')
$nrSnapshotText = Get-Content -Raw -LiteralPath (Join-Path $nrTestRoot 'OptiScaler\NrConfigSnapshot.h')
$nrFixtureText = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'nr_config.cpp')

$nrFieldPattern = '(?<type>NrOptional|CustomOptional)<[^;\r\n]+>\s+(?<name>DlssNr\w+)\b[^;\r\n]*;'
$nrFields = [regex]::Matches($nrConfigText, $nrFieldPattern)
if ($nrFields.Count -eq 0) { throw 'No NR config declarations found; update the coverage parser.' }
$nrFieldNames = @($nrFields | ForEach-Object { $_.Groups['name'].Value })
$nrCaptureNames = @([regex]::Matches($nrSnapshotText, 'X\((DlssNr\w+)\)') |
    ForEach-Object { $_.Groups[1].Value })
$nrStructuredCaptures = @('DlssNrExtraLayers')
$nrCaptureNamesForOptions = @($nrCaptureNames | Where-Object { $_ -notin $nrStructuredCaptures })

foreach ($nrField in $nrFields) {
    if ($nrField.Groups['type'].Value -ne 'NrOptional') {
        throw "Unsynchronized NR option: $($nrField.Groups['name'].Value)"
    }
}
$nrDuplicateCaptures = @($nrCaptureNames | Group-Object | Where-Object { $_.Count -ne 1 })
if ($nrDuplicateCaptures.Count -ne 0) { throw "Duplicate NR snapshot fields: $($nrDuplicateCaptures.Name -join ', ')" }
$nrCoverageDiff = @(Compare-Object $nrFieldNames $nrCaptureNamesForOptions)
if ($nrCoverageDiff.Count -ne 0) {
    throw "NR snapshot field mismatch: $($nrCoverageDiff | Out-String)"
}
if (-not $nrConfigText.Contains('DlssNrExtraLayerOptions DlssNrExtraLayers;') -or
    -not $nrSnapshotText.Contains('X(DlssNrExtraLayers)') -or
    -not $nrFixtureText.Contains('ExtraLayers DlssNrExtraLayers;')) {
    throw 'Structured extra-pass options are not covered by the production and standalone snapshots.'
}

# The standalone source exercises the production template without loading Config's Windows/NGX
# dependencies. Keep its types and defaults mechanically checked against production declarations.
$nrFixtureFields = [regex]::Matches($nrFixtureText, $nrFieldPattern)
foreach ($nrField in $nrFields) {
    $nrName = $nrField.Groups['name'].Value
    $nrFixtureMatches = @($nrFixtureFields | Where-Object { $_.Groups['name'].Value -eq $nrName })
    # FaultConfig intentionally shadows ScanAnchors with an injected throwing type; compare the
    # normal TestConfig declaration (the first match), not that isolated fault fixture.
    if ($nrFixtureMatches.Count -eq 0 -or $nrFixtureMatches[0].Value -cne $nrField.Value) {
        throw "Standalone config fixture type/default differs for $nrName"
    }
}
Write-Output "PASS NR snapshot coverage and fixture declarations: $($nrFields.Count) options"
