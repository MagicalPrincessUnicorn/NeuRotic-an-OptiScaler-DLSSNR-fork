#pragma once
#include <nr/contracts/C02_Context.h>
#include "NativeResourceRegistry.h"
#include <dlssnr/ControlledSceneShader.h>
#include <dlssnr/NativeNgxCallCapture.h>
#include <nr/d3d12/NativeRecordingState.h>
#include <d3dcompiler.h>
#include <functional>

namespace Neurotic::Lifecycle
{
class NativeControlledSceneProducer;
// Owner-issued statement about a recorded producer, never GPU completion or
// original game content. No constructor or caller-supplied semantic label port.
class NativeProducerColorBinding
{
    friend class NativeControlledSceneProducer;
    C::NativeSampleIdentityV1 sample_;
    C::ResourceView input_;
    std::optional<C::ResourceView> output_;
    ID3D12Resource* color_=nullptr;
    ID3D12Resource* destination_=nullptr;
    const void* parameters_=nullptr;
    const void* feature_=nullptr;
    std::uint64_t generation_=0,recording_=0;
    unsigned width_=0,height_=0;
    NativeProducerColorBinding()=default;
    NativeProducerColorBinding(const NativeProducerColorBinding&)=default;
  public:
    std::optional<C::ColorDescription> ColorFor(const C::NativeSampleIdentityV1& sample,
        const DlssNr::NativeNgxCallCapture& call,const C::ResourceView& view,bool output,
        const C::EvidenceRef& evidence)const
    {
        const auto* expected=output?(output_?&*output_:nullptr):&input_;
        if(sample!=sample_||!expected||!ValidEvidence(evidence)||view.identity!=expected->identity||
           !Context::SemanticEqual(view.descriptor,expected->descriptor)||
           !Context::SameFact(view.mip,expected->mip)||!Context::SameFact(view.arrayLayer,expected->arrayLayer)||
           !Context::SameFact(view.plane,expected->plane)||
           call.Resource(output?"Output":"Color")!=(output?destination_:color_))return {};
        C::ColorDescription color;
        color.domain=C::OptionalFact<C::ColorDomain>::FromKnown(C::ColorDomain::SceneLinear,evidence);
        C::Symbol linear;linear.Assign("Linear");
        color.transfer=C::OptionalFact<C::Symbol>::FromKnown(linear,evidence);
        // The analytic RGB palette defines relative unexposed linear values.
        // It establishes no physical nits, display primaries or game identity.
        return color;
    }
    std::shared_ptr<const NativeProducerColorBinding> SelectedOutput(
        const OpaqueSrEvaluationReceipt& receipt,const C::ResourceView& output)const
    {
        const auto revision=receipt.PublishedRevision();const auto& e=receipt.Original();
        if(output_||!revision||!receipt.ObservedReturn()||
           e.featureHandle!=feature_||e.featureGeneration!=generation_||e.parameters!=parameters_||
           e.recordingIncarnation!=recording_||e.inputs[0].native!=color_||e.inputs[0].identity!=input_.identity||
           !e.output.native||e.outputSubresource||e.outputRegion!=C::Rectangle{0,0,width_,height_})return {};
        auto expected=e.output.identity;expected.contentRevision=*revision;
        if(output.identity!=expected||!Context::CompleteContent(output))return {};
        auto result=std::shared_ptr<NativeProducerColorBinding>(new NativeProducerColorBinding(*this));
        result->output_=output;result->destination_=e.output.native;return result;
    }
};

// Bounded controlled-host producer. Calling it really records the reviewed scene;
// it cannot certify preexisting pixels through a string, parameter or token.
class NativeControlledSceneProducer
{
  public:
    static bool SupportedExtent(std::uint64_t width,unsigned height)noexcept
    {return (width==127&&height==95)||(width==159&&height==111);}
    using Images=std::array<ID3D12Resource*,4>;
    using Observe=std::function<D3D12::NativeRecordingObservation(ID3D12GraphicsCommandList*)>;
    using RegisterLayout=std::function<bool(ID3D12RootSignature*,std::vector<D3D12::RootParameter>)>;
    class Declaration
    {
        friend class NativeControlledSceneProducer;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,4> images_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
        std::array<C::ResourceView,4> views_;
        DlssNr::GpuSafety::Ticket ticket_;
        D3D12::NativeRecordingObservation end_;
        std::uint64_t generation_=0;
        unsigned frame_=0,width_=0,height_=0;bool inverted_=false,recorded_=false;
        std::atomic<bool> attempted_{false},recordFinished_{false},consumed_{false};
        std::atomic<unsigned> stage_{0};
        Declaration()=default;
      public:
        Declaration(const Declaration&)=delete;
        // Root must retain this closure BEFORE entry and through terminal GPU
        // retirement, including failure. Scope below never owns retirement.
        bool Record(NativeResourceRegistry& resources,const Observe& observe)noexcept
        try
        {
            if(attempted_.exchange(true,std::memory_order_acq_rel))return false;
            struct Finish{std::atomic<bool>& done;~Finish(){done.store(true,std::memory_order_release);}}finish{recordFinished_};
            stage_=1;
            const auto before=observe(list_.Get());
            if(!before.active||!before.completeCoverage||!before.trackingBeganBeforeRecording||
               before.missingRequiredHistory||before.nativeList!=list_.Get()||!before.incarnation||!before.hookGeneration)return false;
            ticket_=DlssNr::GpuSafety::Record(list_.Get());
            stage_=2;
            Images images{};
            for(unsigned i=0;i<4;++i)
            {
                images[i]=images_[i].Get();
                const auto current=resources.CurrentRegisteredView(images_[i].Get());
                if(!current||current->identity!=views_[i].identity)return false;
            }
            auto action=DlssNr::GpuSafety::BeginLocalAction(ticket_,list_.Get(),std::span<ID3D12Resource* const>(images));
            if(!action)return false;
            std::array<D3D12_RESOURCE_BARRIER,4> barriers{};
            stage_=3;
            for(unsigned i=0;i<4;++i)
            {
                barriers[i].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barriers[i].Transition={images_[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
            }
            if(frame_)list_->ResourceBarrier(4,barriers.data());
            list_->SetPipelineState(pipeline_.Get());list_->SetComputeRootSignature(root_.Get());
            ID3D12DescriptorHeap* heaps[]={heap_.Get()};list_->SetDescriptorHeaps(1,heaps);
            const unsigned constants[]={width_,height_,frame_,inverted_?1u:0u};
            list_->SetComputeRoot32BitConstants(0,4,constants,0);
            list_->SetComputeRootDescriptorTable(1,heap_->GetGPUDescriptorHandleForHeapStart());
            list_->Dispatch((width_+7)/8,(height_+7)/8,1);
            for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
            list_->ResourceBarrier(4,barriers.data());
            end_=observe(list_.Get());
            stage_=4;
            if(!end_.active||!end_.completeCoverage||end_.missingRequiredHistory||end_.nativeList!=before.nativeList||
               end_.incarnation!=before.incarnation||end_.hookGeneration!=before.hookGeneration||
               end_.workOrdinal<=before.workOrdinal)return false;
            for(unsigned i=0;i<4;++i)
            {
                stage_=5+i;
                if(!action->Current()||!resources.RecordWritten(images_[i].Get(),list_.Get(),*action))return false;
                const auto current=resources.CurrentRegisteredView(images_[i].Get());
                if(!current||!Context::CompleteContent(*current))return false;
                views_[i]=*current;
            }
            recorded_=true;stage_=9;return true;
        }
        catch(...){return false;}
        unsigned RecordStage()const noexcept{return stage_.load(std::memory_order_relaxed);}
        bool Terminal()const noexcept
        {return recordFinished_.load(std::memory_order_acquire)&&ticket_&&DlssNr::GpuSafety::InspectTerminalRecording(ticket_).has_value();}
    };
    // CPU creation only. Same fixed existing shader, exact owned UAV mapping.
    // No caller shader, root signature, domain or evidence is accepted.
    static std::shared_ptr<Declaration> Prepare(NativeResourceRegistry& resources,
        ID3D12GraphicsCommandList* list,const Images& images,std::uint64_t featureGeneration,
        unsigned frame,bool inverted,const RegisterLayout& registerLayout)noexcept
    try
    {
        if(!list||!featureGeneration||frame>3||!registerLayout)return {};
        auto result=std::shared_ptr<Declaration>(new Declaration);
        result->list_=DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list).object;
        if(result->list_.Get()!=list||FAILED(list->GetDevice(IID_PPV_ARGS(&result->device_))))return {};
        constexpr std::array formats={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32_UINT};
        for(unsigned i=0;i<4;++i)
        {
            result->images_[i]=DlssNr::NativeIdentity::Resolve<ID3D12Resource>(images[i]).object;
            const auto* view=resources.Observe(images[i]);
            if(!view||!result->images_[i]||result->images_[i].Get()!=images[i])return {};
            for(unsigned j=0;j<i;++j)if(images[j]==images[i])return {};
            Microsoft::WRL::ComPtr<ID3D12Device> device;
            if(FAILED(images[i]->GetDevice(IID_PPV_ARGS(&device)))||
               !DlssNr::NativeIdentity::CompareDevices(device.Get(),result->device_.Get()).equal)return {};
            const auto d=images[i]->GetDesc();
            if(!SupportedExtent(d.Width,d.Height)||(i&&(d.Width!=result->width_||d.Height!=result->height_))||
               d.Format!=formats[i]||d.MipLevels!=1||d.DepthOrArraySize!=1||
               d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.SampleDesc.Count!=1||
               !(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return {};
            result->width_=static_cast<unsigned>(d.Width);result->height_=d.Height;result->views_[i]=*view;
        }
        D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;range.NumDescriptors=4;
        range.OffsetInDescriptorsFromTableStart=D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues=4;parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable={1,&range};
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;
        Microsoft::WRL::ComPtr<ID3DBlob> serialized,error,shader;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error))||
           FAILED(result->device_->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&result->root_))))return {};
        if(!registerLayout(result->root_.Get(),{{D3D12::RootKind::Constants,4},{D3D12::RootKind::Table,0}}))return {};
        if(FAILED(D3DCompile(DlssNr::ControlledScene::Shader,sizeof(DlssNr::ControlledScene::Shader)-1,
            "ControlledScene/scene.hlsl",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&error)))return {};
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=result->root_.Get();
        pipeline.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
        if(FAILED(result->device_->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&result->pipeline_))))return {};
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors=4;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(result->device_->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&result->heap_))))return {};
        auto handle=result->heap_->GetCPUDescriptorHandleForHeapStart();
        const auto stride=result->device_->GetDescriptorHandleIncrementSize(heap.Type);
        for(unsigned i=0;i<4;++i)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=formats[i];view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            result->device_->CreateUnorderedAccessView(images[i],nullptr,&view,handle);handle.ptr+=stride;
        }
        result->generation_=featureGeneration;result->frame_=frame;result->inverted_=inverted;return result;
    }
    catch(...){return {};}
    class Scope;
  private:
    inline static thread_local Scope* current_=nullptr;
  public:
    class Scope
    {
        friend class NativeControlledSceneProducer;
        Scope* prior_;std::shared_ptr<Declaration> declaration_;
        const void* feature_;const void* parameters_;
      public:
        Scope(std::shared_ptr<Declaration> declaration,const void* feature,const void* parameters):
            prior_(current_),declaration_(std::move(declaration)),feature_(feature),parameters_(parameters)
        {current_=this;}
        ~Scope(){current_=prior_;}
        Scope(const Scope&)=delete;
    };
    static std::shared_ptr<const NativeProducerColorBinding> Consume(const DlssNr::NativeNgxCallCapture& call,
        const C::NativeSampleIdentityV1& sample,std::uint64_t generation,const void* parameters,
        const D3D12::NativeRecordingObservation& recording,
        const std::array<const C::ResourceView*,6>& views)noexcept
    try
    {
        auto* scope=current_;if(!scope||scope->prior_||!scope->declaration_)return {};
        auto& d=*scope->declaration_;
        if(d.consumed_.exchange(true,std::memory_order_acq_rel))return {};
        if(!d.recordFinished_.load(std::memory_order_acquire)||!d.recorded_||!scope->feature_||parameters!=scope->parameters_||sample.Check()!=C::Error::None||
           generation!=d.generation_||call.CommandList()!=d.list_.Get()||
           !recording.active||!recording.completeCoverage||recording.missingRequiredHistory||
           recording.nativeList!=d.end_.nativeList||recording.incarnation!=d.end_.incarnation||
           recording.hookGeneration!=d.end_.hookGeneration||recording.workOrdinal!=d.end_.workOrdinal)return {};
        unsigned flags=0,width=0,height=0;
        if(call.Get("DLSS.Feature.Create.Flags",&flags)||flags!=(3u|(d.inverted_?8u:0u))||
           call.Get("DLSS.Render.Subrect.Dimensions.Width",&width)||width!=d.width_||
           call.Get("DLSS.Render.Subrect.Dimensions.Height",&height)||height!=d.height_)return {};
        for(const auto* key:{"DLSS.Pre.Exposure","DLSS.Exposure.Scale"})
        {float value=0;if(call.Get(key,&value)||value!=1.f)return {};}
        if(call.Resource("ExposureTexture")||call.Resource("DLSS.Input.Bias.Current.Color.Mask"))return {};
        constexpr std::array names={"Color","Depth","MotionVectors"};
        constexpr std::array<unsigned,3> indices={0,2,3};
        for(unsigned i=0;i<3;++i)
            if(call.Resource(names[i])!=d.images_[i].Get()||!views[indices[i]]||
               views[indices[i]]->identity!=d.views_[i].identity)return {};
        auto result=std::shared_ptr<NativeProducerColorBinding>(new NativeProducerColorBinding);
        result->sample_=sample;result->input_=d.views_[0];result->color_=d.images_[0].Get();
        result->parameters_=parameters;result->feature_=scope->feature_;result->generation_=generation;
        result->recording_=recording.incarnation;result->width_=d.width_;result->height_=d.height_;return result;
    }
    catch(...){return {};}
};
}
