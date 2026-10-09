#include <pch.h>
#include "IFeature_Dx11wDx12.h"

#include <dlssnr/DlssNr.h>
#include <dlssnr/DlssNr_BridgeTelemetry.h>

#include <Config.h>

#include <proxies/DXGI_Proxy.h>
#include <proxies/D3D12_Proxy.h>
#include <misc/IdentifyGpu.h>

#include <with_dx12/with_dx12.h>

void IFeature_Dx11wDx12::ResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                                         D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    if (beforeState == afterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

bool IFeature_Dx11wDx12::CreateD3D12Objects()
{
    HRESULT result;
    if (!bridgeGeneration)
    {
        if (bridgeGenerationId==UINT64_MAX) return false;
        bridgeGeneration=std::make_unique<BridgeGeneration>();
        ++bridgeGenerationId;
        bridgeGeneration->device11=Dx11Device;
        bridgeGeneration->context=Dx11DeviceContext;
        bridgeGeneration->device12=_dx11on12Device;
        bridgeGeneration->queue=Dx12CommandQueue;
        if (FAILED(Dx11Device->CreateFence(0,D3D11_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&bridgeGeneration->consumerFence)))) return false;
    }

    for (size_t i = 0; i < DX11WDX12_NUM_OF_BUFFERS; i++)
    {
        if (Dx12CommandAllocator[i] == nullptr)
        {
            result =
                _dx11on12Device->CreateCommandAllocator(Dx12CommandListType, IID_PPV_ARGS(&Dx12CommandAllocator[i]));

            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocator error: {:X}", result);
                return false;
            }
        }

        if (Dx12CommandList[i] == nullptr && Dx12CommandAllocator[i] != nullptr)
        {
            // CreateCommandList
            result = _dx11on12Device->CreateCommandList(0, Dx12CommandListType, Dx12CommandAllocator[i], nullptr,
                                                        IID_PPV_ARGS(&Dx12CommandList[i]));

            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList error: {:X}", result);
                return false;
            }

            if (!DlssNr::GpuSafety::PrepareRecordingHooks(Dx12CommandList[i]) ||
                FAILED(Dx12CommandList[i]->Close())) return false;
        }
    }

    if (Dx12Fence == nullptr)
    {
        result = _dx11on12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Dx12Fence));

        if (result != S_OK)
        {
            LOG_ERROR("CreateFence error: {0:X}", result);
            return false;
        }

        Dx12FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (Dx12FenceEvent == nullptr)
        {
            LOG_ERROR("CreateEvent error!");
            return false;
        }
    }

    return true;
}

bool IFeature_Dx11wDx12::GenerationReusable() const
{
    if (!bridgeGeneration) return true;
    if (!bridgeGeneration->consumerFence || FAILED(bridgeGeneration->device11->GetDeviceRemovedReason()) ||
        FAILED(bridgeGeneration->device12->GetDeviceRemovedReason())) return false;
    const auto completed=bridgeGeneration->consumerFence->GetCompletedValue();
    for (const auto& slot : bridgeGeneration->slots)
        if (!slot.use->Reusable(completed)) return false;
    return true;
}

bool IFeature_Dx11wDx12::WaitForGenerationReuse()
{
    if (GenerationReusable()) return true;
    if (!bridgeGeneration || bridgeGeneration->revoked ||
        FAILED(bridgeGeneration->device11->GetDeviceRemovedReason()) ||
        FAILED(bridgeGeneration->device12->GetDeviceRemovedReason()))
    { bridgePreparationReason="generation-unavailable-or-removed";return false; }
    const auto started=GetTickCount64();
    constexpr DWORD deadline=100;
    for (UINT index=0;index<DX11WDX12_NUM_OF_BUFFERS;++index)
    {
        const auto elapsed=GetTickCount64()-started;
        const DWORD remaining=elapsed>=deadline ? 0 : deadline-static_cast<DWORD>(elapsed);
        if (!bridgeGeneration->slots[index].use->WaitForReuse(bridgeGeneration->consumerFence.Get(),
            Dx12Fence,Dx12CommandAllocatorFenceValue[index],Dx12CommandQueue,remaining,&bridgePreparationReason))
            return false;
    }
    // Native events assist completion; they cannot replace device/owner proof.
    if (!GenerationReusable()) { bridgePreparationReason="generation-completion-proof-pending";return false; }
    bridgeWaitMilliseconds+=GetTickCount64()-started;
    if (++bridgeWaitCount==1 || bridgeWaitCount%300==0)
        LOG_INFO("DX11/DX12 bridge completion assistance: waits={}, totalMs={}",bridgeWaitCount,bridgeWaitMilliseconds);
    return true;
}

