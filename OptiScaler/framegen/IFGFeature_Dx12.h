#pragma once
#include "SysUtils.h"
#include "IFGFeature.h"

#include <upscalers/IFeature.h>

#include <shaders/resource_flip/RF_Dx12.h>
#include <shaders/hudless_compare/HC_Dx12.h>
#include <shaders/render_ui/RUI_Dx12.h>

#include <dxgi1_6.h>
#include <d3d12.h>
#include <dlssnr/NrGpuSafety.h>
#include <atomic>

struct Dx12Resource
{
    FG_ResourceType type;
    ID3D12Resource* resource = nullptr;
    UINT top = 0;
    UINT left = 0;
    UINT64 width = 0;
    UINT height = 0;
    ID3D12GraphicsCommandList* cmdList = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    FG_ResourceValidity validity = FG_ResourceValidity::ValidNow;

    // TODO: make private?
    ID3D12Resource* copy = nullptr;
    int frameIndex = -1;
    bool waitingExecution = false;

    ID3D12Resource* GetResource() { return (copy == nullptr) ? resource : copy; }
};

struct LockedDx12Resource
{
    Dx12Resource* resource = nullptr;
    std::shared_lock<std::shared_mutex> lock;

    Dx12Resource* operator->() { return resource; }
    Dx12Resource& operator*() { return *resource; }

    explicit operator bool() const { return resource != nullptr; }
};

class IFGFeature_Dx12 : public virtual IFGFeature
{
  private:
    // Only Streamline scoped input uses this gate; other adapters start ready.
    std::atomic<bool> _streamlineInputsReady[BUFFER_COUNT] { true, true, true, true };
    ID3D12GraphicsCommandList* _copyCommandList[BUFFER_COUNT] {};
    ID3D12CommandAllocator* _copyCommandAllocator[BUFFER_COUNT] {};

    bool InitCopyCmdList();
    void DestroyCopyCmdList();
    bool WaitForUIAllocator(UINT index);
    bool SubmitUICommandList(UINT index);

  protected:
    ID3D12Device* _device = nullptr;
    IDXGISwapChain* _swapChain = nullptr;
    ID3D12CommandQueue* _gameCommandQueue = nullptr;

    HWND _hwnd = NULL;

    UINT64 _fgFramePresentId = 0;
    UINT64 _lastFGFramePresentId = 0;

    ID3D12GraphicsCommandList* _scCommandList[BUFFER_COUNT] {};
    ID3D12CommandAllocator* _scCommandAllocator[BUFFER_COUNT] {};
    bool _scCommandListResetted[BUFFER_COUNT] { false, false, false, false };
    UINT64 _scAllocatorFenceValues[BUFFER_COUNT] {};
    ID3D12Fence* _scFence = nullptr;
    HANDLE _scFenceEvent = nullptr;
    UINT64 _scFenceValue = 0;
    bool _scCompletionUnknown = false;
    std::vector<Microsoft::WRL::ComPtr<IUnknown>> _scPins;
    bool RetireSCWork();
    bool SubmitSCCommandList(UINT index);
    void RetainSCObjectsOnFailure();
    void PinSCResource(ID3D12Resource* resource) { if(resource) _scPins.emplace_back(resource); }

    ID3D12GraphicsCommandList* _uiCommandList[BUFFER_COUNT] {};
    bool _uiBridgeReader[BUFFER_COUNT] {};
    DlssNr::GpuSafety::Ticket _uiBridgeUse[BUFFER_COUNT];
    ID3D12CommandAllocator* _uiCommandAllocator[BUFFER_COUNT] {};
    bool _uiCommandListResetted[BUFFER_COUNT] { false, false, false, false };
    UINT64 _uiAllocatorFenceValues[BUFFER_COUNT] {};
    UINT64 _uiFenceValue = 0;
    ID3D12Fence* _uiFence = nullptr;
    HANDLE _uiFenceEvent = nullptr;

    std::unordered_map<FG_ResourceType, Dx12Resource> _frameResources[BUFFER_COUNT] {};
    std::unordered_map<FG_ResourceType, ID3D12Resource*> _resourceCopy[BUFFER_COUNT] {};
    std::shared_mutex _resourceMutex[BUFFER_COUNT];

    std::unique_ptr<RF_Dx12> _mvFlip;
    std::unique_ptr<RF_Dx12> _depthFlip;
    std::unique_ptr<HC_Dx12> _hudlessCompare;
    std::unique_ptr<RUI_Dx12> _renderUI;

    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState,
                              ID3D12Resource** OutResource, bool UAV = false, bool depth = false);
    bool CreateBufferResourceWithSize(ID3D12Device* device, ID3D12Resource* source, D3D12_RESOURCE_STATES state,
                                      ID3D12Resource** target, UINT width, UINT height, bool UAV, bool depth);
    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);
    bool CopyResource(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source, ID3D12Resource** target,
                      D3D12_RESOURCE_STATES sourceState);

    void NewFrame() override final;
    void FlipResource(Dx12Resource* resource);

  protected:
    virtual void ReleaseObjects() = 0;
    virtual void CreateObjects(ID3D12Device* InDevice) = 0;
    bool RetireUIBridgeReaders();

  public:
    virtual void* FrameGenerationContext() = 0;
    virtual void* SwapchainContext() = 0;
    virtual HWND Hwnd() = 0;
    bool OwnsSwapchain(IUnknown* candidate) const;
    IUnknown* SwapchainIdentity() const noexcept { return _swapChain; }
    inline static std::atomic<uint64_t> nextInputOwner {1};
    const uint64_t inputOwner = nextInputOwner.fetch_add(1);
    bool OwnsBackbuffer(ID3D12Resource* resource) const;

    virtual bool CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                 IDXGISwapChain** swapChain, bool readyToRelease) = 0;
    virtual bool CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                  DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                  IDXGISwapChain1** swapChain, bool readyToRelease) = 0;

    virtual void CreateContext(ID3D12Device* device, FG_Constants& fgConstants) = 0;
    virtual void EvaluateState(ID3D12Device* device, FG_Constants& fgConstants) = 0;

    virtual bool SetResource(Dx12Resource* inputResource) = 0;
    bool CanDispatchInputs(int index) const noexcept override { return StreamlineInputsReady(index); }
    bool StreamlineInputsReady(int index) const noexcept
    {
        return index >= 0 && index < BUFFER_COUNT &&
               _streamlineInputsReady[index].load(std::memory_order_acquire);
    }
    void SetStreamlineInputsReady(int index, bool ready) noexcept
    {
        if (index >= 0 && index < BUFFER_COUNT)
            _streamlineInputsReady[index].store(ready, std::memory_order_release);
    }
    virtual void SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) = 0;

    ID3D12GraphicsCommandList* GetUICommandList(int index = -1, bool bridgeReader = false);
    bool ObserveUIBridgeSubmission(UINT index);
    bool RetainUIBridgeReader(const DlssNr::GpuSafety::LocalRecordingAction&);
    ID3D12GraphicsCommandList* GetSCCommandList(int index = -1);

    LockedDx12Resource GetResource(FG_ResourceType type, int index = -1);
    void RevokeResource(FG_ResourceType type, int index);
    bool GetResourceCopy(FG_ResourceType type, D3D12_RESOURCE_STATES bufferState, ID3D12Resource* output);
    ID3D12CommandQueue* GetCommandQueue();

    bool HasResource(FG_ResourceType type, int index = -1) override final;

    IFGFeature_Dx12() = default;
    virtual ~IFGFeature_Dx12() { DestroyCopyCmdList(); }
};
