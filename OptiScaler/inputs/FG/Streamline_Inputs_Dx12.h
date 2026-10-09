#pragma once
#include "SysUtils.h"
#include <sl.h>
#include <framegen/IFGFeature_Dx12.h>
#include "FgOwnerScope.h"

class Sl_Inputs_Dx12
{
  private:
    bool infiniteDepth = false;
    sl::EngineType engineType = sl::EngineType::eCount;

    std::recursive_mutex _frameBoundaryMutex;
    uint64_t _provider = 0, _owner = 0;
    std::optional<uint32_t> _viewport;
    IUnknown* _chain = nullptr;
    bool _haveFrame = false;
    bool _haveConstants = false;
    bool _isFrameFinished = true;

    uint32_t _currentFrameId = 0;
    uint32_t _currentIndex = -1;
    uint32_t _lastPresentFrameId = UINT32_MAX;
    uint32_t _frameIdIndex[BUFFER_COUNT] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    bool _frameSlotValid[BUFFER_COUNT] {};

    uint64_t mvsWidth = 0;
    uint32_t mvsHeight = 0;
    float _mvScaleX = 0.0f, _mvScaleY = 0.0f;

    void CheckForFrame(IFGFeature_Dx12* fg, uint32_t frameId);
    int IndexForFrameId(uint32_t frameId) const;

  public:
    static uint64_t CaptureOwner();
    void acceptedTags(const sl::ResourceTag* tags, uint32_t count, ID3D12GraphicsCommandList* list,
                      uint32_t frame, uint32_t viewport, uint64_t provider, uint64_t owner, bool accepted);
    bool setConstants(const sl::Constants& constants, uint32_t frameId, uint32_t viewport,
                      uint64_t provider, uint64_t owner);
    bool evaluateState();
    bool reportResource(const sl::ResourceTag& tag, ID3D12GraphicsCommandList* cmdBuffer, uint32_t frameId);
    void reportEngineType(sl::EngineType type)
    {
        std::scoped_lock ownerLock(Neurotic::Runtime::FgOwnerMutex());
        std::scoped_lock frameLock(_frameBoundaryMutex);
        engineType = type;
    }
    bool dispatchFG();
    void markPresent(uint64_t frameId);

    // TODO: some shutdown and cleanup methods
};
