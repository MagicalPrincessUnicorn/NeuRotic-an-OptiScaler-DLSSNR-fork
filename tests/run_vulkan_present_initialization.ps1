param(
 [string]$SourceRoot=(Split-Path $PSScriptRoot -Parent),
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [string]$Compiler='cl.exe',
 [string[]]$IncludeDirectories=@(),
 [string[]]$Cases=@('config','ini','metadata','initializer','reset'),
 [switch]$NegativeControls,
 [switch]$PrepareOnly,
 [string]$SoftwareIcd,
 [string]$SoftwareLoaderDirectory,
 [string]$VulkanLibrary
)
$ErrorActionPreference='Stop'
$SourceRoot=(Resolve-Path -LiteralPath $SourceRoot).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$EvidenceDirectory=[IO.Path]::GetFullPath($EvidenceDirectory)
if($OutputDirectory -eq $SourceRoot -or $EvidenceDirectory -eq $SourceRoot){throw 'Dedicated output/evidence directories required'}
New-Item -ItemType Directory -Force -Path $OutputDirectory,$EvidenceDirectory | Out-Null
$pins=[ordered]@{}
function Read-Pinned([string]$relative){
 $path=Join-Path $SourceRoot $relative;$pins[$path]=(Get-FileHash -LiteralPath $path).Hash
 return [IO.File]::ReadAllText($path)
}
function Region([string]$source,[string]$start,[string]$end){
 $a=$source.IndexOf($start,[StringComparison]::Ordinal);if($a -lt 0){throw "Missing source: $start"}
 $b=$source.IndexOf($end,$a+$start.Length,[StringComparison]::Ordinal);if($b -lt 0){throw "Missing source end: $end"}
 return $source.Substring($a,$b-$a)
}
function Function-Body([string]$source,[string]$start){
 $first=$source.IndexOf($start,[StringComparison]::Ordinal);if($first -lt 0){throw "Missing function: $start"}
 $brace=$source.IndexOf('{',$first);$depth=0
 for($i=$brace;$i -lt $source.Length;$i++){
  if($source[$i] -eq '{'){$depth++}
  if($source[$i] -eq '}'){$depth--;if(!$depth){return $source.Substring($first,$i-$first+1)}}
 };throw 'Unbalanced actual source'
}
function Generate([string]$name,[string]$template,[hashtable]$blocks){
 foreach($block in $blocks.GetEnumerator()){
  if(!$template.Contains($block.Key)){throw "Missing marker: $($block.Key)"};$template=$template.Replace($block.Key,$block.Value)
 }
 if($template.Contains('// ACTUAL_')){throw "Unexpanded source: $name"}
 $path=Join-Path $OutputDirectory $name;[IO.File]::WriteAllText($path,$template)
 $pins[$path]=(Get-FileHash -LiteralPath $path).Hash;return $path
}
$config=Read-Pinned 'OptiScaler/Config.cpp'
$present=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentVk.cpp'
$types=Read-Pinned 'OptiScaler/dlssnr/VulkanPresent.h'
$feature=Read-Pinned 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp'
$check=Read-Pinned 'OptiScaler/dlssnr/VulkanPresentCheck.inl'
$load=[regex]::Match($config,'DlssNrVulkanPresentSameFrameInit\.set_from_config\(readBool\("DlssNr", "VulkanPresentSameFrameInit"\)\);').Value
$save=[regex]::Match($config,'ini\.SetValue\("DlssNr", "VulkanPresentSameFrameInit",\s*GetBoolValue\(Instance\(\)->DlssNrVulkanPresentSameFrameInit\.value_for_config\(\)\)\.c_str\(\)\);').Value
if(!$load -or !$save){throw 'Actual configuration load/save missing'}
$ini=Generate 'vulkan_present_init_ini.cpp' (Read-Pinned 'tests/vulkan_present_init_ini.cpp.in') @{
 '// ACTUAL_LOAD'=$load;'// ACTUAL_SAVE'=$save
 '// ACTUAL_READ_STRING'=(Function-Body $config 'std::optional<std::string> Config::readString(')
 '// ACTUAL_READ_BOOL'=(Function-Body $config 'std::optional<bool> Config::readBool(')
 '// ACTUAL_GET_BOOL'=(Function-Body $config 'std::string GetBoolValue(')
}
$metadata=Generate 'vulkan_present_init_metadata.cpp' (Read-Pinned 'tests/vulkan_present_init_metadata.cpp.in') @{
 '// ACTUAL_TEMPORAL'=(Function-Body $types 'inline VkNrTemporalMetadata VkPresentTemporal(')
 '// ACTUAL_CAPTURED_REQUEST'=(Region $types 'struct VkCapturedGuideInput' 'struct VkPresentRecordResult')
 '// ACTUAL_CHECK'=$check
 '// ACTUAL_MODEL_FRAME'=(Function-Body $present 'bool VkPresentPrivateFrame::ModelFrame(')
 '// ACTUAL_INITIALIZATION_REQUEST'=(Function-Body $present 'std::optional<VkPresentInitializationRequest> VkPresentPrivateFrame::InitializationRequest(')
 '// ACTUAL_EVALUATE'=(Function-Body $present 'bool VkPresentPrivateFrame::Evaluate(')
 '// ACTUAL_REQUESTED_TUNING'=(Region $feature '    const VkTuning::Settings requested {' '    if(beforeSr&&')
 '// ACTUAL_PRESENT_KEY_PREFIX'=(Region $feature '    VkNrGenerationKey key;' '    if (beforeSr) {')
 '// ACTUAL_PRESENT_KEY_SUFFIX'=(Region $feature '    key.creationFlags = createFlags;' '    auto& state = ActiveVk();')
}
$transport=Generate 'vulkan_present_initialization_transport.cpp' (Read-Pinned 'tests/vulkan_present_initialization_transport.cpp.in') @{
 '// ACTUAL_INITIALIZATION_BARRIER'=(Function-Body $present 'if (request_.initializationCompleted)')
}
$base=@('OptiScaler/dlssnr/VulkanNrGeneration.cpp','OptiScaler/dlssnr/VulkanNrRecording.cpp')
$completion=$base+@('OptiScaler/dlssnr/VulkanNrCompletion.cpp','OptiScaler/dlssnr/VulkanPresentRegistry.cpp')
$initialization='OptiScaler/dlssnr/VulkanPresentInitialization.cpp'
$modelPath=Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp'
$fixturePath=Join-Path $SourceRoot 'tests/vulkan_present_initialization.cpp'
$model=Read-Pinned 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp';$fixture=Read-Pinned 'tests/vulkan_present_initialization.cpp'
$start=$model.IndexOf('        if(result.recorded) {g_vkRuntime.Activate(VkNrRoute::Present);g_presentSession.RequestReset();}')
if($start -lt 0){throw 'Actual initializer reset effect missing'}
$end=$model.IndexOf('}', $start)+1;$effect=$model.Substring($start,$end-$start)
$cut=$fixture.IndexOf('static void EligibleOnlyWhenOptedIn()');if($cut -lt 0){throw 'Actual CPU-service fixture boundary missing'}
$template=$fixture.Substring(0,$cut)+@'
#include "dlssnr/VulkanNrRuntime.h"
int main(){
 Fixture f;
 VulkanNrRuntime g_vkRuntime;
 Check(g_vkRuntime.EnsureDevice(f.device,[]{return true;}),"actual runtime initializes fixture device");
 g_vkRuntime.Activate(VkNrRoute::Present);
 VulkanNrSession g_presentSession(VkNrRoute::Present,g_vkRuntime);
 // The existing CPU session seam uses no command or private provider. It models
 // a prior accepted inference so the pending-reset transition is observable.
 const VkFrameRequest ordinary{};
 const auto complete=[](const VkFrameRequest&,bool){return VkRecordResult{VkRecordStatus::Complete,"",true,true};};
 Check(g_presentSession.Record(ordinary,complete).status==VkRecordStatus::Complete&&!g_presentSession.ResetPending(),"prior complete inference clears its reset");
 const auto recorded=g_presentSession.RecordedCount(),completed=g_presentSession.CompletedCount();
 const auto initialized=f.initializer.Prepare(f.request,[&]{return f.current;},
  [&](const VkPresentInitializationRequest& request,VkCommandBuffer command,VkNrUseId use){
   auto result=f.PrepareModels(request,command,use);
   // ACTUAL_INITIALIZATION_RESET_EFFECT
   return result;
  },[&]{f.Poll();});
 Check(initialized.status==VkPresentInitializationStatus::Ready&&f.creates==1,"actual initializer transport completes private creation");
 Check(g_presentSession.ResetPending(),"initialization completion must preserve pending inference reset");
 Check(g_presentSession.RecordedCount()==recorded&&g_presentSession.CompletedCount()==completed,"initialization cannot count a delivered or GPU-completed application frame");
 g_presentSession.ObserveCompleted();
 Check(g_presentSession.ResetPending(),"completion of older inference cannot clear initialization reset");
 bool resetObserved=false;
 const auto inferred=g_presentSession.Record(ordinary,[&](const VkFrameRequest&,bool reset){
  resetObserved=reset;return VkRecordResult{VkRecordStatus::Complete,"",true,true};});
 Check(inferred.status==VkRecordStatus::Complete&&resetObserved&&!g_presentSession.ResetPending(),"only subsequent actual session inference acknowledges reset");
 std::puts("PASS actual initializer reset effect + actual transport and VulkanNrSession: no inference count/reset acknowledgement during initialization");
}
'@
$normal=$template.Replace('// ACTUAL_INITIALIZATION_RESET_EFFECT',$effect)
# Negative control: reproduce the forbidden acknowledgement of initialization
# as a complete session inference, using the real session API to clear its reset.
$mutant=$template.Replace('// ACTUAL_INITIALIZATION_RESET_EFFECT',$effect+"`n   if(result.recorded)g_presentSession.Record(ordinary,complete);")
$resetPositive=Join-Path $OutputDirectory 'vulkan_present_init_reset.cpp'
$resetNegative=Join-Path $OutputDirectory 'clear-reset-on-initialization.cpp'
[IO.File]::WriteAllText($resetPositive,$normal);[IO.File]::WriteAllText($resetNegative,$mutant)
$pins[$resetPositive]=(Get-FileHash -LiteralPath $resetPositive).Hash
$pins[$resetNegative]=(Get-FileHash -LiteralPath $resetNegative).Hash
$available=@{
 config=@{sources=@('tests/vulkan_present_init_config.cpp')}
 ini=@{sources=@($ini)}
 metadata=@{sources=@($metadata)+$base}
 initializer=@{sources=@('tests/vulkan_present_initialization.cpp',$initialization)+$completion}
 reset=@{sources=@($resetPositive,$initialization)+$completion+@('OptiScaler/dlssnr/VulkanNrSession.cpp','OptiScaler/dlssnr/VulkanNrRuntime.cpp')}
 transport=@{sources=@($transport,$initialization)+$completion;software=$true}
}
if($NegativeControls){
 $available['clear-reset-on-initialization']=@{sources=@($resetNegative,$initialization)+$completion+@('OptiScaler/dlssnr/VulkanNrSession.cpp','OptiScaler/dlssnr/VulkanNrRuntime.cpp');oracle='initialization completion must preserve pending inference reset'}
 $Cases+=@('clear-reset-on-initialization')
 $source=Read-Pinned $initialization
 $mutations=@(
  @{name='no-completion-proof';old='if (use_ && submitted_ && VulkanNrRecordings().GpuComplete(use_))';new='if (use_ && submitted_)';oracle='timeout retains submitted initializer'},
  @{name='renew-wait-budget';old='const auto remaining = request.waitBudget->RemainingNs();';new='const auto remaining = uint64_t{100000000};';oracle='exhausted shared budget cannot renew wait'},
  @{name='reset-pending';old='    if (use_) {';new='    if (use_) { vkResetCommandPool(device_,pool_,0);';oracle='pending command cannot reset, duplicate or infer'}
 )
 foreach($mutation in $mutations){
  if(!$source.Contains($mutation.old)){throw "Mutation source missing: $($mutation.name)"}
  $path=Join-Path $OutputDirectory ($mutation.name+'.cpp');[IO.File]::WriteAllText($path,$source.Replace($mutation.old,$mutation.new))
  $pins[$path]=(Get-FileHash -LiteralPath $path).Hash
  $available[$mutation.name]=@{sources=@('tests/vulkan_present_initialization.cpp',$path)+$completion;oracle=$mutation.oracle}
  $Cases+=@($mutation.name)
 }
}
$results=@();$failure=$null
try{
 foreach($case in $Cases){
  if(!$available.ContainsKey($case)){throw "Unknown fixture: $case"};$test=$available[$case]
  $sources=@($test.sources|ForEach-Object {
   $path=if([IO.Path]::IsPathRooted($_)){$_}else{Join-Path $SourceRoot $_}
   $pins[$path]=(Get-FileHash -LiteralPath $path).Hash;$path
  })
  if($PrepareOnly){continue}
  $tool=(Get-Command $Compiler -ErrorAction Stop).Source;$pins[$tool]=(Get-FileHash -LiteralPath $tool).Hash
  $includes=@((Join-Path $SourceRoot 'tests'),(Join-Path $SourceRoot 'OptiScaler'),(Join-Path $SourceRoot 'OptiScaler/dlssnr'),(Join-Path $SourceRoot 'OptiScaler/include'))+$IncludeDirectories
  $exe=Join-Path $OutputDirectory ($case+'.exe')
  $arguments=@('/nologo','/std:c++20','/EHsc','/W4','/UNDEBUG','/DNOMINMAX','/DVK_USE_PLATFORM_WIN32_KHR','/showIncludes')+@($includes|ForEach-Object {'/I'+$_})+$sources+@('/Fe'+$exe,'/link','/INCREMENTAL:NO')
  if($test.software){
   if(!$SoftwareIcd -or !$SoftwareLoaderDirectory -or !$VulkanLibrary){throw 'Explicit verified software ICD, loader directory and Vulkan import library required'}
   foreach($path in @($SoftwareIcd,(Join-Path $SoftwareLoaderDirectory 'vulkan-1.dll'),$VulkanLibrary)){$pins[$path]=(Get-FileHash -LiteralPath $path).Hash}
   $manifest=Get-Content -LiteralPath $SoftwareIcd -Raw|ConvertFrom-Json
   $library=$manifest.ICD.library_path
   if(![IO.Path]::IsPathRooted($library)){$library=Join-Path (Split-Path $SoftwareIcd -Parent) $library}
   $pins[$library]=(Get-FileHash -LiteralPath $library).Hash;$arguments+=@($VulkanLibrary)
  }
  Push-Location $OutputDirectory
  try{
   $log=(& $tool @arguments 2>&1|Out-String);$compileExit=$LASTEXITCODE
   [IO.File]::WriteAllText((Join-Path $EvidenceDirectory ($case+'-compile.log')),$log)
   foreach($line in ($log -split "`n")){
    if($line -match 'including file:\s*(.+?)\s*$'){$path=$Matches[1].Trim();$pins[$path]=(Get-FileHash -LiteralPath $path).Hash}
   }
   if($compileExit){throw "$case compile failed: $compileExit (not behavioral RED)"}
   $prior=@{};foreach($name in @('VK_ICD_FILENAMES','VK_DRIVER_FILES','NR_TEST_SOFTWARE_ICD','PATH')){$prior[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
   try{
    if($test.software){
     [Environment]::SetEnvironmentVariable('VK_ICD_FILENAMES',$SoftwareIcd,'Process')
     [Environment]::SetEnvironmentVariable('VK_DRIVER_FILES',$SoftwareIcd,'Process')
     [Environment]::SetEnvironmentVariable('NR_TEST_SOFTWARE_ICD',$SoftwareIcd,'Process')
     [Environment]::SetEnvironmentVariable('PATH',$SoftwareLoaderDirectory+';'+$prior.PATH,'Process')
    }
    $output=(& $exe 2>&1|Out-String);$runExit=$LASTEXITCODE
   }finally{foreach($name in $prior.Keys){[Environment]::SetEnvironmentVariable($name,$prior[$name],'Process')}}
   [IO.File]::WriteAllText((Join-Path $EvidenceDirectory ($case+'-run.log')),$output)
   $results+=@{case=$case;compile_exit=$compileExit;run_exit=$runExit;executable_sha256=(Get-FileHash -LiteralPath $exe).Hash;negative_oracle=$test.oracle}
   if($test.oracle){if(!$runExit -or !$output.Contains($test.oracle)){throw "$case did not fail its intended behavioral oracle"}}
   elseif($runExit){throw "$case failed: $runExit"}
  }finally{Pop-Location}
 }
 foreach($path in $pins.Keys){if((Get-FileHash -LiteralPath $path).Hash -ne $pins[$path]){throw "Consumed source changed: $path"}}
}catch{$failure=$_.Exception.Message;throw}
finally{
 @{prepared=$true;prepare_only=[bool]$PrepareOnly;results=$results;pins=$pins;failure=$failure;scope='CPU source-extracted services and explicitly selected software Vulkan only. No private-provider, physical-GPU, WSI or game qualification.'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'present-initialization-result.json') -Encoding utf8
}
