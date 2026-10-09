#pragma once
#include "SysUtils.h"
#include <d3d12.h>
#include <dlssnr/NrGpuSafety.h>

class GpuTime_Dx12
{
    static constexpr int QUERY_BUFFER_COUNT = 3;

    ID3D12QueryHeap* _queryHeap = nullptr;
    ID3D12Resource* _readbackBuffer = nullptr;
    std::array<bool, QUERY_BUFFER_COUNT> _trigger {};

    int _currentFrameIndex = 0;
    bool _init = false;
    bool _completionTracked = true;
    bool _recording = false;
    DlssNr::GpuSafety::Ticket _use[QUERY_BUFFER_COUNT];

  public:
    GpuTime_Dx12(ID3D12Device* device, bool completionTracked = true);
    ~GpuTime_Dx12();

    void Start(ID3D12GraphicsCommandList* cmdList);
    void End(ID3D12GraphicsCommandList* cmdList);

    std::optional<double> ReadGpuTime(ID3D12CommandQueue* commandQueue);
    // Discard observations without releasing resources or weakening in-flight reuse fences.
    void InvalidateSamples() { _trigger.fill(false); _recording = false; }
};

class ScopedGpuTime_Dx12
{
    GpuTime_Dx12* _gpuTime;
    ID3D12GraphicsCommandList* _cmdList;

  public:
    ScopedGpuTime_Dx12(GpuTime_Dx12* gpuTime, ID3D12GraphicsCommandList* cmdList) : _gpuTime(gpuTime), _cmdList(cmdList)
    {
        if (_gpuTime && _cmdList)
            _gpuTime->Start(_cmdList);
    }

    ~ScopedGpuTime_Dx12()
    {
        if (_gpuTime && _cmdList)
            _gpuTime->End(_cmdList);
    }
};
