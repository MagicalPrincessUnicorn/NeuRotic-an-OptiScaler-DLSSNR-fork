#include "pch.h"
#include "Streamline_Inputs_Dx12.h"
#include <Config.h>
#include <resource_tracking/ResTrack_dx12.h>
#include <magic_enum.hpp>

uint64_t Sl_Inputs_Dx12::CaptureOwner()
{
    std::scoped_lock lock(Neurotic::Runtime::FgOwnerMutex());
    if (Neurotic::Runtime::FgOwnerChanging()) return 0;
    auto* fg = State::Instance().currentFG;
    if (!fg) return 0;
    static uint64_t previousOwner = 0, previousEpoch = 0, stamp = 0;
    const auto epoch = Neurotic::Runtime::FgOwnerEpoch();
    if (previousOwner != fg->inputOwner || previousEpoch != epoch)
    {
        previousOwner = fg->inputOwner; previousEpoch = epoch; ++stamp;
    }
    return stamp;
}

void Sl_Inputs_Dx12::acceptedTags(const sl::ResourceTag* tags, uint32_t count, ID3D12GraphicsCommandList* list,
    uint32_t frame, uint32_t viewport, uint64_t provider, uint64_t owner, bool accepted)
{
    std::scoped_lock ownerLock(Neurotic::Runtime::FgOwnerMutex());
    std::scoped_lock frameLock(_frameBoundaryMutex);
    auto* fg = State::Instance().currentFG;
    if (!fg || !owner || CaptureOwner() != owner || !provider || count > 128 || (!tags && count)) return;
    if (_owner != owner || _provider != provider || _chain != fg->SwapchainIdentity())
    {
        _owner = owner; _provider = provider; _chain = fg->SwapchainIdentity(); _viewport.reset(); _haveFrame = false;
        std::fill(std::begin(_frameIdIndex), std::end(_frameIdIndex), UINT32_MAX);
        std::fill(std::begin(_frameSlotValid), std::end(_frameSlotValid), false);
        for (int slot = 0; slot < BUFFER_COUNT; ++slot)
        {
            fg->SetStreamlineInputsReady(slot, false);
            for (auto type : {FG_ResourceType::Depth, FG_ResourceType::Velocity, FG_ResourceType::HudlessColor, FG_ResourceType::UIColor})
                fg->RevokeResource(type, slot);
        }
    }
    // The tag API has no swapchain argument. Bind a viewport only from an
    // accepted actual backbuffer identity, never from global currentFG alone.
    if (!_viewport && accepted)
        for (uint32_t i = 0; i < count; ++i)
            if (tags[i].type == sl::kBufferTypeBackbuffer && tags[i].resource &&
                fg->OwnsBackbuffer(static_cast<ID3D12Resource*>(tags[i].resource->native))) _viewport = viewport;
    if (!_viewport || *_viewport != viewport) return;
    if (_haveFrame && frame == _currentFrameId && _isFrameFinished) return;
    if (_haveFrame && frame != _currentFrameId && static_cast<int32_t>(frame - _currentFrameId) <= 0) return;
    CheckForFrame(fg, frame);
    const int slot = IndexForFrameId(frame);
    if (slot < 0) return;
    fg->SetStreamlineInputsReady(slot, false);
    if (!tags)
    {
        for (auto type : {FG_ResourceType::Depth, FG_ResourceType::Velocity, FG_ResourceType::HudlessColor, FG_ResourceType::UIColor})
            fg->RevokeResource(type, slot);
        fg->SetStreamlineInputsReady(slot, _haveConstants);
        return;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& tag = tags[i];
        std::optional<FG_ResourceType> type;
        if (tag.type == sl::kBufferTypeDepth || tag.type == sl::kBufferTypeHiResDepth || tag.type == sl::kBufferTypeLinearDepth) type = FG_ResourceType::Depth;
        else if (tag.type == sl::kBufferTypeMotionVectors) type = FG_ResourceType::Velocity;
        else if (tag.type == sl::kBufferTypeHUDLessColor) type = FG_ResourceType::HudlessColor;
        else if (tag.type == sl::kBufferTypeUIColorAndAlpha) type = FG_ResourceType::UIColor;
        if (!type) continue;
        if (!accepted || !tag.resource || !tag.resource->native || !reportResource(tag, list, frame))
            fg->RevokeResource(*type, slot);
    }
    fg->SetStreamlineInputsReady(slot, _haveConstants);
}

