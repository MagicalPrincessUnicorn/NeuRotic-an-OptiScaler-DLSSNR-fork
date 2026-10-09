#pragma once

template <typename FeatureType> struct ContextData
{
    std::unique_ptr<FeatureType> feature;
    NVSDK_NGX_Parameter* createParams = nullptr;
    // D3D12 transitions own local parameter tables; other API owners retain
    // their existing createParams lifecycle until migrated.
    std::shared_ptr<NVSDK_NGX_Parameter> ownedCreateParams;
    int changeBackendCounter = 0;
    // D3D12 owned context obligation; Evaluate/backend replacement cannot clear
    // an unresolved explicit release or make a caller-handle overwrite safe.
    bool ownedReleaseUnresolved = false;
    // Logical release accepted; the complete owner and closed registry guard
    // remain retained until genuine recording retirement and provider success.
    bool ownedReleaseDeferred = false;
};
