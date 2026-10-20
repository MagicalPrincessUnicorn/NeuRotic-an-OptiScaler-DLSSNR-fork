param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$source=Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/VulkanPresentGuidesVk.cpp')
$start=$source.IndexOf('struct GuideImages final');$end=$source.IndexOf('VulkanPresentGuides& Guides()')
$body=$source.Substring($start,$end-$start)
$start=$source.IndexOf('bool BindVulkanPresentGuides(');$end=$source.IndexOf('std::optional<VkNrFrameContract> SelectVulkanGuideMetadata(')
$bind=$source.Substring($start,$end-$start)
$fixture=(Get-Content -Raw (Join-Path $PSScriptRoot 'vulkan_guide_recording.cpp.in')).Replace('// PRODUCTION_COPY_GUIDES',$body).Replace('// PRODUCTION_BIND_GUIDES',$bind)
[IO.File]::WriteAllText((Join-Path $Output 'vulkan_guide_recording.cpp'),$fixture)
& (Join-Path $PSScriptRoot 'Run-VulkanGuideRecording.cmd') $Output
exit $LASTEXITCODE
