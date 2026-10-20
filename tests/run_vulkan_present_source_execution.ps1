param(
 [string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [string]$Compiler='cl.exe',
 [switch]$PrepareOnly
)
$ErrorActionPreference='Stop'
# Build owner05 supplies its existing developer environment and stable output.
# PrepareOnly generates the exact production-method fixture without compilation.
$SourceRoot=(Resolve-Path -LiteralPath $SourceRoot).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$EvidenceDirectory=[IO.Path]::GetFullPath($EvidenceDirectory)
if($OutputDirectory -eq $SourceRoot -or $EvidenceDirectory -eq $SourceRoot){throw 'Use dedicated output/evidence directories'}
New-Item -ItemType Directory -Force -Path $OutputDirectory,$EvidenceDirectory | Out-Null
$pins=[ordered]@{}
function Read-Pinned([string]$relative){
 $path=Join-Path $SourceRoot $relative
 $pins[$relative]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
 return [IO.File]::ReadAllText($path).Replace("`r`n","`n")
}
function Region([string]$source,[string]$start,[string]$end){
 $first=$source.IndexOf($start,[StringComparison]::Ordinal)
 if($first -lt 0){throw "Production start missing: $start"}
 $last=$source.IndexOf($end,$first+$start.Length,[StringComparison]::Ordinal)
 if($last -le $first){throw "Production end missing: $end"}
 return $source.Substring($first,$last-$first)
}
function Function-Body([string]$source,[string]$start){
 $first=$source.IndexOf($start,[StringComparison]::Ordinal)
 if($first -lt 0){throw "Production function missing: $start"}
 $brace=$source.IndexOf('{',$first);$depth=0
 for($i=$brace;$i -lt $source.Length;$i++){
  if($source[$i] -eq '{'){$depth++}
  if($source[$i] -eq '}'){$depth--;if($depth -eq 0){return $source.Substring($first,$i-$first+1)}}
 }
 throw "Unbalanced production function: $start"
}
$executor=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentExecutor.cpp'
$types=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentExecutor.h'
$submission=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentSubmission.cpp'
$submissionTypes=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentSubmission.h'
$presentTypes=Read-Pinned 'OptiScaler/dlssnr/VulkanPresent.h'
$sessionTypes=Read-Pinned 'OptiScaler/dlssnr/VulkanNrSession.h'
$guideTypes=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentGuidesVk.h'
$template=Read-Pinned 'tests/vulkan_present_source_execution.cpp.in'
$initializationTypes=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentInitialization.h'
$privateCheck=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentCheck.inl'
# This fixture-only callback declaration follows the actual source ABI. It lets
# the new regression template run against the unchanged prior source for RED.
$callbackLifetime=if($types.Contains('const VkNrPresentModelLifetime&)>')){',const VkNrPresentModelLifetime&'}else{''}
$template=$template.Replace(' /* FIXTURE_CALLBACK_LIFETIME */',$callbackLifetime)
$before=Function-Body $executor 'VkPresentExecution VulkanPresentExecutor::BeforePresent('
# Guard ordering checks complement behavior, and fail if the seam moves.
$record=$before.IndexOf('result.record = RecordVkPresentImage(')
$guard=$before.IndexOf('if(!current())')
$forward=$before.IndexOf('slot->consumer=VulkanPresentConsumers().Register(')
$reset=$before.IndexOf('vkResetFences(')
$submit=$before.IndexOf('result.handoff = SubmitVkPresent(')
if($record -lt 0 -or $guard -le $record -or $forward -le $guard -or $reset -le $guard -or $submit -le $guard){throw 'Production late-guard ordering changed'}
$blocks=@{
 '// PRODUCTION_INITIALIZATION_ELIGIBILITY'=(Function-Body $initializationTypes 'inline bool VkPresentInitializationEligible(')
 '// PRODUCTION_INITIALIZATION_RESULTS'=(Region $initializationTypes 'enum class VkPresentInitializationStatus' 'using VkPresentPrepareModels')
 '// PRODUCTION_PRIVATE_FRAME_CHECK'=$privateCheck
 '// PRODUCTION_WAIT_BUDGET'=(Region $sessionTypes 'class VkNrWaitBudget' 'struct VkFrameRequest')
 '// PRODUCTION_IMAGE_REQUEST'=(Region $presentTypes 'struct VkPresentImageRequest' 'struct VkPresentRecordResult')
 '// PRODUCTION_RECORD_RESULT'=(Region $presentTypes 'struct VkPresentRecordResult' '// Recording commands')
 '// PRODUCTION_GUIDE_CONTRACT'=(Function-Body $guideTypes 'inline VkNrFrameContract MakeVulkanGuidePresentContract(')
 '// PRODUCTION_SUBMISSION_TYPES'=(Region $submissionTypes 'enum class VkPresentHandoffState' '// A successful return')
 '// PRODUCTION_SLOT_TYPES'=(Region $submissionTypes 'class VkPresentSlotState' '} // namespace DlssNr')
 '// PRODUCTION_SUBMISSION_FUNCTIONS'=((Region $submission 'VkResult PollOrWaitVkPresentFence(' 'bool ApplyVkPresentHandoff(')+"`n"+(Region $submission 'bool VkPresentSlotState::CanRecord(' '} // namespace DlssNr'))
 '// PRODUCTION_EXECUTOR_TYPES'=(Region $types 'struct VkPresentExecution' 'VulkanPresentExecutor& GetVulkanPresentExecutor();').Replace('  private:','  public:')
 '// PRODUCTION_FIND'=(Function-Body $executor 'VulkanPresentExecutor::Slot* VulkanPresentExecutor::Find(')
 '// PRODUCTION_COMPLETION'=(Function-Body $executor 'void VulkanPresentExecutor::ObserveSlotCompletion(')
 '// PRODUCTION_INVALIDATE'=(Function-Body $executor 'void VulkanPresentExecutor::InvalidateSourceHistory(')
 '// PRODUCTION_BEFORE_PRESENT'=$before
 '// PRODUCTION_ORIGINAL_RETURNED'=(Function-Body $executor 'void VulkanPresentExecutor::OriginalPresentReturned(')
}
foreach($entry in $blocks.GetEnumerator()){
 if(-not $template.Contains($entry.Key)){throw "Fixture marker missing: $($entry.Key)"}
 $template=$template.Replace($entry.Key,$entry.Value)
}
if($template.Contains('// PRODUCTION_')){throw 'Unexpanded production fixture marker'}
$generated=Join-Path $OutputDirectory 'vulkan_present_source_execution.cpp'
[IO.File]::WriteAllText($generated,$template)
$owner=Read-Pinned 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp'
$lifetime=Read-Pinned 'tests/vulkan_present_model_lifetime.cpp.in'
$lifetime=$lifetime.Replace('// PRODUCTION_CURRENT_LIFETIME',(Function-Body $owner 'std::optional<VkNrPresentModelLifetime> CurrentVulkanPresentModelLifetimeVk('))
$lifetime=$lifetime.Replace('// PRODUCTION_FRAME_LIFETIME',(Region $owner '        base.contract.deviceGeneration=VkNrCompletionDeviceGeneration(base.device);' '        base.contract.settingsRevision='))
$lifetime=$lifetime.Replace('// PRODUCTION_RECORDED_LIFETIME',(Region $owner '    if(base.route==VkNrRoute::Present&&result.modelRecorded)' '    result.requestedPasses=chain.requested;'))
if($lifetime.Contains('// PRODUCTION_')){throw 'Unexpanded actual owner fixture marker'}
$generatedLifetime=Join-Path $OutputDirectory 'vulkan_present_model_lifetime.cpp'
[IO.File]::WriteAllText($generatedLifetime,$lifetime)
$provider=Read-Pinned 'tests/vulkan_present_provider_guard.cpp.in'
$streamline=Read-Pinned 'OptiScaler/hooks/Streamline_Hooks.cpp'
$providerMethod=Function-Body $streamline 'DlssNr::VkPresentProviderPreparation StreamlineHooks::PrepareVulkanFullFrame('
$providerSignature=$providerMethod.Substring(0,$providerMethod.IndexOf('{')).Trim()
$providerDeclaration=$providerSignature.Replace('DlssNr::VkPresentProviderPreparation StreamlineHooks::','static DlssNr::VkPresentProviderPreparation ')+';'
$provider=$provider.Replace('// PRODUCTION_PROVIDER_DECLARATION',$providerDeclaration)
$lifetimeArgument=if($providerSignature.Contains('sourceLifetime')){',lifetime'}else{''}
$provider=$provider.Replace('/* FIXTURE_PROVIDER_LIFETIME_ARGUMENT */',$lifetimeArgument)
$provider=$provider.Replace('// PRODUCTION_PROVIDER_PREPARE',$providerMethod)
if($provider.Contains('// PRODUCTION_') -or $provider.Contains('/* FIXTURE_')){throw 'Unexpanded provider fixture marker'}
$generatedProvider=Join-Path $OutputDirectory 'vulkan_present_provider_guard.cpp'
[IO.File]::WriteAllText($generatedProvider,$provider)
$null=Read-Pinned 'OptiScaler/dlssnr/VulkanNrModelLifetime.h'
$null=Read-Pinned 'OptiScaler/dlssnr/FinalFallbackControl.h'
$null=Read-Pinned 'OptiScaler/dlssnr/VulkanNrRuntime.h'
$tests=@(
 @{name='vulkan_present_model_lifetime';sources=@($generatedLifetime,'OptiScaler/dlssnr/VulkanNrSession.cpp','OptiScaler/dlssnr/VulkanNrRuntime.cpp','OptiScaler/dlssnr/VulkanNrRecording.cpp','OptiScaler/dlssnr/VulkanPresent.cpp')},
 @{name='vulkan_present_provider_guard';sources=@($generatedProvider)},
 @{name='vulkan_present_source_plan';sources=@('tests/vulkan_present_source_plan.cpp')},
 @{name='present_source_policy';sources=@('tests/universal_connections/present_source_policy.cpp')},
 @{name='vulkan_present_status';sources=@('tests/vulkan_present_status.cpp','OptiScaler/dlssnr/VulkanPresentStatus.cpp','OptiScaler/dlssnr/VulkanNrRecording.cpp')},
 @{name='vulkan_present_source_execution';sources=@($generated)}
)
foreach($path in @('OptiScaler/dlssnr/VulkanPresentSourcePlan.h','OptiScaler/dlssnr/PresentSourcePolicy.h','OptiScaler/dlssnr/VulkanPresentStatus.h','OptiScaler/dlssnr/VulkanPresentAdmission.h','OptiScaler/dlssnr/VulkanPresentColor.h','OptiScaler/dlssnr/CapturedGuideContract.h','OptiScaler/dlssnr/DlssNr_PresentInputDecision.h')){ $null=Read-Pinned $path }
foreach($test in $tests){foreach($source in $test.sources){if(-not [IO.Path]::IsPathRooted($source)){$null=Read-Pinned $source}}}
$result=[ordered]@{internal_only=$true;scope='CPU fixture only, no GPU/model/game/provider execution';prepared=$true;compiled=$false;tests=@();source_pins=$pins;generated_sha256=(Get-FileHash -LiteralPath $generated).Hash.ToLowerInvariant();lifetime_generated_sha256=(Get-FileHash -LiteralPath $generatedLifetime).Hash.ToLowerInvariant();provider_generated_sha256=(Get-FileHash -LiteralPath $generatedProvider).Hash.ToLowerInvariant()}
if(-not $PrepareOnly){
 $compilerPath=(Get-Command $Compiler -ErrorAction Stop).Source
 Push-Location $OutputDirectory
 try{
  foreach($test in $tests){
   $sources=@($test.sources | ForEach-Object {if([IO.Path]::IsPathRooted($_)){$_}else{Join-Path $SourceRoot $_}})
   $exe=Join-Path $OutputDirectory ($test.name+'.exe')
   $args=@('/nologo','/std:c++20','/EHsc','/W4','/DNOMINMAX','/DVK_USE_PLATFORM_WIN32_KHR',('/I'+(Join-Path $SourceRoot 'OptiScaler')),('/I'+(Join-Path $SourceRoot 'external/vulkan/include')),('/I'+(Join-Path $SourceRoot 'external/nvngx_dlss_sdk')),('/I'+(Join-Path $SourceRoot 'external/streamline'))) + $sources + @("/Fe$exe",'/link','/INCREMENTAL:NO')
   $log=(& $compilerPath @args 2>&1 | Out-String);$compileExit=$LASTEXITCODE
   [IO.File]::WriteAllText((Join-Path $EvidenceDirectory ($test.name+'-compile.log')),$log)
   if($compileExit -ne 0){throw "$($test.name) compile failed: $compileExit"}
   $log=(& $exe 2>&1 | Out-String);$runExit=$LASTEXITCODE
   [IO.File]::WriteAllText((Join-Path $EvidenceDirectory ($test.name+'-run.log')),$log)
   if($runExit -ne 0){throw "$($test.name) failed: $runExit"}
   $result.tests+=@{name=$test.name;compile_exit=$compileExit;run_exit=$runExit}
  }
  $result.compiled=$true
 }finally{Pop-Location}
}
foreach($entry in $pins.GetEnumerator()){
 if((Get-FileHash -LiteralPath (Join-Path $SourceRoot $entry.Key)).Hash.ToLowerInvariant() -ne $entry.Value){throw "Source changed during fixture: $($entry.Key)"}
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'vulkan-present-source-result.json') -Encoding utf8
Write-Output $(if($PrepareOnly){'Production fixture prepared; no compilation/execution performed'}else{'Focused CPU fixtures passed; no runtime qualification inferred'})