void Sl_Inputs_Dx12::CheckForFrame(IFGFeature_Dx12* fg, uint32_t frameId)
{
    // Caller holds the owner and complete feeder transaction locks.
    if (_haveFrame && (frameId == _currentFrameId || static_cast<int32_t>(frameId - _currentFrameId) <= 0)) return;
    _isFrameFinished = false;
    _haveConstants = false;
    mvsWidth = 0;
    mvsHeight = 0;
    fg->SetStreamlineInputsReady((fg->GetIndex() + 1) % BUFFER_COUNT, false);
    fg->StartNewFrame();
    _currentIndex = fg->GetIndex();
    fg->SetStreamlineInputsReady(_currentIndex, false);
    _currentFrameId = frameId;
    _frameIdIndex[_currentIndex] = frameId;
    _frameSlotValid[_currentIndex] = true;
    _haveFrame = true;
}
int Sl_Inputs_Dx12::IndexForFrameId(uint32_t frameId) const
{
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
        if (_frameSlotValid[i] && _frameIdIndex[i] == frameId)
            return i;
    }

    return -1;
}

bool Sl_Inputs_Dx12::setConstants(const sl::Constants& values, uint32_t frameId, uint32_t viewport,
                                    uint64_t provider, uint64_t owner)
{
    std::scoped_lock ownerLock(Neurotic::Runtime::FgOwnerMutex());
    std::scoped_lock frameLock(_frameBoundaryMutex);
    if (!_viewport || *_viewport != viewport || _provider != provider || _owner != owner || CaptureOwner() != owner) return false;
    if (_haveFrame && frameId == _currentFrameId && _isFrameFinished) return false;
    if (_haveFrame && frameId != _currentFrameId && static_cast<int32_t>(frameId - _currentFrameId) <= 0) return false;
    auto fgOutput = State::Instance().currentFG;

    if (fgOutput == nullptr)
        return false;

    CheckForFrame(fgOutput, frameId);
    fgOutput->SetStreamlineInputsReady(_currentIndex, false);

    auto data = sl::Constants {};
    bool dataFound = false;

    if (values.structVersion == data.structVersion)
    {
        data = values;

        dataFound = true;
    }
    else if ((data.structVersion == sl::kStructVersion2 && values.structVersion == sl::kStructVersion1) ||
             values.structVersion == 0)
    // Spider-Man Remastered does this funny thing of sending an invalid struct version
    {
        auto* pNext = data.next;
        memcpy(&data, &values, sizeof(values) - sizeof(sl::Constants::minRelativeLinearDepthObjectSeparation));
        data.structVersion = sl::kStructVersion2;
        data.next = pNext;

        dataFound = true;
    }

    if (dataFound)
    {
        auto config = Config::Instance();

        // FG Evaluate part
        FG_Constants fgConstants {};

        // TODO
        fgConstants.displayWidth = 0;
        fgConstants.displayHeight = 0;

        fgConstants.flags.reset();

        // if ()
        //     fgConstants.flags |= FG_Flags::Hdr;

        if (data.depthInverted)
            fgConstants.flags |= FG_Flags::InvertedDepth;

        if (data.motionVectorsJittered)
            fgConstants.flags |= FG_Flags::JitteredMVs;

        if (data.motionVectorsDilated)
            fgConstants.flags |= FG_Flags::DisplayResolutionMVs;

        if (config->FGAsync.value_or_default())
            fgConstants.flags |= FG_Flags::Async;

        if (infiniteDepth)
            fgConstants.flags |= FG_Flags::InfiniteDepth;

        if (config->FGXeFGDepthInverted.value_or_default() != (data.depthInverted == sl::Boolean::eTrue) ||
            config->FGXeFGJitteredMV.value_or_default() != (data.motionVectorsJittered == sl::Boolean::eTrue) ||
            config->FGXeFGHighResMV.value_or_default() != (data.motionVectorsDilated == sl::Boolean::eTrue))
        {
            config->FGXeFGDepthInverted = (data.depthInverted == sl::Boolean::eTrue);
            config->FGXeFGJitteredMV = (data.motionVectorsJittered == sl::Boolean::eTrue);
            config->FGXeFGHighResMV = (data.motionVectorsDilated == sl::Boolean::eTrue);
            LOG_DEBUG("XeFG DepthInverted: {}", config->FGXeFGDepthInverted.value_or_default());
            LOG_DEBUG("XeFG JitteredMV: {}", config->FGXeFGJitteredMV.value_or_default());
            LOG_DEBUG("XeFG HighResMV: {}", config->FGXeFGHighResMV.value_or_default());
            config->SaveXeFG();
        }

        fgOutput->EvaluateState(State::Instance().currentD3D12Device, fgConstants);

        if (!config->FGEnabled.value_or_default())
        {
            LOG_TRACE("FG not enabled");
            return true;
        }
        else
        {
            if (!fgOutput->IsActive() && !fgOutput->IsPaused())
            {
                fgOutput->Activate();
            }
            else if (!fgOutput->IsActive() || fgOutput->IsPaused())
            {
                LOG_TRACE("FG not active or paused (A:{}, P:{})", fgOutput->IsActive(), fgOutput->IsPaused());
                return true;
            }
        }

        // Frame data part
        // Nukem's function, licensed under GPLv3
        auto loadCameraMatrix = [&]()
        {
            if (data.orthographicProjection)
                return false;

            float projMatrix[4][4];
            memcpy(projMatrix, (void*) &data.cameraViewToClip, sizeof(projMatrix));

            // BUG: Various RTX Remix-based games pass in an identity matrix which is completely useless. No
            // idea why.
            const bool isEmptyOrIdentityMatrix = [&]()
            {
                float m[4][4] = {};
                if (memcmp(projMatrix, m, sizeof(m)) == 0)
                    return true;

                m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
                return memcmp(projMatrix, m, sizeof(m)) == 0;
            }();

            if (isEmptyOrIdentityMatrix)
                return false;

            // a 0 0 0
            // 0 b 0 0
            // 0 0 c e
            // 0 0 d 0
            const double b = projMatrix[1][1];
            const double c = projMatrix[2][2];
            const double d = projMatrix[3][2];
            const double e = projMatrix[2][3];

            if (e < 0.0)
            {
                data.cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (d / c));
                data.cameraFar = static_cast<float>(d / (c + 1.0));
            }
            else
            {
                data.cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (-d / c));
                data.cameraFar = static_cast<float>(-d / (c - 1.0));
            }

            if (data.depthInverted)
                std::swap(data.cameraNear, data.cameraFar);

            data.cameraFOV = static_cast<float>(2.0 * std::atan(1.0 / b));
            return true;
        };

        bool dontRecalc = false;

        LOG_TRACE("Camera from SL pre recalc near: {}, far: {}", data.cameraNear, data.cameraFar);

        // UE seems to not be passing the correct cameraViewToClip
        // and we can't use it to calculate cameraNear and cameraFar.
        if (engineType != sl::EngineType::eUnreal && !dontRecalc)
            loadCameraMatrix();

        // Workaround for more games with broken cameraViewToClip
        if (!dontRecalc && (data.cameraNear < 0.0f || data.cameraFar < 0.0f))
            dontRecalc = true;

        infiniteDepth = false;
        if (data.cameraNear != 0.0f && data.cameraFar == 0.0f)
        {
            // A CameraFar value of zero indicates an infinite far plane. Due to a bug in FSR's
            // setupDeviceDepthToViewSpaceDepthParams function, CameraFar must always be greater than
            // CameraNear when in use.

            infiniteDepth = true;
            data.cameraFar = data.cameraNear + 1.0f;
        }

        fgOutput->SetCameraValues(data.cameraNear, data.cameraFar, data.cameraFOV, data.cameraAspectRatio, 0.0f);

        fgOutput->SetJitter(data.jitterOffset.x, data.jitterOffset.y);

        // Streamline is not 100% clear on if we should multiply by resolution or not.
        // But UE games and Dead Rising expect that multiplication to be done, even if the scale is 1.0.
        // bool multiplyByResolution = dataCopy.mvecScale.x != 1.f || dataCopy.mvecScale.y != 1.f;
        bool multiplyByResolution = true;
        _mvScaleX = data.mvecScale.x;
        _mvScaleY = data.mvecScale.y;
        if (multiplyByResolution)
            fgOutput->SetMVScale(data.mvecScale.x * mvsWidth, data.mvecScale.y * mvsHeight);
        else
            fgOutput->SetMVScale(data.mvecScale.x, data.mvecScale.y);

        fgOutput->SetCameraData(reinterpret_cast<float*>(&data.cameraPos), reinterpret_cast<float*>(&data.cameraUp),
                                reinterpret_cast<float*>(&data.cameraRight), reinterpret_cast<float*>(&data.cameraFwd));

        fgOutput->SetReset(data.reset == sl::Boolean::eTrue);

        fgOutput->SetFrameTimeDelta(static_cast<float>(State::Instance().lastFGFrameTime));
    }
    else
    {
        LOG_ERROR("Wrong constant struct version");
    }

    _haveConstants = dataFound;
    fgOutput->SetStreamlineInputsReady(_currentIndex, dataFound);
    return dataFound;
}

