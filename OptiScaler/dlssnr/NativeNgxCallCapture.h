#pragma once
#include "NativeIdentity.h"
#include "StreamlineSourceScope.h"
#include <inputs/universal_feeder/PreparedNgxOrigin.h>
#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>

namespace Neurotic::Lifecycle { class NativeProcessBootstrap; }
namespace DlssNr
{
template<class Feature> class NativeFeatureRegistry;
// Original parameters at the actual outer NGX boundary, before any redirection.
// Retained COM interfaces prevent address/lifetime ambiguity while the callback
// owns this capture. They grant no GPU use, content revision or producer/final
// lineage. Later operation owners must retain their own complete C03 obligations.
class NativeNgxCallCapture
{
    template<class Feature> friend class NativeFeatureRegistry;
    friend class Neurotic::Lifecycle::NativeProcessBootstrap;
#ifdef NR_SPECTRE_SOURCE_TESTING
    friend class NativeNgxCallCaptureTestAccess;
#endif
    inline static constexpr std::array integerKeys={"DLSS.Feature.Create.Flags","Width","Height","OutWidth","OutHeight",
        "PerfQualityValue","Reset","DLSS.Render.Subrect.Dimensions.Width","DLSS.Render.Subrect.Dimensions.Height",
        "DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y","DLSS.Input.Depth.Subrect.Base.X",
        "DLSS.Input.Depth.Subrect.Base.Y","DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y",
        "DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y"};
    inline static constexpr std::array floatKeys={"Jitter.Offset.X","Jitter.Offset.Y","MV.Scale.X","MV.Scale.Y",
        "DLSS.Pre.Exposure","DLSS.Exposure.Scale"};
    inline static constexpr std::array resourceKeys={"Color","Output","Depth","MotionVectors","ExposureTexture","DLSS.Input.Bias.Current.Color.Mask"};
    struct ResourceCapture
    {
        std::optional<void*> raw;
        Microsoft::WRL::ComPtr<IUnknown> original;
        Microsoft::WRL::ComPtr<ID3D12Resource> native;
        D3D12_RESOURCE_DESC description{};
        std::optional<D3D12_FEATURE_DATA_FORMAT_SUPPORT> formatSupport;
    };
    std::array<std::optional<unsigned>,integerKeys.size()> integers_;
    std::array<std::optional<float>,floatKeys.size()> floats_;
    std::array<ResourceCapture,resourceKeys.size()> resources_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    std::shared_ptr<const StreamlineSourceScope::Observation> sourceFrame_;
    bool preparedSource_=false;
    NativeNgxCallCapture()=default;
    template<class Parameters,class Result>static std::unique_ptr<NativeNgxCallCapture> Capture(
        ID3D12GraphicsCommandList* list,Parameters* parameters,Result success)noexcept
    {
        try
        {
            if(!list||!parameters)return {};
            auto capture=std::unique_ptr<NativeNgxCallCapture>(new NativeNgxCallCapture);
            capture->list_=NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list).object;
            if(!capture->list_||capture->list_->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||
                FAILED(capture->list_->GetDevice(IID_PPV_ARGS(&capture->device_))))return {};
            // Capture the actual enclosing API call before any parameter-owner
            // Get can reenter. This observation is not a canonical game frame.
            capture->sourceFrame_=StreamlineSourceScope::CaptureFor(capture->list_.Get());
            capture->preparedSource_=Neurotic::Feed::DeclaresPreparedNgxSource(parameters,success);
            for(std::size_t i=0;i<integerKeys.size();++i)
            {unsigned value=0;if(parameters->Get(integerKeys[i],&value)==success)capture->integers_[i]=value;}
            for(std::size_t i=0;i<floatKeys.size();++i)
            {float value=0;if(parameters->Get(floatKeys[i],&value)==success)capture->floats_[i]=value;}
            for(std::size_t i=0;i<resourceKeys.size();++i)
            {
                auto& retained=capture->resources_[i];void* value=nullptr;
                if(parameters->Get(resourceKeys[i],&value)==success)retained.raw=value;
                if(!retained.raw||!value){if(i<4)return {};continue;}
                retained.original=static_cast<IUnknown*>(value);
                retained.native=NativeIdentity::Resolve<ID3D12Resource>(retained.original.Get()).object;
                Microsoft::WRL::ComPtr<ID3D12Device> device;
                if(!retained.native||FAILED(retained.native->GetDevice(IID_PPV_ARGS(&device)))||
                    !NativeIdentity::CompareDevices(device.Get(),capture->device_.Get()).equal)return {};
                retained.description=retained.native->GetDesc();
                D3D12_FEATURE_DATA_FORMAT_SUPPORT support{retained.description.Format};
                if(SUCCEEDED(capture->device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))))
                    retained.formatSupport=support;
            }
            return capture;
        }
        catch(...){return {};}
    }
  public:
    bool DeclaresPreparedSource()const noexcept{return preparedSource_;}
    bool SameTemporalMetadata(const NativeNgxCallCapture& other)const noexcept {
        if(preparedSource_!=other.preparedSource_||integers_!=other.integers_)return false;
        for(std::size_t i=0;i<floats_.size();++i) {
            if(floats_[i].has_value()!=other.floats_[i].has_value())return false;
            if(floats_[i]&&std::bit_cast<std::uint32_t>(*floats_[i])!=std::bit_cast<std::uint32_t>(*other.floats_[i]))return false;
        }
        for(std::size_t i=0;i<resources_.size();++i)
            if(resources_[i].native!=other.resources_[i].native)return false;
        return true;
    }
    struct CallerOutputRegionValue
    {
        // The capture retains this resource. This describes the caller's
        // requested target only; no SR/NR write or GPU completion is implied.
        ID3D12Resource* resource=nullptr;
        std::uint32_t x=0,y=0,width=0,height=0,subresource=0;
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    };
    NativeNgxCallCapture(const NativeNgxCallCapture&)=delete;
    NativeNgxCallCapture& operator=(const NativeNgxCallCapture&)=delete;
    static constexpr const auto& ResourceNames()noexcept{return resourceKeys;}
    const std::shared_ptr<const StreamlineSourceScope::Observation>& SourceFrame()const noexcept
    {return sourceFrame_;}
    // Read-only adapter for ObserveNgxEvaluation: zero means a captured Get
    // succeeded, one means absent. These are local statuses, not host API results.
    int Get(const char* key,unsigned* value)const noexcept
    {
        if(!key||!value)return 1;
        for(std::size_t i=0;i<integerKeys.size();++i)if(std::string_view(key)==integerKeys[i])
        {if(!integers_[i])return 1;*value=*integers_[i];return 0;}return 1;
    }
    template<class Parameters,class Result>bool MatchesEvaluationScalars(Parameters* parameters,Result success)const noexcept
    try
    {
        if(!parameters||Neurotic::Feed::DeclaresPreparedNgxSource(parameters,success)!=preparedSource_)return false;
        for(std::size_t i=0;i<integerKeys.size();++i)
        {
            unsigned value=0;const bool present=parameters->Get(integerKeys[i],&value)==success;
            if(present!=integers_[i].has_value()||(present&&value!=*integers_[i]))return false;
        }
        for(std::size_t i=0;i<floatKeys.size();++i)
        {
            float value=0;const bool present=parameters->Get(floatKeys[i],&value)==success;
            if(present!=floats_[i].has_value()||(present&&(!std::isfinite(value)||value!=*floats_[i])))return false;
        }
        return true;
    }
    catch(...){return false;}
    // Existing consumers receive finite controls only. AFNR separately needs
    // original nonfinite evidence to refuse it rather than assume it absent.
    int Get(const char* key,float* value)const noexcept
    {
        float original=0;
        if(!value||GetOriginalFloat(key,&original)||!std::isfinite(original))return 1;
        *value=original;return 0;
    }
    int GetOriginalFloat(const char* key,float* value)const noexcept
    {
        if(!key||!value)return 1;
        for(std::size_t i=0;i<floatKeys.size();++i)if(std::string_view(key)==floatKeys[i])
        {if(!floats_[i])return 1;*value=*floats_[i];return 0;}return 1;
    }
    int Get(const char* key,void** value)const noexcept
    {
        if(!key||!value)return 1;
        for(std::size_t i=0;i<resourceKeys.size();++i)if(std::string_view(key)==resourceKeys[i])
        {if(!resources_[i].raw)return 1;*value=*resources_[i].raw;return 0;}return 1;
    }
    ID3D12Resource* Resource(std::string_view key)const noexcept
    {for(std::size_t i=0;i<resourceKeys.size();++i)if(key==resourceKeys[i])return resources_[i].native.Get();return nullptr;}
    const D3D12_RESOURCE_DESC* Description(std::string_view key)const noexcept
    {for(std::size_t i=0;i<resourceKeys.size();++i)if(key==resourceKeys[i]&&resources_[i].native)return &resources_[i].description;return nullptr;}
    const D3D12_FEATURE_DATA_FORMAT_SUPPORT* FormatSupport(std::string_view key)const noexcept
    {for(std::size_t i=0;i<resourceKeys.size();++i)if(key==resourceKeys[i]&&resources_[i].formatSupport)return &*resources_[i].formatSupport;return nullptr;}
    std::optional<CallerOutputRegionValue> CallerOutputRegion(std::uint32_t width,std::uint32_t height)const noexcept
    {
        const auto* description=Description("Output");auto* output=Resource("Output");
        unsigned x=0,y=0;
        if(!output||!description||!device_||!width||!height||
           Get("DLSS.Output.Subrect.Base.X",&x)!=0||Get("DLSS.Output.Subrect.Base.Y",&y)!=0||
           description->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||description->DepthOrArraySize!=1||
           description->MipLevels!=1||description->SampleDesc.Count!=1||
           description->Format==DXGI_FORMAT_UNKNOWN||x>description->Width||y>description->Height||
           width>description->Width-x||height>description->Height-y)return {};
        D3D12_FEATURE_DATA_FORMAT_INFO info{description->Format};
        if(FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,&info,sizeof(info)))||info.PlaneCount!=1)
            return {};
        return CallerOutputRegionValue{output,x,y,width,height,0,description->Format};
    }
    ID3D12GraphicsCommandList* CommandList()const noexcept{return list_.Get();}
    ID3D12Device* Device()const noexcept{return device_.Get();}
};
}
