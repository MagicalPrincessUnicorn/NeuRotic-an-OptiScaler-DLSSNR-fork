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

$create = $dx12.IndexOf("g_nr.layer2.feature =`n            CreateNrFeature")
$createReturn = $dx12.IndexOf('This command list contains layer-2 creation', $create)
$evaluate = $dx12.IndexOf('cmdList, g_nr.layer2.feature, g_nr.capabilityParams', $create)
Require ($create -ge 0 -and $createReturn -gt $create -and $createReturn -lt $evaluate) `
    'Layer-2 creation must return before either model evaluation.'
Require ($dx12.Contains('const bool releasedLayer2ThisCall = TickNrRetired(true);') -and `
         $dx12.Contains('if (releasedLayer2ThisCall)')) `
    'Layer-2 vendor release must force a lifecycle-only frame.'
Require ($dx12.Contains('featureAwaitingRelease') -and $dx12.Contains('ParkSecondLayerFeature')) `
    'Layer-2 replacement must remain blocked on completion-gated retirement.'

Write-Output 'PASS second NR layer: default-off config, D3D12-only exposure, composed ordering, independent feature, lifecycle-only create/release'
