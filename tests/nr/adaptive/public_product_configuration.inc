// Included in the actual renderer TU under the fixture flag. These are normal
// public configuration options set before creation, never owner/readiness bits.
extern "C" __declspec(dllexport) int NR_AdaptiveFixtureConfigureNativePostSr()
{
    try{
        if(!DlssNr::AdaptivePublicFixture::Services::Forwarder()||
           NVNGXProxy::IsDx11Inited()||NVNGXProxy::IsDx12Inited()||NVNGXProxy::IsVulkanInited()||
           g_nr.feature||(g_nr.nativeCreation&&g_nr.nativeCreation->Attempted()))return 0;
        auto& config=*Config::Instance();
        config.DLSSEnabled.set_volatile_value(true);
        config.Dx12Upscaler.set_volatile_value(Upscaler::DLSS);
        config.DlssNrNativeProtocol=true;config.DlssNrRoute=0u;
        config.SetDlssNrRenderingMode(0);
        config.DlssNrPasses=1u;config.DlssNrSecondLayer=false;
        config.DlssNrWorkingScale=1.0f;config.DlssNrApplyModel=true;
        config.DlssNrUseProxy=false;config.DlssNrHoldFrame=false;
        config.DlssNrCompare=0u;config.DlssNrDebugView=0u;
        config.DlssNrReversibleMode=0u;
        config.DlssNrWhitePointSource=0u;config.DlssNrWhitePointScale=1.0f;
        config.DlssNrWhitePointFromExposure=false;
        config.DlssNrTransfer=1u;config.DlssNrTransferStrength=1.0f;
        config.DlssNrColourStrength=1.0f;config.DlssNrMaxRatio=2.0f;
        config.RestoreComputeSignature.set_volatile_value(true);
        config.RestoreGraphicSignature.set_volatile_value(false);
        config.SetDlssNrEnabled(true);
        return config.GetDlssNrRuntimeSnapshot().enabled?1:0;
    }catch(...){return 0;}
}