bool Sl_Inputs_Dx12::evaluateState()
{
    std::scoped_lock ownerLock(Neurotic::Runtime::FgOwnerMutex());
    std::scoped_lock frameLock(_frameBoundaryMutex);
    if (Neurotic::Runtime::FgOwnerChanging()) return false;
    auto fgOutput = State::Instance().currentFG;

    if (fgOutput == nullptr)
        return false;

    LOG_FUNC();

    static UINT64 lastFrameCount = 0;
    static UINT64 repeatsInRow = 0;
    if (lastFrameCount == fgOutput->FrameCount())
    {
        repeatsInRow++;
    }
    else
    {
        lastFrameCount = fgOutput->FrameCount();
        repeatsInRow = 0;
    }

    if (repeatsInRow > 10 && fgOutput->IsActive())
    {
        LOG_WARN("Many frame count repeats in a row, stopping FG");
        State::Instance().fgChanged = true;
        repeatsInRow = 0;
        return false;
    }

    return true;
}

bool Sl_Inputs_Dx12::reportResource(const sl::ResourceTag& tag, ID3D12GraphicsCommandList* cmdBuffer, uint32_t frameId)
{
    std::scoped_lock ownerLock(Neurotic::Runtime::FgOwnerMutex());
    std::scoped_lock frameLock(_frameBoundaryMutex);
    if (Neurotic::Runtime::FgOwnerChanging()) return false;
    auto& state = State::Instance();
    state.dlssgLastFrame = state.fgLastFrame;

    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

    // It's possible for only some resources to be marked ready if FGEnabled is enabled during resource tagging
    if (fgOutput == nullptr || !Config::Instance()->FGEnabled.value_or_default())
        return false;

    static const bool ignoreValidUntilEvaluateForFG =
        State::Instance().gameQuirks[GameQuirk::IgnoreValidUntilEvaluateForFG];

    // eValidUntilEvaluate is usually used for upscaling, not FG
    // we could track multiple versions of the same resource for the same frame id
    // to be able to compare validity but that seems pointless
    if (tag.lifecycle == sl::eValidUntilEvaluate && ignoreValidUntilEvaluateForFG)
        return false;

    LOG_DEBUG("Reporting SL resource type: {} lifecycle: {} frameId: {}", tag.type,
              magic_enum::enum_name(tag.lifecycle), frameId);

    CheckForFrame(fgOutput, frameId);

    if (!tag.resource || tag.resource->native == nullptr)
    {
        LOG_TRACE("tag.resource->native is null");
        return false;
    }

    if (!cmdBuffer && tag.lifecycle == sl::eOnlyValidNow)
        LOG_TRACE("cmdBuffer is null");

    auto d3dRes = (ID3D12Resource*) tag.resource->native;
    auto desc = d3dRes->GetDesc();
    if (tag.extent && (tag.extent.left > desc.Width || tag.extent.width > desc.Width - tag.extent.left ||
        tag.extent.top > desc.Height || tag.extent.height > desc.Height - tag.extent.top)) return false;
    Microsoft::WRL::ComPtr<ID3D12Device> resourceDevice, chainDevice;
    Microsoft::WRL::ComPtr<IDXGISwapChain> chain;
    if (!_chain || FAILED(_chain->QueryInterface(IID_PPV_ARGS(&chain))) ||
        FAILED(d3dRes->GetDevice(IID_PPV_ARGS(&resourceDevice))) ||
        FAILED(chain->GetDevice(IID_PPV_ARGS(&chainDevice))) || resourceDevice.Get() != chainDevice.Get()) return false;

    Dx12Resource res = {};
    res.resource = d3dRes;
    res.cmdList = cmdBuffer; // Critical for eOnlyValidNow
    res.width = tag.extent ? tag.extent.width : desc.Width;
    res.height = tag.extent ? tag.extent.height : desc.Height;
    res.left = tag.extent.left;
    res.top = tag.extent.top;
    res.state = (D3D12_RESOURCE_STATES) tag.resource->state;
    res.validity =
        (tag.lifecycle == sl::eValidUntilPresent) ? FG_ResourceValidity::UntilPresent : FG_ResourceValidity::ValidNow;

    // If eValidUntilEvaluate is provided without cmdList when there's not much we can do
    if (!res.cmdList && res.validity != FG_ResourceValidity::UntilPresent)
    {
        return false; // Missing copy rights never extends the caller lifetime.
    }

    if (frameId > 0)
    {
        int index = IndexForFrameId(frameId);

        if (index >= 0)
        {
            res.frameIndex = index;
        }
        else
        {
            return false;
        }
    }
    else
    {
        res.frameIndex = IndexForFrameId(frameId);
        if (res.frameIndex < 0) return false;
    }

    bool handled = true;
    const auto publish = [&]() {
        // Snapshot short-lived data now, inside the caller's copy rights. The
        // provider owns this private copy in its existing per-slot storage;
        // dispatch remains closed until matching constants commit.
        if (!_haveConstants && res.cmdList)
            res.validity = FG_ResourceValidity::ValidButMakeCopy;
        return fgOutput->SetResource(&res);
    };

    // Map types
    if (tag.type == sl::kBufferTypeDepth || tag.type == sl::kBufferTypeHiResDepth ||
        tag.type == sl::kBufferTypeLinearDepth)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::Depth, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        if (Config::Instance()->FGDepthValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        res.type = FG_ResourceType::Depth;
        handled = publish();
    }
    else if (tag.type == sl::kBufferTypeMotionVectors)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::Velocity, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        if (Config::Instance()->FGVelocityValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        res.type = FG_ResourceType::Velocity;
        mvsWidth = res.width; // Track locally for dispatch logic
        mvsHeight = res.height;
        handled = publish();
        if (handled && _haveConstants)
            fgOutput->SetMVScale(_mvScaleX * mvsWidth, _mvScaleY * mvsHeight);
    }
    else if (tag.type == sl::kBufferTypeHUDLessColor)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::HudlessColor, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        res.type = FG_ResourceType::HudlessColor;

        if (Config::Instance()->FGHudlessValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        fgOutput->SetInterpolationRect(res.width, res.height);
        handled = publish();
    }
    else if (tag.type == sl::kBufferTypeUIColorAndAlpha)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::UIColor, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        res.type = FG_ResourceType::UIColor;

        // Fallback size logic
        UINT64 width = 0;
        UINT height = 0;
        fgOutput->GetInterpolationRect(width, height, _currentIndex);

        if (width == 0)
            fgOutput->SetInterpolationRect(res.width, res.height);

        handled = publish();
    }
    else
    {
        handled = false;
    }

    return handled;
}