bool IFeature_Dx11wDx12::MarkConsumer(UINT slot)
{
    auto& generation=*bridgeGeneration;
    auto& use=*generation.slots[slot].use;
    if (generation.consumerValue>=UINT64_MAX-1 ||
        FAILED(generation.context->Signal(generation.consumerFence.Get(),++generation.consumerValue)))
    {
        use.unknown=true; generation.revoked=true; return false;
    }
    use.consumer=generation.consumerValue;
    generation.context->Flush();
    return true;
}

bool IFeature_Dx11wDx12::CancelRecording(UINT slot)
{
    // Only a successfully closed, never-submitted list may cancel via Reset.
    auto& use=*bridgeGeneration->slots[slot].use;
    if (FAILED(Dx12CommandList[slot]->Reset(Dx12CommandAllocator[slot],nullptr)) ||
        !DlssNr::GpuSafety::Reusable(use.recording) || FAILED(Dx12CommandList[slot]->Close()))
    { use.unknown=true; bridgeGeneration->revoked=true; return false; }
    return true;
}

DlssNr::Bridge::Identity IFeature_Dx11wDx12::CurrentIdentity(UINT slot, UINT64 lifecycle) const
{
    const auto& cache=Dx11WithDx12::GetUpscalerResourceCache();
    return {bridgeAttemptId,bridgeGenerationId,Dx11WithDx12::GetLastPreparedUpscalerFrameId(),slot,
        reinterpret_cast<uintptr_t>(Dx11WithDx12::GetD3D11DeviceContext()),
        reinterpret_cast<uintptr_t>(Dx11WithDx12::GetD3D12Device()),
        reinterpret_cast<uintptr_t>(Dx11WithDx12::GetD3D12CommandQueue()),
        reinterpret_cast<uintptr_t>(cache.ParamOutput[slot]),reinterpret_cast<uintptr_t>(cache.Output[slot].Dx12Resource),
        bridgeAttemptId,lifecycle};
}

void IFeature_Dx11wDx12::ReleaseSharedResources()
{
    if (!GenerationReusable()) return;
    for (size_t i = 0; i < DX11WDX12_NUM_OF_BUFFERS; i++)
    {
        SAFE_RELEASE(Dx12CommandList[i]);
        SAFE_RELEASE(Dx12CommandAllocator[i]);
        Dx12CommandAllocatorFenceValue[i] = 0;
    }

    SAFE_RELEASE(Dx12Fence);
    Dx12FenceValue = 0;

    if (Dx12FenceEvent)
    {
        CloseHandle(Dx12FenceEvent);
        Dx12FenceEvent = nullptr;
    }

    bridgeGeneration.reset();
}

