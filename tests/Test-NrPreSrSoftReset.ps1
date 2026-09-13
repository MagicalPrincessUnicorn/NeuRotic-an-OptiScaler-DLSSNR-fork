$ErrorActionPreference = "Stop"

$sourcePath = Join-Path $PSScriptRoot "..\OptiScaler\shaders\dlssnr\DlssNr_Dx12.cpp"
$source = [IO.File]::ReadAllText((Resolve-Path $sourcePath))
$configHeader = [IO.File]::ReadAllText((Resolve-Path (Join-Path $PSScriptRoot "..\OptiScaler\Config.h")))
$configSource = [IO.File]::ReadAllText((Resolve-Path (Join-Path $PSScriptRoot "..\OptiScaler\Config.cpp")))
$policyHeader = [IO.File]::ReadAllText((Resolve-Path (Join-Path $PSScriptRoot "..\OptiScaler\dlssnr\NrExperimentalPolicy.h")))
$handoff = [IO.File]::ReadAllText((Resolve-Path (Join-Path $PSScriptRoot "..\docs\enhancements\presr-soft-reset.md")))

function Require-Literal([string]$Text, [string]$Needle, [string]$Message)
{
    if (-not $Text.Contains($Needle))
    {
        throw $Message
    }
}

$softStart = $source.IndexOf("if (softResetFrame)")
$softEnd = $source.IndexOf("else if (resetRequested)", $softStart)
if ($softStart -lt 0 -or $softEnd -le $softStart)
{
    throw "Soft-reset branch could not be isolated."
}

$softBranch = $source.Substring($softStart, $softEnd - $softStart)
Require-Literal $softBranch "requestResetForAllPasses();" "Soft reset is not sent to every pass."
foreach ($forbidden in @("ParkNrFeature(", "ParkNrResource(", "CreateNrFeature(",
                          "preSrAwaitingEvaluation =", "preSrScratchPrimed = false"))
{
    if ($softBranch.Contains($forbidden))
    {
        throw "Soft-reset branch contains forbidden structural operation: $forbidden"
    }
}

Require-Literal $source "if (resetRequested && !softResetFrame)" "Held structural resets are not kept on the conservative bypass."
Require-Literal $source "cfg.DlssNrPreSrSoftReset.value_or_default()" "Soft-reset behavior is not gated by the experimental option."
Require-Literal $source "experimentalPolicySnapshot.active" "Soft-reset behavior can bypass experimental-session recovery readiness."
Require-Literal $policyHeader "ready && config.DlssNrExperimentalMode.value_or_default()" "Soft-reset behavior is not gated by the ready experimental master switch."
Require-Literal $source "preSrSoftResetEnabled || g_nr.preSrResetPolicy.softResetForBurst" "Disabling the option can abandon an accepted burst debt."
Require-Literal $source "resetPolicy.softResetForBurst && event == PreSrEvent::SoftReset" "The burst-latched policy does not gate soft-reset frames."
Require-Literal $source "firstObservation || inputChanged || outputChanged || qualityChanged ||" "Structural size/format/quality classification is missing."
Require-Literal $source "experimentalPolicy, configurationChanged, nrRestart);" "New transitions are not isolated behind the experimental policy."
Require-Literal $source 'EndPreSrSoftResetBurst("native upscaler feature release")' "Native feature replacement does not terminate soft-reset classification."
Require-Literal $source "g_nr.preSrSoftResetDispatch && DlssNr::ResetPendingAfterPass(" "Main-pass success latch escaped the native Pre-SR scope."
Require-Literal $source "params->Set(NVSDK_NGX_Parameter_Output, originalOutputVoid);" "Pre-SR does not restore the native output parameter."
Require-Literal $configHeader "NrOptional<bool> DlssNrPreSrSoftReset { false };" "Experimental soft-reset option is not default-off."
Require-Literal $configSource 'readBool("DlssNr", "PreSrSoftReset")' "Experimental soft-reset option is not loaded."
Require-Literal $configSource 'ini.SetValue("DlssNr", "PreSrSoftReset"' "Experimental soft-reset option is not saved."
Require-Literal $handoff 'UI location: `Neural Rendering > Neural Rendering - Experimental Overrides`' "The integrated UI placement contract is missing."
Require-Literal $handoff 'Potential fix for situations where camera cuts cause the NR layer to reload, leading to a jarring presentation. This might lead to crashes when loading between worldspaces. Requires more testing.' "The approved experimental description changed."

$entry = $source.Substring($source.IndexOf('ID3D12Resource* EvaluateBeforeUpscale('))
$entry = $entry.Substring(0, $entry.IndexOf('void EvaluateAfterUpscale('))
$latch = $entry.IndexOf('LatchPreSrReset();')
foreach ($exit in @('if (cmdList == nullptr)', 'if (!GpuSafety::Record(cmdList))',
                    'if (color == nullptr)', 'void* originalOutputVoid = nullptr;')) {
    if ($latch -lt 0 -or $entry.IndexOf($exit) -le $latch) {
        throw "Experimental Reset must be latched before: $exit"
    }
}
Require-Literal $entry 'const bool observeEarly = authoritativeNativePreSr &&' 'Missing authoritative route gate.'
$compatStart = $entry.IndexOf('const bool sessionCompatible =')
$compatEnd = $entry.IndexOf('const PreSrEvent event =', $compatStart)
$compat = $entry.Substring($compatStart, $compatEnd - $compatStart)
if ($compat.Contains('preSrScratchPrimed') -or $compat.Contains('preSrAwaitingEvaluation')) {
    throw 'Temporary publication readiness must not be used as session identity.'
}
$failureBranch = $entry.Substring($entry.IndexOf('else if (resetRequested)'),
    $entry.IndexOf('// Observe compatible configuration changes') - $entry.IndexOf('else if (resetRequested)'))
Require-Literal $failureBranch 'HoldPreSrStructuralReset(' 'Resource failures must not unconditionally hold Reset.'
if ($failureBranch.Contains('EndPreSrSoftResetBurst(')) { throw 'Structural escalation truncates burst accounting.' }
Require-Literal $source 'aborted; lifecycle counts partial' 'Interrupted burst totals must identify their incomplete scope.'
Require-Literal $source 'g_featureBuilds + g_layer2FeatureBuilds -' 'Burst builds must include additional models.'
Require-Literal $source 'preSrResetFrameResult.RecordPass' 'Missing actual model evaluation accounting.'
$native = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\OptiScaler\inputs\NVNGX_DLSS_Dx12.cpp'))
if ([regex]::Matches($native, 'EvaluateBeforeUpscale\(InCmdList, InParameters, nullptr, &\*nrSettings, true\)').Count -ne 1) {
    throw 'Exactly the native passthrough call must opt into the authoritative experiment.'
}
Write-Output 'PASS: source wiring/order assertions (not GPU execution), native-only gate, recovery, full-burst accounting'
