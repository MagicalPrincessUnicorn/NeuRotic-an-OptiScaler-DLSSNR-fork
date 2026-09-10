$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$dx12Path = Join-Path $root 'OptiScaler\shaders\dlssnr\DlssNr_Dx12.cpp'
$dx12 = Get-Content -Raw -LiteralPath $dx12Path
$vk = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler\dlssnr\DlssNrFeature_Vk.cpp')
$config = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler\Config.h')
$snapshot = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler\NrConfigSnapshot.h')

function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Require ($config.Contains('NrOptional<bool> DlssNrSecondLayer { false };')) `
    'Second-layer config must remain an explicit default-off bool.'
Require ($snapshot.Contains('X(DlssNrSecondLayer)')) `
    'Second-layer config is missing from the coherent render snapshot.'
Require ($snapshot.Contains('X(DlssNrSecondLayerWorkingScale)') -and `
         $snapshot.Contains('X(DlssNrSecondLayerPreset)') -and `
         $snapshot.Contains('X(DlssNrSecondLayerTransferStrength)')) `
    'Layer-2 processing controls are missing from the coherent render snapshot.'
Require (-not $vk.Contains('DlssNrSecondLayer')) `
    'Vulkan must not consume or advertise the D3D12-only second-layer option.'
Require (-not $dx12.Contains('passFeature')) `
    'The removed pass-count feature array must not be revived.'

$firstResolve = $dx12.IndexOf('++g_nr.successfulEvaluations;')
$secondEncode = $dx12.IndexOf('DlssNrConstants layer2Encode = encodeParams;')
$secondEvaluate = $dx12.IndexOf('cmdList, g_nr.layer2.feature, g_nr.capabilityParams')
$secondResolve = $dx12.IndexOf('layer2ResolveAnswer, g_nr.layer2.hdrCopy')
Require ($firstResolve -ge 0 -and $secondEncode -gt $firstResolve -and `
         $secondEvaluate -gt $secondEncode -and $secondResolve -gt $secondEvaluate) `
    'Layer 2 must encode the completed layer-1 composition, evaluate its own feature, then resolve.'

$create = $dx12.IndexOf('CreateNrFeature(SecondLayerTuning(cfg)')
$createReturn = $dx12.IndexOf('This command list contains layer-2 creation', $create)
$evaluate = $dx12.IndexOf('cmdList, g_nr.layer2.feature, g_nr.capabilityParams', $create)
Require ($create -ge 0 -and $createReturn -gt $create -and $createReturn -lt $evaluate) `
    'Layer-2 creation must return before either model evaluation.'
Require ($dx12.Contains('const bool releasedLayer2ThisCall = TickNrRetired(true);') -and `
         $dx12.Contains('if (releasedLayer2ThisCall)')) `
    'Layer-2 vendor release must force a lifecycle-only frame.'
Require ($dx12.Contains('featureAwaitingRelease') -and $dx12.Contains('ParkSecondLayerFeature')) `
    'Layer-2 replacement must remain blocked on completion-gated retirement.'
Require ($dx12.Contains('cfg.DlssNrSecondLayerWorkingScale.value_or_default()') -and `
         $dx12.Contains('cfg.DlssNrSecondLayerScalingDownscaler.value_or_default()') -and `
         $dx12.Contains('cfg.DlssNrSecondLayerTransferStrength.value_or_default()') -and `
         $dx12.Contains('cfg.DlssNrSecondLayerApplyModel.value_or_default()')) `
    'Layer 2 must consume its own resolution, resampling, composition and apply controls.'
Require (-not $dx12.Contains('ParkSecondLayerFeature(resolutionChanged ?')) `
    'A layer-1-only tuning or working-scale change must not retire layer 2.'

Write-Output 'PASS second NR layer: default-off config, independent controls/resources, composed ordering, lifecycle-only create/release'

$normalFirst = $dx12.IndexOf('resolveParams.CompareMode = 0;')
$normalDebug = $dx12.IndexOf('resolveParams.DebugView = 0;')
$firstDispatch = $dx12.IndexOf('DispatchPass(cmdList, resolveParams, resolveProxy')
$lateDebug = $dx12.IndexOf('diagnosticParams.DebugView = presentationParams.DebugView;')
$lateCompare = $dx12.IndexOf('display.Mode = DlssNrMode_Present;')
Require ($normalFirst -lt $firstDispatch -and $normalDebug -lt $firstDispatch -and `
         $normalFirst -ge 0 -and $normalDebug -ge 0 -and `
         $lateDebug -gt $secondResolve -and $lateCompare -gt $lateDebug) `
    'Display comparison/debug must be excluded from both model inputs and run after the second resolve.'
Require ($dx12.Contains('display, g_nr.colorCopy, nullptr, g_nr.hdrCopy')) `
    'Late comparison must use the original first-layer frame.'
Require ($dx12.Contains('mvToWork.y') -and $dx12.Contains('mvToLayer2Work.y')) `
    'Both layers must scale vertical motion by the actual working height.'
$failure = $dx12.IndexOf('if (scratchResult == DlssNr::Detail::LayerScratchResult::SecondUnavailable)')
$failureEnd = $dx12.IndexOf('ReleaseSurfacesIfFormatChanged(desc.Format, target);', $failure)
$fallback = $dx12.Substring($failure, $failureEnd - $failure)
Require ($fallback.Contains('secondLayerHealthy = false;') -and $fallback.Contains('g_nr.layer2.failed = true;') -and `
         -not $fallback.Contains('return;') -and -not $fallback.Contains('g_nr.reset = true;')) `
    'Optional allocation failure must disable layer 2 without skipping or resetting layer 1.'
Require ($dx12.Contains('layer.featureAwaitingRelease != nullptr && !layer.failed')) `
    'A failed optional layer must not stall layer 1 throughout pending retirement.'
Write-Output 'PASS multipass review fixes: late presentation, independent motion axes, optional allocation fallback'
