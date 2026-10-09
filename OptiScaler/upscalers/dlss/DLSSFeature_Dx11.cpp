#include <pch.h>
#include "DLSSFeature_Dx11.h"
#include <Config.h>
#include <dlssnr/FinalFallbackControl.h>

#include <dxgi.h>

bool DLSSFeatureDx11::InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_ERROR("nvngx.dll not loaded!");

        SetInit(false);
        return false;
    }

    NVSDK_NGX_Result nvResult;
    bool initResult = false;

    do
    {
        if (!_dlssInitedDx11)
        {
            _dlssInitedDx11 = NVNGXProxy::InitDx11(Device);

            if (!_dlssInitedDx11)
                return false;

            _moduleLoaded =
                (NVNGXProxy::D3D11_Init_ProjectID() != nullptr || NVNGXProxy::D3D11_Init_Ext() != nullptr) &&
                (NVNGXProxy::D3D11_Shutdown() != nullptr || NVNGXProxy::D3D11_Shutdown1() != nullptr) &&
                (NVNGXProxy::D3D11_GetParameters() != nullptr || NVNGXProxy::D3D11_AllocateParameters() != nullptr) &&
                NVNGXProxy::D3D11_DestroyParameters() != nullptr && NVNGXProxy::D3D11_CreateFeature() != nullptr &&
                NVNGXProxy::D3D11_ReleaseFeature() != nullptr && NVNGXProxy::D3D11_EvaluateFeature() != nullptr;

            // delay between init and create feature
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_INFO("Creating DLSS feature: device={} context={} NGX initialized={}",
                 static_cast<void*>(Device), static_cast<void*>(InContext), NVNGXProxy::IsDx11Inited());

        if (NVNGXProxy::D3D11_CreateFeature() != nullptr)
        {
            ProcessInitParams(InParameters);

            _p_dlssHandle = &_dlssHandle;
            nvResult = NVNGXProxy::D3D11_CreateFeature()(InContext, NVSDK_NGX_Feature_SuperSampling, InParameters,
                                                         &_p_dlssHandle);

            if (nvResult != NVSDK_NGX_Result_Success)
            {
                LOG_ERROR("NVNGXProxy::D3D11_CreateFeature result: {0:X}", (unsigned int) nvResult);
                if (nvResult == NVSDK_NGX_Result_FAIL_NotInitialized)
                    LOG_ERROR("Native DLSS creation refused: NGX reports NotInitialized despite cached init={}; device={} context={}. Native Temporal cannot attach to this failed DLSS feature.",
                              NVNGXProxy::IsDx11Inited(), static_cast<void*>(Device), static_cast<void*>(InContext));
                break;
            }
        }
        else
        {
            LOG_ERROR("NVNGXProxy::D3D11_CreateFeature is nullptr");
            break;
        }

        ReadVersion();

        _nrDx11.Created(InParameters);

        initResult = true;

    } while (false);

    SetInit(initResult);

    return initResult;
}

bool DLSSFeatureDx11::EvaluateInternal(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    // Keep NR admission across Prepare, the game DLSS call, and completion.
    // Suppression skips NR only; the game's ordinary DLSS evaluation continues.
    DlssNr::FinalFallback::RenderScope renderAdmission;
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    NVSDK_NGX_Result nvResult;

    if (NVNGXProxy::D3D11_EvaluateFeature() != nullptr)
    {
        ProcessEvaluateParams(InParameters);

        if (renderAdmission.Admitted()) _nrDx11.Prepare(InDeviceContext, InParameters);

        struct RestoreNativeDx11Parameters
        {
            DlssNr::NativeDx11::Feature& feature;
            NVSDK_NGX_Parameter* parameters;
            bool admitted;
            ~RestoreNativeDx11Parameters() { if (admitted) feature.Restore(parameters); }
        } restore {_nrDx11, InParameters, renderAdmission.Admitted()};

        nvResult = NVNGXProxy::D3D11_EvaluateFeature()(InDeviceContext, _p_dlssHandle, InParameters, NULL);

        if (renderAdmission.Admitted())
        {
            _nrDx11.Restore(InParameters);
            _nrDx11.Complete(InDeviceContext, nvResult == NVSDK_NGX_Result_Success);
        }

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }

        LOG_TRACE("_EvaluateFeature ok!");
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    return true;
}

DLSSFeatureDx11::DLSSFeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters), DLSSFeature(InHandleId, InParameters)
{
    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_INFO("nvngx.dll not loaded, now loading");
        NVNGXProxy::InitNVNGX();
    }

    LOG_INFO("binding complete!");
}

DLSSFeatureDx11::~DLSSFeatureDx11()
{
    if (State::Instance().isShuttingDown)
        return;

    if (NVNGXProxy::D3D11_ReleaseFeature() != nullptr && _p_dlssHandle != nullptr)
        NVNGXProxy::D3D11_ReleaseFeature()(_p_dlssHandle);
}
