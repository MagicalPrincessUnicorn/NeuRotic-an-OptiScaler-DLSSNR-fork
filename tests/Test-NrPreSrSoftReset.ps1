$ErrorActionPreference = "Stop"

$sourcePath = Join-Path $PSScriptRoot "..\OptiScaler\shaders\dlssnr\DlssNr_Dx12.cpp"
$source = [IO.File]::ReadAllText((Resolve-Path $sourcePath))
$configHeader = [IO.File]::ReadAllText((Resolve-Path (Join-Path $PSScriptRoot "..\OptiScaler\Config.h")))
$configSource = [IO.File]::ReadAllText((Resolve-Path (Join-Path $PSScriptRoot "..\OptiScaler\Config.cpp")))
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
Require-Literal $softBranch "BeginPreSrSoftResetBurst();" "Soft-reset telemetry does not start."
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
Require-Literal $source "resetPolicy.softResetForBurst && event == PreSrEvent::SoftReset" "The burst-latched policy does not gate soft-reset frames."
Require-Literal $source "firstObservation || inputChanged || outputChanged || qualityChanged ||" "Structural size/format/quality classification is missing."
Require-Literal $source "configurationChanged || nrRestart || resetPolicy.conservativeTransition;" "Model/pass configuration, NR restart, or default conservative Reset does not take structural precedence."
Require-Literal $source 'EndPreSrSoftResetBurst("native upscaler feature release")' "Native feature replacement does not terminate soft-reset classification."
Require-Literal $source "ResetPendingAfterPass(mainResetSubmitted," "Main-pass Reset is not success-latched."
Require-Literal $source "params->Set(NVSDK_NGX_Parameter_Output, originalOutputVoid);" "Pre-SR does not restore the native output parameter."
Require-Literal $configHeader "NrOptional<bool> DlssNrPreSrSoftReset { false };" "Experimental soft-reset option is not default-off."
Require-Literal $configSource 'readBool("DlssNr", "PreSrSoftReset")' "Experimental soft-reset option is not loaded."
Require-Literal $configSource 'ini.SetValue("DlssNr", "PreSrSoftReset"' "Experimental soft-reset option is not saved."
Require-Literal $handoff 'Intended UI location: `Advanced > Experimental`' "The future UI placement contract is missing."
Require-Literal $handoff 'Potential fix for situations where camera cuts cause the NR layer to reload, leading to a jarring presentation. This might lead to crashes when loading between worldspaces. Requires more testing.' "The approved experimental description changed."

Write-Output "PASS: default-off experimental gate, burst latch, retained Feature 18/scratch, and conservative structural handling"
