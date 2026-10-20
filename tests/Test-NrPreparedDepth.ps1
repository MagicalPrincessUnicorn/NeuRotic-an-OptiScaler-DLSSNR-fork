$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$owner = Get-Content -Raw (Join-Path $root 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp')
$helper = Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/NrPreparedDepth.h')
$config = Get-Content -Raw (Join-Path $root 'OptiScaler/Config.h')
$adapter = Get-Content -Raw (Join-Path $root 'OptiScaler/inputs/FSR3_Dx12.cpp')
function Check([bool]$value, [string]$message) {
    if (!$value) { throw $message }
    Write-Output "PASS source contract: $message"
}
Check ($owner.Contains('!privateCommandList && preparedDepthEnabled') -and $owner.Contains('ExperimentalPolicy::Capture(cfg).preparedDepth')) 'direct Native opt-in boundary'
Check ($config.Contains('NrOptional<int32_t, NoDefault> DepthResourceBarrier') -and
    ([regex]::Matches($adapter,'DepthResourceBarrier\.set_volatile_value_if_changed\(').Count -eq 2)) 'all live depth adapter writes use synchronized value-only arrival with unchanged-write suppression'
Check ($owner.Contains('Native.PreparedDepthRepresentationNotEnrolled')) 'unenrolled protocol remains refused'
Check ($owner.Contains('SetNativeArrival(request,') -and $owner.Contains('cfg.depthResourceArrival') -and
    !$owner.Contains('request.arrivalKnown = true')) 'prepared guide preserves captured adapter arrival declaration'
$prepare = $owner.IndexOf('preparedDepth = DlssNr::PreparedDepth::Prepare(')
$refusal = $owner.IndexOf('if (!depthIn)', $prepare)
$gate = $owner.IndexOf('!DlssNr::PreparedDepth::CanUse(preparedDepth)', $refusal)
$model = $owner.IndexOf('result = static_cast<int>(DlssNr::Proxy::Run(', $gate)
Check ($prepare -gt 0 -and $refusal -gt $prepare -and $gate -gt $refusal -and $model -gt $gate) 'refusal and currentness gate precede model wrapper'
Check ($helper.Contains('GpuSafety::Reusable(slot.use)') -and $helper.Contains('GpuSafety::BeginLocalAction')) 'existing completion and recording authority'
Check (!$helper.Contains('CopyResource(') -and !$helper.Contains('ALL_SUBRESOURCES') -and
    $helper.Contains('from.SubresourceIndex=0') -and $helper.Contains('to.SubresourceIndex=0')) 'selected mip-zero copy only'
Check ($owner.Contains('ParkNrResource(slot.resource)') -and $owner.Contains('stage=owner-reusable-observed')) 'existing retirement authority and truthful evidence'
Check ($owner.Contains('stage=wrapper-selection callableSelected={}') -and
    $owner.Contains('stage=wrapper-result callableSelected={}') -and
    $owner.Contains('stage=returned recorded={}')) 'wrapper selection, result and resolve remain distinct'
Check ($owner.Contains('preparedDepth.copyRecorded') -and
    $helper.Contains('recording-revoked-after-copy')) 'recorded work is retained even after refusal'
$entry=$owner.IndexOf('ReportDepthEligibility({preparedDepthEnabled')
Check ($entry -gt 0 -and $entry -lt $prepare -and $owner.Contains('NR_DEPTH_ELIGIBILITY v=1 session={} pid={}')) 'one-mip and policy-off descriptors observed before preparation'
$report=$owner.Substring($owner.IndexOf('void ReportDepthEligibility('), $owner.IndexOf('void DlssNr_Dx12::Dispatch(')-$owner.IndexOf('void ReportDepthEligibility('))
Check (!$report.Contains('FrameTrace::Accepts') -and !$report.Contains('NR_FRAME_TRACE(') -and $report.Contains('if (!session.armed) return;') -and $report.Contains('catch (...)')) 'eligibility has an independent bound and cannot throw into rendering'
