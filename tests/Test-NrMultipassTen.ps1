$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$config = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler\Config.cpp')
$header = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler\Config.h')
$dx12 = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler\shaders\dlssnr\DlssNr_Dx12.cpp')
$pool = Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler\shaders\dlssnr\NrCompositionPool.h')

function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    Write-Output "PASS: $message"
}

Require ($header.Contains('NrOptional<bool> DlssNrMultipassEnabled { false };') -and
         $header.Contains('NrOptional<uint32_t> DlssNrPasses { 1 };')) `
    'multipass remains opt-in with one pass as the shipped default'
Require ($header.Contains('static constexpr size_t Count = 8;') -and
         $header.Contains('DlssNrExtraLayerOptions DlssNrExtraLayers;')) `
    'pass 2 compatibility plus eight extra settings records cover passes 1 through 10'
Require ($config.Contains('std::clamp(DlssNrPasses.value_or_default(), 1u, 10u)') -and
         $config.Contains('"DlssNrLayer" + std::to_string(index + 3)')) `
    'pass count is bounded and passes 3 through 10 load/save independent sections'
Require ($dx12.Contains('std::array<NrSecondLayerState, 8> layers3to10;') -and
         $dx12.Contains('for (size_t index = 0; index < 9; ++index)')) `
    'nine later passes own independent feature, history and surface state'
Require ($dx12.Contains('CreateNrFeature(PassSettings(cfg') -and
         $dx12.Contains('the pass chain waits for the next command list')) `
    'later model sessions use lifecycle-only creation frames'
Require ($dx12.Contains('for (size_t index = 1; index + 1 < healthyPassCount; ++index)') -and
         $dx12.Contains('g_lastLayerCount = static_cast<unsigned int>(index + 2);')) `
    'later passes are evaluated as a bounded sequential chain'
Require ($dx12.Contains('keeping {} completed passes') -and
         $dx12.Contains('if (layer.failed) break;')) `
    'a later-pass failure preserves the last completed image and stops the chain'
Require ($pool.Contains('MaxPasses = 10') -and
         $pool.Contains('MaxAdmissionSlots') -and
         $pool.Contains('RequiredSlots(unsigned int passes)')) `
    'composition descriptors reserve a bounded ten-pass maximum'
