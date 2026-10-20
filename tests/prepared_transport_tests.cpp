#include <dlssnr/PreparedVkDx12Transport.h>
#include <dlssnr/PreparedTransportProtocol.h>
#include "../addons/prepared-guides/src/SharedGpuClient.h"
#include <cassert>
#include <iostream>
using namespace DlssNr::PreparedTransport;
namespace P=Neurotic::Feed::Prepared;
namespace I=Neurotic::Feed::Prepared::Ipc;
static bool imageImport=true, semaphoreImport=true;
static VKAPI_ATTR void VKAPI_CALL BufferSupport(VkPhysicalDevice,const VkPhysicalDeviceExternalBufferInfo* info,VkExternalBufferProperties* p){
    assert(info->handleType==VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT);
    assert(info->usage==(VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    p->externalMemoryProperties.externalMemoryFeatures=imageImport?VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT:0;
    p->externalMemoryProperties.compatibleHandleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
}
static VKAPI_ATTR VkResult VKAPI_CALL Format(VkPhysicalDevice,const VkPhysicalDeviceImageFormatInfo2*,VkImageFormatProperties2* p)
{
    p->imageFormatProperties.maxExtent={4096,4096,1};
    p->imageFormatProperties.sampleCounts=VK_SAMPLE_COUNT_1_BIT;
    auto* e=static_cast<VkExternalImageFormatProperties*>(p->pNext);
    e->externalMemoryProperties.externalMemoryFeatures=imageImport?VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT:0;
    e->externalMemoryProperties.compatibleHandleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL Semaphore(VkPhysicalDevice,const VkPhysicalDeviceExternalSemaphoreInfo*,VkExternalSemaphoreProperties* p)
{
    p->externalSemaphoreFeatures=semaphoreImport?VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT:0;
    p->compatibleHandleTypes=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
}
int main()
{
    namespace A=reshade::api;
    P::Descriptor shared;shared.producer=1;shared.session=2;shared.stream=3;shared.device=4;shared.generation=5;shared.capture=6;
    shared.sourceApi=P::C::GraphicsApi::Vulkan;shared.flags=P::Reset|P::HasDistrust|P::ZeroJitterPolicy;shared.maskCapture=6;
    shared.color=shared.depth=shared.motion=shared.distrust={1920,1080,0,0,1920,1080};
    std::array<A::resource_desc,4> shapes{};
    const std::array<A::format,4> formats{A::format::r8g8b8a8_unorm,A::format::r32_float,A::format::r16g16_float,A::format::r8_unorm};
    for(unsigned i=0;i<4;++i)shapes[i]=A::resource_desc(1920,1080,1,1,formats[i],1,A::memory_heap::default_,A::resource_usage::copy_source);
    std::string why;assert(nrpg::ValidateSharedInput(shared,shapes,why));
    auto changed=shared;changed.maskCapture=5;assert(!nrpg::ValidateSharedInput(changed,shapes,why));
    changed=shared;changed.sourceApi=P::C::GraphicsApi::D3D12;assert(!nrpg::ValidateSharedInput(changed,shapes,why));
    changed=shared;changed.color.x=1;assert(!nrpg::ValidateSharedInput(changed,shapes,why));
    changed=shared;changed.colorDomain=P::C::ColorDomain::DisplayLinear;assert(!nrpg::ValidateSharedInput(changed,shapes,why));
    shapes[0].texture.format=A::format::b8g8r8a8_unorm;assert(!nrpg::ValidateSharedInput(shared,shapes,why));shapes[0].texture.format=formats[0];
    shapes[1].texture.samples=4;assert(!nrpg::ValidateSharedInput(shared,shapes,why));shapes[1].texture.samples=1;
    shapes[2].texture.width--;assert(!nrpg::ValidateSharedInput(shared,shapes,why));shapes[2].texture.width++;
    I::Frame frame{};frame.guides.producer=1;frame.guides.session=2;frame.guides.stream=3;
    frame.guides.device=4;frame.guides.generation=5;frame.guides.capture=6;
    assert(I::Valid(frame));
    auto wrong=frame;wrong.header.bytes++;assert(!I::Valid(wrong));
    wrong=frame;wrong.guides.reserved[0]=1;assert(!I::Valid(wrong));
    I::Result result{};result.identity=I::IdentityOf(frame.guides);
    result.outputCapture=6;result.state=I::Outcome::CompletedSuccess;
    result.facts=I::Recorded|I::Submitted|I::GpuCompleted|I::OutputValid;
    result.completionValue=20;
    assert(I::AcceptCompleted(frame,result));
    result.identity.generation++;assert(!I::AcceptCompleted(frame,result));result.identity.generation--;
    result.state=I::Outcome::Failed;assert(!I::AcceptCompleted(frame,result));
    result.state=I::Outcome::CompletedSuccess;result.facts=I::Recorded|I::Submitted;
    assert(!I::AcceptCompleted(frame,result));
    result.facts=I::Recorded|I::Submitted|I::GpuCompleted|I::OutputValid;
    result.outputCapture=5;assert(!I::AcceptCompleted(frame,result));
    result.state=I::Outcome::Failed;result.facts=I::GpuCompleted;assert(!I::Valid(result));
    VkPhysicalDeviceMemoryProperties memory{};memory.memoryTypeCount=2;
    memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memory.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    assert(ChooseMemoryType(memory,3,2)==1u);assert(!ChooseMemoryType(memory,1,1));
    LUID a{1,2},b{1,2},c{2,2};assert(SameLuid(a,b));assert(!SameLuid(a,c));
    assert(!CanRetire({true,false,true,true}));assert(CanRetire({true,true,true,true}));
    Dispatch dispatch{};dispatch.getImageFormatProperties2=Format;dispatch.getExternalSemaphoreProperties=Semaphore;
    Enabled enabled{true,true,true};ImageSpec spec{1920,1080,DXGI_FORMAT_R8G8B8A8_UNORM,false};
    auto physical=reinterpret_cast<VkPhysicalDevice>(uintptr_t(1));
    assert(QuerySupport(physical,dispatch,enabled,spec));
    imageImport=false;assert(!QuerySupport(physical,dispatch,enabled,spec));imageImport=true;
    semaphoreImport=false;assert(!QuerySupport(physical,dispatch,enabled,spec));semaphoreImport=true;
    enabled.timeline=false;assert(!QuerySupport(physical,dispatch,enabled,spec));enabled.timeline=true;
    spec.width=8192;assert(!QuerySupport(physical,dispatch,enabled,spec));spec.width=1920;
    spec.format=DXGI_FORMAT_UNKNOWN;assert(!QuerySupport(physical,dispatch,enabled,spec));
    dispatch.getExternalBufferProperties=BufferSupport;
    ImageSpec raw{};raw.rawBytes=3840ull*2160*4;
    assert(QuerySupport(physical,dispatch,enabled,raw));
    imageImport=false;assert(!QuerySupport(physical,dispatch,enabled,raw));imageImport=true;
    raw.rawBytes=UINT64_MAX;assert(!QuerySupport(physical,dispatch,enabled,raw));
    Session session;assert(!session.Retire({false,false,false,false}));
    std::cout << "prepared transport protocol/support/retirement checks passed\n";
}
