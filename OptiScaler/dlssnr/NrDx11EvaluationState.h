#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstddef>

namespace DlssNr::NativeDx11
{
// Get methods retain the actual prior interfaces. Keep those references until
// restoration, including sparse/null bindings; UINT(-1) preserves UAV counters.
class EvaluationState
{
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11SamplerState* samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* cbvs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* uavs[D3D11_1_UAV_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* omUavs[D3D11_1_UAV_SLOT_COUNT] = {};
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11ComputeShader* shader = nullptr;
    ID3D11ClassInstance* instances[D3D11_SHADER_MAX_INTERFACES] = {};
    UINT instanceCount = D3D11_SHADER_MAX_INTERFACES, uavCount = D3D11_PS_CS_UAV_REGISTER_COUNT;
    template <typename T, std::size_t N> static void Release(T* (&values)[N]) noexcept
    { for (auto* value : values) if (value) value->Release(); }
  public:
    explicit EvaluationState(ID3D11DeviceContext* input) : context(input)
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        if (device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1) uavCount = D3D11_1_UAV_SLOT_COUNT;
        context->CSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, srvs);
        context->CSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers);
        context->CSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, cbvs);
        context->CSGetUnorderedAccessViews(0, uavCount, uavs);
        context->CSGetShader(&shader, instances, &instanceCount);
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
        context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, uavCount, omUavs);
    }
    ~EvaluationState() noexcept
    {
        UINT keep[D3D11_1_UAV_SLOT_COUNT];
        for (auto& value : keep) value = UINT(-1);
        ID3D11UnorderedAccessView* empty[D3D11_1_UAV_SLOT_COUNT] = {};
        context->CSSetUnorderedAccessViews(0, uavCount, empty, keep);
        context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, uavCount, empty, keep);
        UINT rtvCount = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        while (rtvCount && !rtvs[rtvCount - 1]) --rtvCount;
        context->OMSetRenderTargetsAndUnorderedAccessViews(rtvCount, rtvs, dsv,
            rtvCount, uavCount - rtvCount, omUavs + rtvCount, keep + rtvCount);
        context->CSSetShader(shader, instances, instanceCount);
        context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, srvs);
        context->CSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers);
        context->CSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, cbvs);
        context->CSSetUnorderedAccessViews(0, uavCount, uavs, keep);
        Release(srvs); Release(samplers); Release(cbvs); Release(uavs); Release(omUavs);
        Release(rtvs); Release(instances);
        if (dsv) dsv->Release();
        if (shader) shader->Release();
    }
    EvaluationState(const EvaluationState&) = delete;
    EvaluationState& operator=(const EvaluationState&) = delete;
};
}