bool IFeature_Dx11wDx12::ProcessDx11Textures(const NVSDK_NGX_Parameter* InParameters)
{
    HRESULT result;

    auto frame = _frameCount % DX11WDX12_NUM_OF_BUFFERS;
    // Inputs are single-buffered in the existing cache. Poll all users BEFORE
    // overwriting them, as well as the selected allocator; never wait by frame age.
    bridgePreparationReason="resource-or-command-preparation";
    if (!bridgeGeneration || bridgeGeneration->revoked)
    { bridgePreparationReason="generation-unavailable-or-revoked";return false; }
    if (!WaitForGenerationReuse()) return false;
    if (!Dx11WithDx12::UpscalerReadersReusable())
    { bridgePreparationReason="cache-writer-or-fg-reader-pending";return false; }
    auto& slot=bridgeGeneration->slots[frame];
    slot.before={}; slot.after={};
    for (auto& input : slot.inputs) input.Reset();
    *slot.use={};
    const auto cacheFrameKey = Dx11WithDx12::NextUpscalerFrameId();
    if (!cacheFrameKey) return false;
    slot.before=Dx11WithDx12::RetainUpscalerResources();
    const char* names[]={NVSDK_NGX_Parameter_Color,NVSDK_NGX_Parameter_MotionVectors,
        NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_Output,NVSDK_NGX_Parameter_ExposureTexture,
        NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask};
    for (size_t i=0;i<slot.inputs.size();++i)
    {
        ID3D11Resource* input=nullptr;
        if (InParameters->Get(names[i],&input)!=NVSDK_NGX_Result_Success)
            InParameters->Get(names[i],reinterpret_cast<void**>(&input));
        slot.inputs[i]=input;
    }
    Dx11WithDx12::SetUpscalerFrameIndex((UINT) frame);

    const auto allocatorFenceValue = Dx12CommandAllocatorFenceValue[frame];
    result = Dx12CommandAllocator[frame]->Reset();
    if (result != S_OK)
    {
        LOG_ERROR("CommandAllocator Reset error for frame {}, allocator fence {}, completed {}: {:X}", frame,
                  allocatorFenceValue, Dx12Fence->GetCompletedValue(), (UINT) result);
        return false;
    }

    result = Dx12CommandList[frame]->Reset(Dx12CommandAllocator[frame], nullptr);
    if (result != S_OK)
    {
        LOG_ERROR("CommandList Reset error: {:X}", (UINT) result);
        return false;
    }

    bridgeRecordingOpen=true;
    slot.use->recording=DlssNr::GpuSafety::Record(Dx12CommandList[frame]);
    if (!slot.use->recording)
    {
        if (FAILED(Dx12CommandList[frame]->Close())) { slot.use->unknown=true; bridgeGeneration->revoked=true; }
        bridgeRecordingOpen=false;
        return false;
    }

    auto mask = Dx11WithDx12::ResourceMask::Color | Dx11WithDx12::ResourceMask::Mv | Dx11WithDx12::ResourceMask::Depth |
                Dx11WithDx12::ResourceMask::Output;

    if (!AutoExposure())
        mask |= Dx11WithDx12::ResourceMask::Exposure;
    else
        LOG_DEBUG("AutoExposure enabled!");

    const bool reactiveDisabled = Config::Instance()->DisableReactiveMask.value_or(false);
    const bool reactiveRequired = Config::Instance()->Dx11Upscaler.value_or_default() == Upscaler::XeSS ||
                                  Config::Instance()->Dx11Upscaler.value_or_default() == Upscaler::XeSS_on12;

    if (!reactiveDisabled)
        mask |= Dx11WithDx12::ResourceMask::Reactive;
    else
        LOG_DEBUG("ReactiveMask disabled!");

    const auto prepareResult = Dx11WithDx12::PrepareUpscalerResources(
        InParameters, mask, (UINT) frame, cacheFrameKey, Config::Instance()->DontUseNTShared.value_or_default(),
        reactiveRequired, true, &slot.after);
    const auto synchronization=slot.after.synchronization;
    slot.after=Dx11WithDx12::RetainUpscalerResources();
    slot.after.synchronization=synchronization;
    const bool consumerTracked=MarkConsumer((UINT)frame);
    if (!Dx11WithDx12::BindUpscalerUse(slot.use,bridgeGeneration->consumerFence.Get()))
    { slot.use->unknown=true; bridgeGeneration->revoked=true; return false; }
    if (!consumerTracked) return false;

    if (!prepareResult.Success)
    {
        if (prepareResult.MissingExposure)
        {
            LOG_WARN("AutoExposure disabled but ExposureTexture does not exist, enabling auto exposure and changing "
                     "backend");
            State::Instance().autoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
            return true;
        }

        if (prepareResult.MissingReactive && reactiveRequired)
        {
            LOG_WARN("Bias mask does not exist and is required by the current DX11 upscaler, disabling reactive mask");
            Config::Instance()->DisableReactiveMask.set_volatile_value(true);
            State::Instance().changeBackend[Handle()->Id] = true;
            return true;
        }

        LOG_ERROR("Dx11wDx12 resource cache preparation failed");
        return false;
    }

    LOG_DEBUG("Shared handles prepared and synchronized by Dx11WithDx12 cache, frameKey: {}", cacheFrameKey);
    return true;
}

bool IFeature_Dx11wDx12::CopyBackOutput(DlssNr::Bridge::Outcome& outcome)
{
    const auto frame = (UINT) (_frameCount % DX11WDX12_NUM_OF_BUFFERS);
    auto identity=CurrentIdentity(frame,DlssNr::BridgeLifecycleGeneration());
    auto& slot=bridgeGeneration->slots[frame];
    const bool queued=Dx11WithDx12::CopyUpscalerOutputToDx11(frame,&outcome,identity,&slot.after);
    const bool tracked=MarkConsumer(frame);
    if (!queued || !tracked) { outcome.Fail(); return false; }
    outcome.Delivered();
    return true;
}

