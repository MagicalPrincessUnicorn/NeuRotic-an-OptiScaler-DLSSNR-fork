param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$source = Get-Content -Raw (Join-Path $repo 'OptiScaler/hooks/Streamline_Hooks.cpp')
$sections = @(
    @('static std::optional<bool> NrNativeMfgAdapterSupport(', 'std::optional<bool> StreamlineHooks::nativeMfgAdapter('),
    @('std::optional<bool> StreamlineHooks::nativeMfgAdapter(', 'bool StreamlineHooks::prepareNativeMfgCapabilities('),
    @('void StreamlineHooks::updateDlssgOptions()', 'void StreamlineHooks::bindNativeMfgWrapper('),
    @('static void NrTraceNativeMfgReceipt(', 'sl::Result StreamlineHooks::hkslDLSSGSetOptions('),
    @('sl::Result StreamlineHooks::hkslDLSSGSetOptions(', 'sl::Result StreamlineHooks::hkslDLSSGGetState('),
    @('sl::Result StreamlineHooks::hkslDLSSGGetState(', 'bool StreamlineHooks::hkreflex_slOnPluginLoad('),
    @('sl::Result StreamlineHooks::hkslGetFeatureFunction(', 'sl::Result StreamlineHooks::hkslSetTag('),
    @('void StreamlineHooks::wrapNativeDlssgFunction(', 'Neurotic::Mfg::MfgRequestReceipt StreamlineHooks::mfgRequestReceipt(')
)
$bodies = foreach ($pair in $sections) {
    $start = $source.IndexOf($pair[0]); $end = $source.IndexOf($pair[1], $start + 1)
    if ($start -lt 0 -or $end -lt 0) { throw 'Hook extraction boundary missing' }
    $source.Substring($start, $end - $start)
}
Set-Content -Encoding utf8 (Join-Path $OutputDirectory 'nr_streamline_observer_hooks.inc') ($bodies -join "`n")
& (Join-Path $PSScriptRoot 'Run-StreamlineObserver.cmd') $OutputDirectory
exit $LASTEXITCODE
