#include "PreparedVkDx12Transport.h"
#include <wrl/client.h>
#include <vector>
#include <cstring>
#include <limits>
namespace DlssNr::PreparedTransport
{
using Microsoft::WRL::ComPtr;
namespace {
Status No(const char* why){return {false,why};}
Status Yes(){return {true,{}};}
VkImageUsageFlags Usage(const ImageSpec& s) {return VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
    VK_IMAGE_USAGE_SAMPLED_BIT|(s.storage?VK_IMAGE_USAGE_STORAGE_BIT:0);}
bool CompleteDispatch(const Dispatch& d) {return d.getProperties2&&d.getImageFormatProperties2&&d.getExternalSemaphoreProperties&&
    d.getMemoryProperties&&d.getHandleProperties&&d.createImage&&d.destroyImage&&d.getImageMemoryRequirements&&
    d.allocateMemory&&d.freeMemory&&d.bindImageMemory&&d.createSemaphore&&d.destroySemaphore&&d.importSemaphore&&d.barrier&&d.copyImage;}
Status PhysicalIdentity(VkPhysicalDevice p,const Dispatch& d,LUID& luid) {
    if(!p||!d.getProperties2)return No("Vulkan physical-device identity unavailable");
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&id;
    d.getProperties2(p,&properties);
    // A multi-node import needs per-node heap/queue qualification. This utility
    // explicitly supports the single-node envelope rather than guessing.
    if(!id.deviceLUIDValid||id.deviceNodeMask!=1)return No("Vulkan adapter LUID/single-node identity unavailable");
    static_assert(sizeof(luid)==VK_LUID_SIZE);std::memcpy(&luid,id.deviceLUID,sizeof(luid));return Yes();
}
}
VkFormat Format(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:return VK_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM:return VK_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:return VK_FORMAT_R16G16B16A16_SFLOAT;
    case DXGI_FORMAT_R16G16_FLOAT:return VK_FORMAT_R16G16_SFLOAT;
    case DXGI_FORMAT_R32G32_FLOAT:return VK_FORMAT_R32G32_SFLOAT;
    case DXGI_FORMAT_R32_FLOAT:return VK_FORMAT_R32_SFLOAT;
    case DXGI_FORMAT_R8_UNORM:return VK_FORMAT_R8_UNORM;
    default:return VK_FORMAT_UNDEFINED;
    }
}
bool SameLuid(const LUID& a,const LUID& b)noexcept{return a.LowPart==b.LowPart&&a.HighPart==b.HighPart;}
std::optional<std::uint32_t> ChooseMemoryType(const VkPhysicalDeviceMemoryProperties& properties,std::uint32_t imageBits,std::uint32_t handleBits) {
    for(std::uint32_t i=0;i<properties.memoryTypeCount&&i<32;++i)
        if((imageBits&handleBits&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))return i;
    return {};
}
Status QuerySupport(VkPhysicalDevice physical,const Dispatch& d,const Enabled& enabled,const ImageSpec& spec) {
    if(!physical||(!spec.rawBytes&&!d.getImageFormatProperties2)||!d.getExternalSemaphoreProperties)return No("External-image/fence support query unavailable");
    if(!enabled.externalMemoryWin32||!enabled.externalSemaphoreWin32||!enabled.timeline)return No("Existing Vulkan device did not enable transport capabilities");
    if(spec.rawBytes){
        if(spec.rawBytes>64ull*1024*1024||spec.rawBytes%4||spec.width||spec.height||spec.format!=DXGI_FORMAT_UNKNOWN||!d.getExternalBufferProperties)
            return No("Unsupported bounded raw buffer or query unavailable");
        VkPhysicalDeviceExternalBufferInfo info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
        info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;info.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        VkExternalBufferProperties properties{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};d.getExternalBufferProperties(physical,&info,&properties);
        const auto& m=properties.externalMemoryProperties;
        if(!(m.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)||!(m.compatibleHandleTypes&info.handleType))return No("D3D12 raw buffer import unsupported");
    }else{
    if(!spec.width||!spec.height||Format(spec.format)==VK_FORMAT_UNDEFINED)return No("Unsupported shared-image shape or typed format");
    VkPhysicalDeviceExternalImageFormatInfo external{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    external.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    VkPhysicalDeviceImageFormatInfo2 info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};info.pNext=&external;
    info.format=Format(spec.format);info.type=VK_IMAGE_TYPE_2D;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=Usage(spec);
    VkExternalImageFormatProperties externalProperties{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 properties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};properties.pNext=&externalProperties;
    if(d.getImageFormatProperties2(physical,&info,&properties)!=VK_SUCCESS)return No("D3D12 image import format/usage query refused");
    const auto& memory=externalProperties.externalMemoryProperties;
    if(!(memory.externalMemoryFeatures&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)||
       !(memory.compatibleHandleTypes&VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT))return No("D3D12 image import unsupported");
    if(spec.width>properties.imageFormatProperties.maxExtent.width||spec.height>properties.imageFormatProperties.maxExtent.height||
       !(properties.imageFormatProperties.sampleCounts&VK_SAMPLE_COUNT_1_BIT))return No("Shared image exceeds Vulkan format limits");
    }
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
    VkPhysicalDeviceExternalSemaphoreInfo fenceInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};fenceInfo.pNext=&type;
    fenceInfo.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    VkExternalSemaphoreProperties fenceProperties{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
    d.getExternalSemaphoreProperties(physical,&fenceInfo,&fenceProperties);
    if(!(fenceProperties.externalSemaphoreFeatures&VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)||
       !(fenceProperties.compatibleHandleTypes&VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT))return No("D3D12 timeline fence import unsupported");
    return Yes();
}
Status MatchAdapter(VkPhysicalDevice p,const Dispatch& d,IDXGIFactory4* factory,IDXGIAdapter1** output) {
    if(!output)return No("Adapter output unavailable");*output=nullptr;
    if(!factory)return No("DXGI factory unavailable");LUID luid{};auto identity=PhysicalIdentity(p,d,luid);if(!identity)return identity;
    if(FAILED(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(output))))return No("Vulkan physical device has no matching DXGI adapter");
    return Yes();
}
struct Session::Impl {
    struct Image {ComPtr<ID3D12Resource> resource;VkImage image=VK_NULL_HANDLE;VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;
        ImageSpec spec{};std::uint64_t bytes=0;enum class State{Initial,External,Vulkan};State state=State::Initial;};
    VkDevice device=VK_NULL_HANDLE;VkQueue queue=VK_NULL_HANDLE;std::uint32_t family=UINT32_MAX;Dispatch dispatch{};ComPtr<ID3D12Device> d12;
    ComPtr<ID3D12Fence> input,output;VkSemaphore inputSemaphore=VK_NULL_HANDLE,outputSemaphore=VK_NULL_HANDLE;
    std::vector<Image> images;bool referenced=false;
    ~Impl() {
        for(auto& image:images){if(image.image)dispatch.destroyImage(device,image.image,nullptr);if(image.buffer)dispatch.destroyBuffer(device,image.buffer,nullptr);if(image.memory)dispatch.freeMemory(device,image.memory,nullptr);}
        if(inputSemaphore)dispatch.destroySemaphore(device,inputSemaphore,nullptr);
        if(outputSemaphore)dispatch.destroySemaphore(device,outputSemaphore,nullptr);
    }
    Status Fence(ComPtr<ID3D12Fence>& fence,VkSemaphore& semaphore) {
        if(FAILED(d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence))))return No("D3D12 shared fence creation refused");
        HANDLE handle=nullptr;if(FAILED(d12->CreateSharedHandle(fence.Get(),nullptr,GENERIC_ALL,nullptr,&handle)))return No("D3D12 fence handle creation refused");
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};create.pNext=&type;
        auto result=dispatch.createSemaphore(device,&create,nullptr,&semaphore);
        if(result==VK_SUCCESS){VkImportSemaphoreWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
            import.semaphore=semaphore;import.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;import.handle=handle;
            result=dispatch.importSemaphore(device,&import);}
        CloseHandle(handle);return result==VK_SUCCESS?Yes():No("Vulkan shared fence import refused");
    }
    Status MakeImage(VkPhysicalDevice physical,const ImageSpec& spec) {
        if(spec.rawBytes)return MakeBuffer(physical,spec);
        images.emplace_back();auto& image=images.back();image.spec=spec;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=spec.width;desc.Height=spec.height;
        desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=spec.format;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags=spec.storage?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{spec.format};
        if(FAILED(d12->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
           !(support.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D)||
           (spec.storage&&!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)))return No("D3D12 format/typed UAV store unsupported");
        auto allocation=d12->GetResourceAllocationInfo(0,1,&desc);image.bytes=allocation.SizeInBytes;
        if(!image.bytes||image.bytes==UINT64_MAX)return No("D3D12 allocation size unavailable");
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=1;heap.VisibleNodeMask=1;
        if(FAILED(d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,
            IID_PPV_ARGS(&image.resource))))return No("D3D12 shared image creation refused");
        HANDLE handle=nullptr;if(FAILED(d12->CreateSharedHandle(image.resource.Get(),nullptr,GENERIC_ALL,nullptr,&handle)))return No("D3D12 image handle creation refused");
        struct Close {HANDLE handle;~Close(){CloseHandle(handle);}} close{handle};
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};create.pNext=&external;create.imageType=VK_IMAGE_TYPE_2D;
        create.format=Format(spec.format);create.extent={spec.width,spec.height,1};create.mipLevels=1;create.arrayLayers=1;
        create.samples=VK_SAMPLE_COUNT_1_BIT;create.tiling=VK_IMAGE_TILING_OPTIMAL;create.usage=Usage(spec);
        create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;create.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        if(dispatch.createImage(device,&create,nullptr,&image.image)!=VK_SUCCESS)return No("Vulkan external image creation refused");
        VkMemoryRequirements required{};dispatch.getImageMemoryRequirements(device,image.image,&required);
        VkMemoryWin32HandlePropertiesKHR imported{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        if(dispatch.getHandleProperties(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,handle,&imported)!=VK_SUCCESS)return No("Imported memory properties unavailable");
        VkPhysicalDeviceMemoryProperties memory{};dispatch.getMemoryProperties(physical,&memory);
        auto type=ChooseMemoryType(memory,required.memoryTypeBits,imported.memoryTypeBits);
        if(!type||required.size>image.bytes)return No("No compatible device-local imported allocation");
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=image.image;
        VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};import.pNext=&dedicated;
        import.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;import.handle=handle;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.pNext=&import;allocate.allocationSize=image.bytes;allocate.memoryTypeIndex=*type;
        if(dispatch.allocateMemory(device,&allocate,nullptr,&image.memory)!=VK_SUCCESS)return No("Vulkan dedicated external allocation refused");
        if(dispatch.bindImageMemory(device,image.image,image.memory,0)!=VK_SUCCESS)return No("Vulkan imported memory binding refused");
        return Yes();
    }
    Status MakeBuffer(VkPhysicalDevice physical,const ImageSpec& spec){
        if(!dispatch.createBuffer||!dispatch.destroyBuffer||!dispatch.getBufferMemoryRequirements||!dispatch.bindBufferMemory)return No("Raw buffer dispatch unavailable");
        images.emplace_back();auto& b=images.back();b.spec=spec;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=spec.rawBytes;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto allocation=d12->GetResourceAllocationInfo(0,1,&desc);b.bytes=allocation.SizeInBytes;
        if(!b.bytes||b.bytes>64ull*1024*1024)return No("Shared buffer allocation bound refused");
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        if(FAILED(d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&b.resource))))return No("Shared raw resource creation refused");
        HANDLE handle=nullptr;if(FAILED(d12->CreateSharedHandle(b.resource.Get(),nullptr,GENERIC_ALL,nullptr,&handle)))return No("Shared raw handle creation refused");
        struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{handle};
        VkExternalMemoryBufferCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};create.pNext=&external;create.size=spec.rawBytes;
        create.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(dispatch.createBuffer(device,&create,nullptr,&b.buffer)!=VK_SUCCESS)return No("Vulkan shared raw buffer creation refused");
        VkMemoryRequirements required{};dispatch.getBufferMemoryRequirements(device,b.buffer,&required);
        VkMemoryWin32HandlePropertiesKHR imported{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        if(dispatch.getHandleProperties(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,handle,&imported)!=VK_SUCCESS)return No("Shared raw memory properties unavailable");
        VkPhysicalDeviceMemoryProperties memory{};dispatch.getMemoryProperties(physical,&memory);
        const auto type=ChooseMemoryType(memory,required.memoryTypeBits,imported.memoryTypeBits);
        if(!type||required.size>b.bytes)return No("Shared raw allocation incompatible");
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.buffer=b.buffer;
        VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};import.pNext=&dedicated;import.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;import.handle=handle;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.pNext=&import;allocate.allocationSize=b.bytes;allocate.memoryTypeIndex=*type;
        if(dispatch.allocateMemory(device,&allocate,nullptr,&b.memory)!=VK_SUCCESS||dispatch.bindBufferMemory(device,b.buffer,b.memory,0)!=VK_SUCCESS)return No("Shared raw import/binding refused");
        return Yes();
    }
};
Session::Session()=default;
Session::~Session(){
    // Existing completion/recording owners must retain this Session through
    // retirement. A violated ownership contract leaks this bounded native packet
    // deliberately; destruction must never free possible live GPU references.
    if(impl_&&impl_->referenced)(void)impl_.release();
}
Status Session::Initialize(VkPhysicalDevice physical,VkDevice device,VkQueue queue,std::uint32_t family,ID3D12Device* d12,
    const Dispatch& d,const Enabled& enabled,std::span<const ImageSpec> specs) {
    if(impl_)return No("Transport session already initialized; explicit retirement required");
    if(!device||!queue||!d12||family==UINT32_MAX||specs.empty()||specs.size()>5||!CompleteDispatch(d))return No("Transport device/queue/dispatch or bounded image set unavailable");
    LUID luid{};auto identity=PhysicalIdentity(physical,d,luid);if(!identity)return identity;
    if(d12->GetNodeCount()!=1||!SameLuid(luid,d12->GetAdapterLuid()))return No("Vulkan and D3D12 devices are not on the same single-node adapter");
    std::uint64_t rawTotal=0;
    for(const auto& spec:specs){auto supported=QuerySupport(physical,d,enabled,spec);if(!supported)return supported;
        rawTotal+=spec.rawBytes;if(rawTotal>192ull*1024*1024)return No("Shared raw packet exceeds bounded memory");}
    auto next=std::make_unique<Impl>();next->device=device;next->queue=queue;next->family=family;next->dispatch=d;next->d12=d12;
    for(const auto& spec:specs){auto made=next->MakeImage(physical,spec);if(!made)return made;}
    auto input=next->Fence(next->input,next->inputSemaphore);if(!input)return input;
    auto output=next->Fence(next->output,next->outputSemaphore);if(!output)return output;
    impl_=std::move(next);return Yes();
}
std::size_t Session::Count()const{return impl_?impl_->images.size():0;}
VkDevice Session::Device()const{return impl_?impl_->device:VK_NULL_HANDLE;}
VkQueue Session::Queue()const{return impl_?impl_->queue:VK_NULL_HANDLE;}
std::uint32_t Session::QueueFamily()const{return impl_?impl_->family:UINT32_MAX;}
ImageView Session::Image(std::size_t i)const{if(!impl_||i>=Count())return {};const auto& image=impl_->images[i];return {image.image,image.resource.Get(),image.spec,image.bytes};}
VkBuffer Session::Buffer(std::size_t i)const{return impl_&&i<Count()?impl_->images[i].buffer:VK_NULL_HANDLE;}
VkSemaphore Session::InputSemaphore()const{return impl_?impl_->inputSemaphore:VK_NULL_HANDLE;}
VkSemaphore Session::OutputSemaphore()const{return impl_?impl_->outputSemaphore:VK_NULL_HANDLE;}
ID3D12Fence* Session::InputFence()const{return impl_?impl_->input.Get():nullptr;}
ID3D12Fence* Session::OutputFence()const{return impl_?impl_->output.Get():nullptr;}
bool Session::MarkReferenced(){if(!impl_)return false;impl_->referenced=true;return true;}
bool Session::PendingOwnership()const{return impl_&&impl_->referenced;}
Status Session::Acquire(VkCommandBuffer command,std::size_t i,bool initial) {
    if(!impl_||!command||i>=Count())return No("Transport acquisition arguments unavailable");
    auto& image=impl_->images[i];
    if(image.state!=(initial?Impl::Image::State::Initial:Impl::Image::State::External))return No("Transport image ownership does not match acquisition");
    if(image.buffer){VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;b.dstQueueFamilyIndex=impl_->family;
        b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;b.buffer=image.buffer;b.size=VK_WHOLE_SIZE;MarkReferenced();
        impl_->dispatch.barrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&b,0,nullptr);
        image.state=Impl::Image::State::Vulkan;return Yes();}
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=initial?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;barrier.dstQueueFamilyIndex=impl_->family;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;barrier.image=image.image;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};MarkReferenced();
    impl_->dispatch.barrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    image.state=Impl::Image::State::Vulkan;return Yes();
}
Status Session::Release(VkCommandBuffer command,std::size_t i) {
    if(!impl_||!command||i>=Count())return No("Transport release arguments unavailable");
    auto& image=impl_->images[i];if(image.state!=Impl::Image::State::Vulkan)return No("Transport image is not acquired for this queue family");
    if(image.buffer){VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcQueueFamilyIndex=impl_->family;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;
        b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;b.buffer=image.buffer;b.size=VK_WHOLE_SIZE;MarkReferenced();
        impl_->dispatch.barrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,1,&b,0,nullptr);
        image.state=Impl::Image::State::External;return Yes();}
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex=impl_->family;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;barrier.image=image.image;
    barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};MarkReferenced();
    impl_->dispatch.barrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    image.state=Impl::Image::State::External;return Yes();
}
Status Session::CopyFrom(VkCommandBuffer command,std::size_t i,VkImage source,VkImageLayout layout,std::uint32_t width,std::uint32_t height) {
    if(!impl_||!command||!source||i>=Count()||!width||!height)return No("Transport source copy arguments unavailable");
    auto& image=impl_->images[i];if(!image.image||image.state!=Impl::Image::State::Vulkan||width>image.spec.width||height>image.spec.height||source==image.image||
        (layout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL&&layout!=VK_IMAGE_LAYOUT_GENERAL))return No("Transport source copy ownership/layout/extent refused");
    VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={width,height,1};MarkReferenced();
    impl_->dispatch.copyImage(command,source,layout,image.image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);return Yes();
}
Status Session::CopyTo(VkCommandBuffer command,std::size_t i,VkImage destination,VkImageLayout layout,std::uint32_t width,std::uint32_t height) {
    if(!impl_||!command||!destination||i>=Count()||!width||!height)return No("Transport destination copy arguments unavailable");
    auto& image=impl_->images[i];if(!image.image||image.state!=Impl::Image::State::Vulkan||width>image.spec.width||height>image.spec.height||destination==image.image||
        (layout!=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&layout!=VK_IMAGE_LAYOUT_GENERAL))return No("Transport destination copy ownership/layout/extent refused");
    VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={width,height,1};MarkReferenced();
    impl_->dispatch.copyImage(command,image.image,VK_IMAGE_LAYOUT_GENERAL,destination,layout,1,&copy);return Yes();
}
Status Session::Retire(const DrainEvidence& evidence) {
    if(!CanRetire(evidence))return No("Transport drain/replay/provider ownership not proven");
    impl_.reset();return Yes();
}
}