bool Sl_Inputs_Dx12::dispatchFG()
{
    std::scoped_lock ownerLock(Neurotic::Runtime::FgOwnerMutex());
    std::scoped_lock frameLock(_frameBoundaryMutex);
    LOG_FUNC();
    return true;
}

void Sl_Inputs_Dx12::markPresent(uint64_t frameId)
{
    std::scoped_lock ownerLock(Neurotic::Runtime::FgOwnerMutex());
    std::scoped_lock frameLock(_frameBoundaryMutex);
    if (!_haveFrame || !_viewport || CaptureOwner() != _owner ||
        frameId != _currentFrameId) return;
    std::scoped_lock lock(_frameBoundaryMutex);
    LOG_TRACE("frameId: {}", frameId);
    _isFrameFinished = true;
    _lastPresentFrameId = static_cast<uint32_t>(frameId);

    if (!_haveConstants)
    {
        auto* fg = State::Instance().currentFG;
        fg->SetStreamlineInputsReady(_currentIndex, false);
        for (auto type : {FG_ResourceType::Depth, FG_ResourceType::Velocity, FG_ResourceType::HudlessColor, FG_ResourceType::UIColor})
            fg->RevokeResource(type, _currentIndex);
        return; // UntilPresent rights expire here; late constants cannot resurrect them.
    }

    if (State::Instance().currentFG != nullptr)
        State::Instance().currentFG->SetFrameCount(frameId);
}