bool IFeature_Dx11wDx12::Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();
    auto lifecycleLock=DlssNr::LockBridgeLifecycle();
    auto resourceLock=Dx11WithDx12::LockUpscalerResources();

    if (IsInited())
        return true;

    if (State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
        State::Instance().gameEngine == GameEngineType::Unreal ||
        State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine)
    {
        LOG_INFO("Dx11 detected, disabling UE resource barrier overrides");
        Config::Instance()->ColorResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Config::Instance()->MVResourceBarrier.set_volatile_value(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    Device = InDevice;
    DeviceContext = InContext;

    if (!BaseInit(Device, InContext, InParameters))
    {
        LOG_DEBUG("BaseInit failed!");
        return false;
    }

    SetInitParameters(InParameters);

    // The list has to be recording, and what is recorded on it has to run.
    //
    // Every command list here is closed the moment it is created, and the note this replaces said
    // non-DLSS upscalers do not use the list during Init -- which was true, and stopped being true the
    // day DLSS got a bridge variant. DLSS and Ray Reconstruction are the only features whose
    // InitInternal touches the argument at all: NVSDK_NGX_D3D12_CreateFeature records the model's
    // weight upload and history initialisation onto it, and documents that the caller must submit it
    // afterwards.
    //
    // Recorded onto a closed list, all of that is discarded. ID3D12GraphicsCommandList methods return
    // void, so nothing fails, nothing logs, and CreateFeature still answers Success -- the model then
    // runs against state that was never uploaded. That is the posterised, flat-blocked picture, and it
    // gets worse with model size: the old CNN degraded to soft, the transformer collapses to blocks.
    //
    // So: open the list, let Init record into it, submit it, and wait. The wait is not optional --
    // the first Evaluate resets this same allocator, and doing that under work still in flight is a
    // device removal rather than a bad picture.
    if (!GenerationReusable() || bridgeGeneration->revoked ||
        FAILED(Dx12CommandAllocator[0]->Reset()) ||
        FAILED(Dx12CommandList[0]->Reset(Dx12CommandAllocator[0],nullptr))) return false;
    auto& use=*bridgeGeneration->slots[0].use;
    use.recording=DlssNr::GpuSafety::Record(Dx12CommandList[0]);
    if (!use.recording) { Dx12CommandList[0]->Close(); return false; }
    const bool initialised=dx12Feature->Init(_dx11on12Device,Dx12CommandList[0],InParameters);
    if (FAILED(Dx12CommandList[0]->Close()))
    { use.unknown=true; bridgeGeneration->revoked=true; return false; }
    if (!initialised) { CancelRecording(0); return false; }
    ID3D12CommandList* lists[]={Dx12CommandList[0]};
    Dx12CommandQueue->ExecuteCommandLists(1,lists);
    const UINT64 signalled=++Dx12FenceValue;
    const bool observed=DlssNr::GpuSafety::OrderedOn(use.recording,Dx12CommandQueue);
    const bool sealed=DlssNr::GpuSafety::SealOwnedRecording(Dx12CommandList[0]);
    if (FAILED(Dx12CommandQueue->Signal(Dx12Fence,signalled)) || !observed || !sealed)
    { use.unknown=true; bridgeGeneration->revoked=true; return false; }
    Dx12CommandAllocatorFenceValue[0]=signalled;
    // Creation uploads must finish before a first frame can use/reset them.
    // A bounded wait is allowed only for this exact known submission.
    if (!DlssNr::Bridge::FenceReached(Dx12Fence->GetCompletedValue(),signalled))
    {
        if (Dx12Fence->GetCompletedValue()==UINT64_MAX ||
            FAILED(Dx12Fence->SetEventOnCompletion(signalled,Dx12FenceEvent)) ||
            WaitForSingleObject(Dx12FenceEvent,5000)!=WAIT_OBJECT_0) return false;
    }
    if (!GenerationReusable() || !DlssNr::Bridge::FenceReached(Dx12Fence->GetCompletedValue(),signalled)) return false;
    bridgeInitialised=true;
    SetInit(true);
    LOG_INFO("Init: exact feature creation recording completed and sealed");

    return IsInited();
}

bool IFeature_Dx11wDx12::Evaluate(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    auto lifecycleLock=DlssNr::LockBridgeLifecycle();
    auto resourceLock=Dx11WithDx12::LockUpscalerResources();
    if (!IsInited() || !bridgeGeneration || bridgeAttemptId==UINT64_MAX)
        return false;
    ++bridgeAttemptId;
    bridgeRecordingOpen=false;
    DlssNr::Bridge::Identity pendingIdentity;
    pendingIdentity.attempt=bridgeAttemptId;
    pendingIdentity.generation=bridgeGenerationId;
    pendingIdentity.configuration=bridgeAttemptId;
    bridgeReceipt=DlssNr::Bridge::Outcome(pendingIdentity).Snapshot();
    bridgeReceipt.failed=true;
    struct CurrentOutcomeObservation
    {
        DlssNr::Bridge::Receipt& bridgeReceipt;
        ~CurrentOutcomeObservation() { DlssNr::BridgeTelemetry().ObserveOutcome(bridgeReceipt); }
    } observation{bridgeReceipt};

    ID3D11DeviceContext4* dc;
    auto result = InDeviceContext->QueryInterface(IID_PPV_ARGS(&dc));

    if (result != S_OK)
    {
        LOG_ERROR("QueryInterface error: {0:x}", result);
        return false;
    }

    // Revoke admission before looking at retirement. A new native device or
    // queue requires a host feature recreation, never reuse of old provider state.
    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    dc->GetDevice(&contextDevice);
    Microsoft::WRL::ComPtr<ID3D11Device5> contextDevice5;
    contextDevice.As(&contextDevice5);
    const bool ownersChanged=contextDevice5.Get()!=Dx11Device ||
        WithDx12::GetD3D12Device()!=_dx11on12Device || WithDx12::GetD3D12CommandQueue()!=Dx12CommandQueue;
    if (ownersChanged)
    {
        bridgeGeneration->revoked=true;
        dc->Release();
        return false;
    }
    if (dc != Dx11DeviceContext)
    {
        bridgeGeneration->revoked=true;
        if (!GenerationReusable()) { dc->Release(); return false; }
        ReleaseSharedResources();
        Dx11DeviceContext=dc;
        if (!CreateD3D12Objects()) { dc->Release(); return false; }
        Dx11WithDx12::Init(Dx11Device,Dx11DeviceContext,_dx11on12Device,Dx12CommandQueue);
    }
    dc->Release();
    if (bridgeGeneration->revoked) return false;

    auto frame = _frameCount % DX11WDX12_NUM_OF_BUFFERS;
    auto cmdList = Dx12CommandList[frame];

    auto& cache = Dx11WithDx12::GetUpscalerResourceCache();
    auto& dx11Color = cache.Color;
    auto& dx11Mv = cache.Mv;
    auto& dx11Depth = cache.Depth;
    auto& dx11Reactive = cache.Reactive;
    auto& dx11Exp = cache.Exposure;
    auto& dx11Out = cache.Output[frame];

    auto getOriginalNgxResource = [](NVSDK_NGX_Parameter* parameters, const char* name, ID3D11Resource** outResource)
    {
        if (parameters == nullptr || name == nullptr || outResource == nullptr)
            return false;

        *outResource = nullptr;

        if (parameters->Get(name, outResource) != NVSDK_NGX_Result_Success)
            parameters->Get(name, (void**) outResource);

        return *outResource != nullptr;
    };

    ID3D11Resource* restoreParamColor = nullptr;
    ID3D11Resource* restoreParamMv = nullptr;
    ID3D11Resource* restoreParamOutput = nullptr;
    ID3D11Resource* restoreParamDepth = nullptr;
    ID3D11Resource* restoreParamExposure = nullptr;
    ID3D11Resource* restoreParamReactive = nullptr;

    const bool hasRestoreParamColor =
        getOriginalNgxResource(InParameters, NVSDK_NGX_Parameter_Color, &restoreParamColor);
    const bool hasRestoreParamMv =
        getOriginalNgxResource(InParameters, NVSDK_NGX_Parameter_MotionVectors, &restoreParamMv);
    const bool hasRestoreParamOutput =
        getOriginalNgxResource(InParameters, NVSDK_NGX_Parameter_Output, &restoreParamOutput);
    const bool hasRestoreParamDepth =
        getOriginalNgxResource(InParameters, NVSDK_NGX_Parameter_Depth, &restoreParamDepth);
    const bool hasRestoreParamExposure =
        getOriginalNgxResource(InParameters, NVSDK_NGX_Parameter_ExposureTexture, &restoreParamExposure);
    const bool hasRestoreParamReactive = getOriginalNgxResource(
        InParameters, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &restoreParamReactive);

    ID3D11ShaderResourceView* restoreSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11SamplerState* restoreSamplerStates[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* restoreCBVs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* restoreUAVs[D3D11_1_UAV_SLOT_COUNT] = {};
    ID3D11RenderTargetView* restoreRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* restoreDSV = nullptr;

    // backup compute shader resources
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        restoreSRVs[i] = nullptr;
        InDeviceContext->CSGetShaderResources(i, 1, &restoreSRVs[i]);

    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
    {
        restoreSamplerStates[i] = nullptr;
        InDeviceContext->CSGetSamplers(i, 1, &restoreSamplerStates[i]);

    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
    {
        restoreCBVs[i] = nullptr;
        InDeviceContext->CSGetConstantBuffers(i, 1, &restoreCBVs[i]);

    }

    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
    {
        restoreUAVs[i] = nullptr;
        InDeviceContext->CSGetUnorderedAccessViews(i, 1, &restoreUAVs[i]);

    }

    InDeviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, restoreRTVs, &restoreDSV);

    // Unbind RenderTargets
    ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    InDeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTVs, nullptr);

    bool dx12EvalResult = false;
    bool commandListRecording = false;
    bool commandListExecuted = false;
    DlssNr::Bridge::Outcome outcome(bridgeReceipt.identity);
    const auto nrSettings = TryNrConfigSnapshot(*Config::Instance());
    const auto nrBefore = DlssNr::Telemetry();
    do
    {
        const bool processed=ProcessDx11Textures(InParameters);
        commandListRecording=bridgeRecordingOpen;
        if (!processed)
        {
            if (++bridgeBusyRefusals==1 || bridgeBusyRefusals%120==0)
                LOG_WARN("DX11/DX12 bridge preparation refused: reason={}, count={}",bridgePreparationReason,bridgeBusyRefusals);
            DlssNr::BridgeTelemetry().Begin((bool) nrSettings, nrSettings && nrSettings->GetDlssNrRuntimeSnapshot().enabled,
                                            nrSettings && nrSettings->DlssNrRoute.value_or_default() == 0,
                                            nrBefore.lifecycleOpen, false, nrBefore.failureReason);
            break;
        }

        if (State::Instance().changeBackend[Handle()->Id])
        {
            break;
        }

        commandListRecording = true;
        outcome=DlssNr::Bridge::Outcome(CurrentIdentity((UINT)frame,DlssNr::BridgeLifecycleGeneration()));
        outcome.Prepared();

        InParameters->Set(NVSDK_NGX_Parameter_Color, (void*) dx11Color.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_MotionVectors, (void*) dx11Mv.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_Output, (void*) dx11Out.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_Depth, (void*) dx11Depth.Dx12Resource);

        if (!AutoExposure() && dx11Exp.Dx12Resource != nullptr)
            InParameters->Set(NVSDK_NGX_Parameter_ExposureTexture, (void*) dx11Exp.Dx12Resource);

        if (!Config::Instance()->DisableReactiveMask.value_or(false) && dx11Reactive.Dx12Resource != nullptr)
            InParameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask,
                              (void*) dx11Reactive.Dx12Resource);

        const bool bridgeInputsAvailable = dx11Color.Dx12Resource != nullptr && dx11Mv.Dx12Resource != nullptr &&
                                           dx11Out.Dx12Resource != nullptr && dx11Depth.Dx12Resource != nullptr;
        const bool nrEnabled = nrSettings && nrSettings->GetDlssNrRuntimeSnapshot().enabled;
        const bool nativeRoute = nrSettings && nrSettings->DlssNrRoute.value_or_default() == 0;
        DlssNr::BridgeTelemetry().Begin((bool) nrSettings, nrEnabled, nativeRoute, nrBefore.lifecycleOpen,
                                        bridgeInputsAvailable, nrBefore.failureReason);

        // Match the proven native-DX12 order. Performance mode composes into a private Pre-SR
        // scratch before the upscaler; RestoreAfterUpscale puts the game's output binding back.
        // Quality mode then composes after the upscaler. One immutable settings snapshot covers
        // both sides, so a menu edit cannot schedule both routes during a single bridge frame.
        if (nrSettings && nrEnabled && nativeRoute && nrBefore.lifecycleOpen && bridgeInputsAvailable)
        {
            const auto work=DlssNr::EvaluateBridgeBeforeUpscale(cmdList, InParameters, Dx12CommandQueue, &*nrSettings);
            outcome.NrResult(work);
            if (work.outputRestoreFailed) { outcome.Fail();break; }
        }

        LOG_DEBUG("Dispatch!!");
        dx12EvalResult = dx12Feature->Evaluate(cmdList, InParameters);
        outcome.ProviderResult(dx12EvalResult);

        if (nrSettings && nrEnabled && nativeRoute)
            DlssNr::RestoreAfterUpscale(InParameters);

        // DLSS 5 Neural Rendering rides the bridge: at this moment the block carries the D3D12 copies
        // of every input, the list is still recording, and the model's edit lands on the D3D12 output
        // before it is copied back to the game's D3D11 texture. This one call is what makes the pass
        // work in DirectX 11 games, whatever upscaler carried it here.
        static bool reportedNrOffer = false;

        if (!reportedNrOffer)
        {
            reportedNrOffer = true;
            LOG_INFO("DLSS-NR: the D3D11 bridge reached the hand-off (upscale ok: {}, enabled: {})",
                     dx12EvalResult, Config::Instance()->GetDlssNrRuntimeSnapshot().enabled);

        }

        if (dx12EvalResult && nrSettings && nrEnabled && nativeRoute && nrBefore.lifecycleOpen &&
            bridgeInputsAvailable)
            outcome.NrResult(DlssNr::EvaluateBridgeAfterUpscale(cmdList, InParameters, Dx12CommandQueue, &*nrSettings));

        const auto nrAfter = DlssNr::Telemetry();
        if (outcome.Snapshot().nrComposed && nrAfter.lifecycleGeneration!=nrBefore.lifecycleGeneration)
        { dx12EvalResult=false; outcome.Fail(); }


    } while (false);

    if (hasRestoreParamColor)
        InParameters->Set(NVSDK_NGX_Parameter_Color, (void*) restoreParamColor);

    if (hasRestoreParamMv)
        InParameters->Set(NVSDK_NGX_Parameter_MotionVectors, (void*) restoreParamMv);

    if (hasRestoreParamOutput)
        InParameters->Set(NVSDK_NGX_Parameter_Output, (void*) restoreParamOutput);

    if (hasRestoreParamDepth)
        InParameters->Set(NVSDK_NGX_Parameter_Depth, (void*) restoreParamDepth);

    if (hasRestoreParamExposure)
        InParameters->Set(NVSDK_NGX_Parameter_ExposureTexture, (void*) restoreParamExposure);

    if (hasRestoreParamReactive)
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void*) restoreParamReactive);

    if (commandListRecording)
    {
        bridgeRecordingOpen=false;
        const auto closeResult = cmdList->Close();
        if (closeResult != S_OK)
        {
            LOG_ERROR("CommandList Close error: {:X}", (UINT) closeResult);
            DlssNr::BridgeTelemetry().CommandListClosed(false);
            dx12EvalResult = false;
            bridgeGeneration->slots[frame].use->unknown=true;
            bridgeGeneration->revoked=true;
        }
        else if (!dx12EvalResult) CancelRecording((UINT)frame);
    }

    if (dx12EvalResult)
    {
        ID3D12CommandList* ppCommandLists[] = { cmdList };
        if (dx11Out.SharedTexture==cache.ParamOutput[frame]) outcome.OriginalMayBeWritten();
        Dx12CommandQueue->ExecuteCommandLists(1, ppCommandLists);
        commandListExecuted = true;

        const auto fenceValue = ++Dx12FenceValue;
        result = Dx12CommandQueue->Signal(Dx12Fence, fenceValue);
        auto& use=*bridgeGeneration->slots[frame].use;
        const bool observed=DlssNr::GpuSafety::OrderedOn(use.recording,Dx12CommandQueue);
        const bool sealed=DlssNr::GpuSafety::SealOwnedRecording(cmdList);
        const bool tracked=result==S_OK && observed && sealed && fenceValue!=UINT64_MAX;
        outcome.Submitted(tracked);
        if (!tracked)
        {
            LOG_ERROR("Dx12CommandQueue Signal failed for feature fence {}: {:X}", fenceValue, (UINT) result);
            use.unknown=true; bridgeGeneration->revoked=true;
            DlssNr::BridgeTelemetry().Submitted(false);
            dx12EvalResult = false;
        }
        else
        {
            Dx12CommandAllocatorFenceValue[frame] = fenceValue;
        }
    }

    auto evalResult = false;

    do
    {
        if (!dx12EvalResult || !commandListExecuted)
            break;

        if (!CopyBackOutput(outcome))
        {
            LOG_ERROR("Can't copy output texture back!");
            DlssNr::BridgeTelemetry().CopyBack(false);
            break;
        }

        evalResult = true;

    } while (false);

    if (!evalResult) outcome.Fail();
    bridgeReceipt=outcome.Snapshot();
    if (evalResult)
        _frameCount++;
    else
        Dx11WithDx12::ClearLastPreparedUpscalerFrameState();

    // restore compute shader resources
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        InDeviceContext->CSSetShaderResources(i, 1, &restoreSRVs[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
    {
        InDeviceContext->CSSetSamplers(i, 1, &restoreSamplerStates[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
    {
        InDeviceContext->CSSetConstantBuffers(i, 1, &restoreCBVs[i]);
    }

    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
    {
        InDeviceContext->CSSetUnorderedAccessViews(i, 1, &restoreUAVs[i], 0);
    }

    InDeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, restoreRTVs, restoreDSV);

    for (auto* object : restoreSRVs) if (object) object->Release();
    for (auto* object : restoreSamplerStates) if (object) object->Release();
    for (auto* object : restoreCBVs) if (object) object->Release();
    for (auto* object : restoreUAVs) if (object) object->Release();
    for (auto* object : restoreRTVs) if (object) object->Release();
    if (restoreDSV) restoreDSV->Release();
    return evalResult;
}

bool IFeature_Dx11wDx12::BaseInit(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                                  NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    if (!InContext)
    {
        LOG_ERROR("context is null!");
        return false;
    }

    auto contextResult = InContext->QueryInterface(IID_PPV_ARGS(&Dx11DeviceContext));
    if (contextResult != S_OK)
    {
        LOG_ERROR("QueryInterface ID3D11DeviceContext4 result: {0:x}", contextResult);
        return false;
    }
    else
    {
        Dx11DeviceContext->Release();
    }

    if (!InDevice)
        Dx11DeviceContext->GetDevice(&InDevice);

    auto dx11DeviceResult = InDevice->QueryInterface(IID_PPV_ARGS(&Dx11Device));

    if (dx11DeviceResult != S_OK)
    {
        LOG_ERROR("QueryInterface ID3D11Device5 result: {0:x}", dx11DeviceResult);
        return false;
    }
    else
    {
        Dx11Device->Release();
    }

    if (!WithDx12::PrepareD3D12ForD3D11(InDevice, D3D_FEATURE_LEVEL_11_0))
    {
        LOG_ERROR("Cannot resolve D3D12 device/queue from WithDx12!");
        return false;
    }

    _dx11on12Device = WithDx12::GetD3D12Device();
    if (_dx11on12Device == nullptr)
    {
        LOG_ERROR("Cannot get D3D12 device from WithDx12!");
        return false;
    }

    Dx12CommandQueue = WithDx12::GetD3D12CommandQueue();
    if (Dx12CommandQueue == nullptr)
    {
        LOG_ERROR("Cannot get D3D12 command queue from WithDx12!");
        return false;
    }

    Dx12CommandListType = WithDx12::GetD3D12CommandListType();

    if (!CreateD3D12Objects())
    {
        LOG_ERROR("Failed to create D3D12 objects!");
        return false;
    }

    Dx11WithDx12::Init(Dx11Device, Dx11DeviceContext);

    return true;
}

IFeature_Dx11wDx12::IFeature_Dx11wDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters)
{
}

IFeature_Dx11wDx12::~IFeature_Dx11wDx12()
{
    auto lifecycleLock=DlssNr::LockBridgeLifecycle();
    auto resourceLock=Dx11WithDx12::LockUpscalerResources();
    if (!GenerationReusable())
    {
        // No destructor may invalidate a possibly submitted provider/resource.
        // Deliberate process-lifetime detachment: no allocation on the failure path.
        LOG_WARN("Dx11wDx12: retaining an unresolved bridge generation until process teardown");
        Dx11WithDx12::QuarantineUpscalerCache();
        (void)dx12Feature.release();
        (void)bridgeGeneration.release();
        return;
    }
    ReleaseSharedResources();
}
