#include "pch.h"
#include "FT_Dx12.h"
#include "FT_Common.h"

#include "precompile/FT_Shader.h"
#include "precompile/PresentColor_Shader.h"

#include <Config.h>

#include <magic_enum.hpp>

static DXGI_FORMAT GetCreateFormat(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    // Common UNORM 8-bit
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;

    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;

    // 10:10:10:2
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    }

    return fmt;
}

bool FT_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState)
{
    DXGI_FORMAT createFormat;
    if (InSource != nullptr)
    {
        auto inFormat = InSource->GetDesc().Format;
        createFormat = format;
        LOG_INFO("Input Format: {}, Create Format: {}", magic_enum::enum_name(inFormat),
                 magic_enum::enum_name(createFormat));
    }
    else
    {
        return false;
    }

    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto result =
        Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags, 0, 0, createFormat);

    if (result)
    {
        _buffer->SetName(L"FT_Buffer");
        _bufferState = InState;
    }

    return result;
}

void FT_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool FT_Dx12::BindImmutableDescriptors(ID3D12Resource* input, ID3D12Resource* output, ID3D12Resource* originalPq)
{
    if (!_init || input == nullptr || output == nullptr)
        return false;
    if (_needsReference && originalPq == nullptr)
        return false;
    if (_immutableInput != nullptr)
        return input == _immutableInput && output == _immutableOutput && originalPq == _immutableReference;
    if ((_bgraDecode || _bgraEncode) && !ValidBgraPair(input, output))
        return false;
    if (_bgraEncode && !CreatePackedBgra(output))
        return false;
    CreateShaderResourceView(_device, input, _frameHeaps[0].GetSrvCPU(0));
    if (_bgraEncode)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view {};
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements = static_cast<UINT>(_packedBgra->GetDesc().Width / 4);
        view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        _device->CreateUnorderedAccessView(_packedBgra, nullptr, &view, _frameHeaps[0].GetUavCPU(0));
    }
    else
        CreateUnorderedAccessView(_device, output, _frameHeaps[0].GetUavCPU(0), 0);
    _immutableInput = input;
    _immutableOutput = output;
    _immutableReference = originalPq;
    if (_needsReference)
        CreateShaderResourceView(_device, originalPq, _frameHeaps[0].GetSrvCPU(1));
    return true;
}

bool FT_Dx12::ValidBgraPair(ID3D12Resource* input, ID3D12Resource* output) const
{
    if (!input || !output) return false;
    const auto a = input->GetDesc(), b = output->GetDesc();
    const bool shape = a.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        b.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && a.Width && a.Height &&
        a.Width == b.Width && a.Height == b.Height && a.SampleDesc.Count == 1 && b.SampleDesc.Count == 1 &&
        a.DepthOrArraySize == 1 && b.DepthOrArraySize == 1 && a.MipLevels == 1 && b.MipLevels == 1;
    return shape && a.Format == (_bgraDecode ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM) &&
        b.Format == (_bgraDecode ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM) &&
        (a.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0 &&
        (!_bgraDecode || (b.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);
}

bool FT_Dx12::CreatePackedBgra(ID3D12Resource* output)
{
    const auto desc = output->GetDesc();
    UINT64 bytes = 0;
    _device->GetCopyableFootprints(&desc, 0, 1, 0, &_bgraFootprint, nullptr, nullptr, &bytes);
    if (!bytes || _bgraFootprint.Offset != 0 ||
        _bgraFootprint.Footprint.RowPitch != ((desc.Width * 4 + 255) / 256) * 256 ||
        bytes / 4 > UINT_MAX)
        return false;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    return SUCCEEDED(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&_packedBgra)));
}

bool FT_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;
    if (_needsReference && _immutableReference == nullptr)
        return false;
    // BGRA descriptors and its packed copy carrier belong to one drained generation.
    // Encode expects the BGRA destination in COPY_DEST, decode expects an RGBA UAV.
    if ((_bgraDecode || _bgraEncode) &&
        (_immutableInput == nullptr || !ValidBgraPair(InResource, OutResource)))
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), InCmdList);

    if (_immutableInput != nullptr)
    {
        if (InResource != _immutableInput || OutResource != _immutableOutput)
            return false;
        _counter = 0;
    }
    else
    {
        _counter = (_counter + 1) % FT_NUM_OF_HEAPS;
        CreateShaderResourceView(_device, InResource, _frameHeaps[_counter].GetSrvCPU(0));
        CreateUnorderedAccessView(_device, OutResource, _frameHeaps[_counter].GetUavCPU(0), 0);
    }
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    auto inDesc = InResource->GetDesc();
    dispatchWidth = static_cast<UINT>((inDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (inDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    if (_bgraEncode)
    {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_packedBgra,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        InCmdList->ResourceBarrier(1, &barrier);
    }
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    if (_bgraEncode)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_packedBgra,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        InCmdList->ResourceBarrier(1, &barrier);
        const CD3DX12_TEXTURE_COPY_LOCATION source(_packedBgra, _bgraFootprint), destination(OutResource, 0);
        InCmdList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(_packedBgra,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        InCmdList->ResourceBarrier(1, &barrier);
    }

    return true;
}

FT_Dx12::FT_Dx12(std::string InName, ID3D12Device* InDevice, DXGI_FORMAT InFormat, Transfer transfer)
    : Shader_Dx12(InName, InDevice), format(InFormat)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    _needsReference = transfer == Transfer::ScRgbToPq2020;
    _bgraDecode = transfer == Transfer::Bgra8ToRgba8;
    _bgraEncode = transfer == Transfer::Rgba8ToBgra8;
    if (!SetupRootSignature(InDevice, _needsReference ? 2 : 1, 1, 0))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    const void* code = FT_cso;
    size_t size = sizeof(FT_cso);
    if (transfer == Transfer::Pq2020ToScRgb) { code = PresentColorDecode_cso; size = sizeof(PresentColorDecode_cso); }
    if (transfer == Transfer::ScRgbToPq2020) { code = PresentColorEncode_cso; size = sizeof(PresentColorEncode_cso); }
    if (_bgraDecode) { code = PresentBgraDecode_cso; size = sizeof(PresentBgraDecode_cso); }
    if (_bgraEncode) { code = PresentBgraEncode_cso; size = sizeof(PresentBgraEncode_cso); }
    if (!CreateComputePipeline(InDevice, &_pipelineState, code, size,
                               transfer == Transfer::Copy ? FT_ShaderCode.c_str() : nullptr))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    _init = InitHeaps(InDevice, _frameHeaps, FT_NUM_OF_HEAPS);
}

bool FT_Dx12::IsFormatCompatible(DXGI_FORMAT InFormat)
{
    // Bold move: accept all formats
    return true;
}

FT_Dx12::~FT_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < FT_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    SAFE_RELEASE(_buffer);
    SAFE_RELEASE(_packedBgra);
}
