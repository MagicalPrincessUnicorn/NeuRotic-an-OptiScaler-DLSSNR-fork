// Execute the shipped DXBC shader on WARP against a CPU layout oracle.
#define NOMINMAX
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#pragma warning(push)
#pragma warning(disable: 4324) // Production constant buffer intentionally has 256-byte alignment.
#include "../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
#pragma warning(pop)
#include "../OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.h"
using Microsoft::WRL::ComPtr;
static void Require(bool ok) { if (!ok) std::abort(); }
static void Check(HRESULT hr) { Require(SUCCEEDED(hr)); }
using Pixel = std::array<float, 4>;
constexpr UINT W = 32, H = 24;

static Pixel Sample(const std::vector<Pixel>& pixels, float u, float v)
{
    const float x = u * W - 0.5f, y = v * H - 0.5f;
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = x - ix, fy = y - iy;
    Pixel p {};
    for (int dy = 0; dy < 2; ++dy)
    for (int dx = 0; dx < 2; ++dx)
    {
        const auto& tap = pixels[std::clamp(iy + dy, 0, static_cast<int>(H) - 1) * W +
                                 std::clamp(ix + dx, 0, static_cast<int>(W) - 1)];
        for (int c = 0; c < 4; ++c)
            p[c] += tap[c] * (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
    }
    return p;
}

int main()
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                           D3D11_SDK_VERSION, &device, nullptr, &context));
    ComPtr<ID3D11ComputeShader> shader;
    Check(device->CreateComputeShader(DlssNr_cso, sizeof(DlssNr_cso), nullptr, &shader));
    context->CSSetShader(shader.Get(), nullptr, 0);
    std::vector<Pixel> original(W * H), completed(W * H);
    for (UINT y = 0; y < H; ++y)
    for (UINT x = 0; x < W; ++x)
    {
        original[y * W + x] = { x / 16.0f, y / 12.0f, 0.2f, 0.4f };
        completed[y * W + x] = { 2.0f + x / 8.0f, 3.0f + y / 6.0f, 5.0f, 0.8f };
    }
    D3D11_TEXTURE2D_DESC td {};
    td.Width = W; td.Height = H; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> base, finalImage, output, keep;
    D3D11_SUBRESOURCE_DATA upload { original.data(), W * sizeof(Pixel), 0 };
    Check(device->CreateTexture2D(&td, &upload, &base));
    upload.pSysMem = completed.data();
    Check(device->CreateTexture2D(&td, &upload, &finalImage));
    Check(device->CreateTexture2D(&td, nullptr, &output));
    Check(device->CreateTexture2D(&td, nullptr, &keep));
    ComPtr<ID3D11ShaderResourceView> baseView, finalView;
    ComPtr<ID3D11UnorderedAccessView> outputView, keepView;
    Check(device->CreateShaderResourceView(base.Get(), nullptr, &baseView));
    Check(device->CreateShaderResourceView(finalImage.Get(), nullptr, &finalView));
    Check(device->CreateUnorderedAccessView(output.Get(), nullptr, &outputView));
    Check(device->CreateUnorderedAccessView(keep.Get(), nullptr, &keepView));
    ID3D11ShaderResourceView* srvs[] = {finalView.Get(), finalView.Get(), baseView.Get(), baseView.Get(), baseView.Get()};
    ID3D11UnorderedAccessView* uavs[] = {outputView.Get(), keepView.Get()};
    context->CSSetShaderResources(0, 5, srvs); context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    D3D11_SAMPLER_DESC sd {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX; sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    ComPtr<ID3D11SamplerState> sampler;
    Check(device->CreateSamplerState(&sd, &sampler));
    auto* samplerPtr = sampler.Get(); context->CSSetSamplers(0, 1, &samplerPtr);
    D3D11_BUFFER_DESC bd {};
    bd.ByteWidth = sizeof(DlssNrConstants); bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    Check(device->CreateBuffer(&bd, nullptr, &constants));
    auto* cb = constants.Get(); context->CSSetConstantBuffers(0, 1, &cb);
    td.BindFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    Check(device->CreateTexture2D(&td, nullptr, &readback));
    unsigned int checks = 0;
    for (UINT mode : {0u, 1u, 2u})
    for (UINT swap : {0u, 1u})
    for (float zoom : {1.0f, 1.5f, 2.0f})
    for (float split : {0.25f, 0.5f, 0.75f})
    {
        DlssNrConstants p {};
        p.Mode = DlssNrMode_Present; p.Width = W; p.Height = H; p.WhitePoint = 1.0f;
        p.CompareMode = mode; p.CompareSwap = swap; p.CompareZoom = zoom; p.CompareSplit = split;
        context->UpdateSubresource(constants.Get(), 0, nullptr, &p, 0, 0);
        context->Dispatch((W + 7) / 8, (H + 7) / 8, 1);
        context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped {};
        Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        for (UINT y = 0; y < H; ++y)
        for (UINT x = 0; x < W; ++x)
        {
            const float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
            float su = u, sv = v;
            bool left = false, divider = false, outside = false;
            if (mode == 1)
            {
                left = (u < 0.5f) != (swap != 0);
                su = 0.5f + ((u < 0.5f ? u * 2 : (u - 0.5f) * 2) - 0.5f) / zoom;
                sv = 0.5f + (v - 0.5f) * 2 / zoom;
                outside = su < 0 || su > 1 || sv < 0 || sv > 1;
                divider = std::abs(u - 0.5f) < 1.0f / W;
            }
            if (mode == 2) { left = (u < split) != (swap != 0); divider = std::abs(u - split) < 1.0f / W; }
            auto expected = Sample(left ? original : completed, su, sv);
            if (outside) expected[0] = expected[1] = expected[2] = 0;
            if (divider) expected[0] = expected[1] = expected[2] = 1;
            auto actual = reinterpret_cast<const Pixel*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch)[x];
            for (int c = 0; c < 4; ++c)
            {
                if (std::abs(actual[c] - expected[c]) > 0.002f)
                {
                    std::fprintf(stderr, "display mismatch mode %u swap %u zoom %.1f at %u,%u channel %d: %f != %f\n",
                                 mode, swap, zoom, x, y, c, actual[c], expected[c]);
                    std::abort();
                }
            }
        }
        context->Unmap(readback.Get(), 0); ++checks;
    }
    std::printf("PASS: shipped multipass display shader, %u WARP cases (original/final, wipe, side-by-side, zoom, swap, HDR range, alpha).\n", checks);
}
