from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'OptiScaler/dlssnr/VulkanPresentGuidesVk.cpp').read_text(encoding='utf-8')
body=source[source.index('struct GuideImages final'):source.index('VulkanPresentGuides& Guides()')]
out=root/'builds/vulkan-experiment/tests/sampled-guides/vulkan_guide_recording.cpp'
out.parent.mkdir(parents=True,exist_ok=True)
bind=source[source.index('bool BindVulkanPresentGuides('):source.index('std::optional<VkNrFrameContract> SelectVulkanGuideMetadata(')]
ngx=(root/'OptiScaler/inputs/NVNGX_DLSS_Vk.cpp').read_text(encoding='utf-8')
a=ngx.index('            frame.ngxSrInputContract=');b=ngx.index('            frame.physicalDevice=',a)
caller=ngx[a:b]
out.write_text((root/'tests/vulkan_guide_recording.cpp.in').read_text(encoding='utf-8-sig').replace('// PRODUCTION_NGX_GUIDE_IDENTITY',caller).replace('// PRODUCTION_COPY_GUIDES',body).replace('// PRODUCTION_BIND_GUIDES',bind),encoding='utf-8')
