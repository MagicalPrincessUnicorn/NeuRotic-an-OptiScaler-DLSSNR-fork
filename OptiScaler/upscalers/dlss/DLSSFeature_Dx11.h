#pragma once
#include <upscalers/IFeature_Dx11.h>
#include "DLSSFeature.h"
#include <dlssnr/DlssNr_Dx11.h>
#include <string>

class DLSSFeatureDx11 : public DLSSFeature, public IFeature_Dx11
{
  private:
    DlssNr::NativeDx11::Feature _nrDx11;
  protected:
  public:
    bool InitInternal(ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters) override;

    feature_version Version() override { return DLSSFeature::Version(); }
    Upscaler GetUpscalerType() const final { return DLSSFeature::GetUpscalerType(); }
    API Api() const override { return IFeature_Dx11::Api(); }
    bool CallsUpscalerEndByItself() override { return IFeature_Dx11::CallsUpscalerEndByItself(); }

    bool IsWithDx12() override { return false; }

    DLSSFeatureDx11(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);
    ~DLSSFeatureDx11();
};
