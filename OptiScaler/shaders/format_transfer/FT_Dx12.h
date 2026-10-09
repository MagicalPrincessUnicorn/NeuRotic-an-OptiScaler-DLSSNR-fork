#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#define FT_NUM_OF_HEAPS 2

class FT_Dx12 : public Shader_Dx12
{
  private:
    FrameDescriptorHeap _frameHeaps[FT_NUM_OF_HEAPS];
    ID3D12Resource* _immutableInput = nullptr;
    ID3D12Resource* _immutableOutput = nullptr;
    ID3D12Resource* _immutableReference = nullptr;
    bool _needsReference = false;
    bool _bgraDecode = false;
    bool _bgraEncode = false;
    ID3D12Resource* _packedBgra = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT _bgraFootprint {};
    bool ValidBgraPair(ID3D12Resource* input, ID3D12Resource* output) const;
    bool CreatePackedBgra(ID3D12Resource* output);

    ID3D12Resource* _buffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState = D3D12_RESOURCE_STATE_COMMON;
    DXGI_FORMAT format;

    uint32_t bufferWidth = 0;
    uint32_t bufferHeight = 0;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    enum class Transfer { Copy, Pq2020ToScRgb, ScRgbToPq2020, Bgra8ToRgba8, Rgba8ToBgra8 };
    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState);
    void SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource);
    // Bind once before submission. Caller retains this resource generation until GPU completion.
    bool BindImmutableDescriptors(ID3D12Resource* input, ID3D12Resource* output,
                                  ID3D12Resource* originalPq = nullptr);

    ID3D12Resource* Buffer() { return _buffer; }
    bool CanRender() const { return _init && _buffer != nullptr; }
    bool Ready() const { return _init; }
    DXGI_FORMAT Format() const { return format; }

    FT_Dx12(std::string InName, ID3D12Device* InDevice, DXGI_FORMAT InFormat,
            Transfer transfer = Transfer::Copy);

    bool IsFormatCompatible(DXGI_FORMAT InFormat);

    ~FT_Dx12();
};
