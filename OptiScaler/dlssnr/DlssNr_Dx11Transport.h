#pragma once

#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <cstring>

// Private texture transport only. No NGX, global upscaler cache, or game-specific state.
// The caller owns the producer/consumer fences and must retire work before reuse/destruction.
namespace DlssNr::Dx11Transport
{
using Microsoft::WRL::ComPtr;
inline constexpr size_t SlotCount = 4;
inline constexpr UINT64 BudgetBytes = 1280ull * 1024 * 1024;
inline constexpr UINT64 OutputBudgetBytes = 512ull * 1024 * 1024;
inline bool SameObject(IUnknown* a, IUnknown* b)
{
    ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) &&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x == y;
}
inline bool SameAdapter(ID3D11Device* device11, ID3D12Device* device12)
{
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc {};
    if (!device11 || !device12 || FAILED(device11->QueryInterface(IID_PPV_ARGS(&dxgi))) ||
        FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc))) return false;
    const auto luid = device12->GetAdapterLuid();
    return luid.LowPart == desc.AdapterLuid.LowPart && luid.HighPart == desc.AdapterLuid.HighPart;
}
class ContextGuard
{
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> previous;
  public:
    ContextGuard(ID3D11DeviceContext* input, ID3DDeviceContextState* isolated)
    {
        if (input && isolated && input->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE &&
            SUCCEEDED(input->QueryInterface(IID_PPV_ARGS(&context))))
            context->SwapDeviceContextState(isolated, &previous);
    }
    ~ContextGuard() { if (previous) context->SwapDeviceContextState(previous.Get(), nullptr); }
    explicit operator bool() const { return previous != nullptr; }
    ContextGuard(const ContextGuard&) = delete;
    ContextGuard& operator=(const ContextGuard&) = delete;
};
class ContextLock
{
    ComPtr<ID3D11Multithread> multithread;
  public:
    explicit ContextLock(ID3D11DeviceContext* context)
    {
        if (context && SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&multithread))) &&
            multithread->GetMultithreadProtected()) multithread->Enter();
        else multithread.Reset();
    }
    ~ContextLock() { if (multithread) multithread->Leave(); }
};
struct DepthFormat { DXGI_FORMAT carrier, view; UINT bytes; };
inline DepthFormat DepthType(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT:
        return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, 4};
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, 4};
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return {DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, 8};
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM:
        return {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM, 2};
    case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_FLOAT: return {format, format, 2};
    case DXGI_FORMAT_R32_FLOAT: return {format, format, 4};
    default: return {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, 0};
    }
}
inline UINT MotionBytes(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_R16G16_FLOAT ? 4 : format == DXGI_FORMAT_R32G32_FLOAT ? 8 : 0;
}
inline bool SupportedShape(const D3D11_TEXTURE2D_DESC& d)
{
    return d.Width && d.Height && d.Width <= 8192 && d.Height <= 8192 && d.MipLevels &&
        d.ArraySize == 1 && d.SampleDesc.Count == 1;
}
struct Texture
{
    D3D11_TEXTURE2D_DESC sourceDesc {};
    ComPtr<ID3D11Texture2D> sourceCarrier, shared;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D12Resource> resource12;
    UINT64 bytes = 0;
    bool rawCopy = false, directSource = false;

    bool Matches(const D3D11_TEXTURE2D_DESC& d) const
    {
        return resource12 && d.Width == sourceDesc.Width && d.Height == sourceDesc.Height &&
            d.Format == sourceDesc.Format && d.BindFlags == sourceDesc.BindFlags && SupportedShape(d);
    }
    // Allocate transactionally; no old allocation is released unless its owner permitted reuse.
    static UINT64 RequiredBytes(const D3D11_TEXTURE2D_DESC& input, bool depth)
    {
        const auto format = DepthType(input.Format);
        const UINT mvBytes = MotionBytes(input.Format);
        if ((depth && !format.bytes) || (!depth && !mvBytes)) return 0;
        const UINT64 pixels = UINT64(input.Width) * input.Height;
        UINT64 result = pixels * (depth ? 4 : mvBytes * 2);
        // Admission is conservative: a resource carrying the SRV bind flag can
        // still reject the required typed view. Actual allocation may be smaller.
        if (!(depth && input.Format == DXGI_FORMAT_R32_FLOAT))
            result += pixels * (depth ? format.bytes : mvBytes);
        return result;
    }
    bool Prepare(ID3D11Device* device11, ID3D12Device* device12,
                 const D3D11_TEXTURE2D_DESC& input, bool depth, std::string& reason,
                 ID3D11Texture2D* source = nullptr)
    {
        if (!SupportedShape(input)) { reason = "guide texture shape unsupported"; return false; }
        const auto format = DepthType(input.Format);
        const UINT mvBytes = MotionBytes(input.Format);
        if ((depth && !format.bytes) || (!depth && !mvBytes))
        { reason = "guide format unsupported: " + std::to_string(input.Format); return false; }
        if (Matches(input)) return true;
        Texture next;
        next.sourceDesc = input;
        D3D11_TEXTURE2D_DESC d {};
        d.Width = input.Width; d.Height = input.Height; d.MipLevels = 1; d.ArraySize = 1;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        // Two-component float formats are not NT-shareable. Expand XY to XY00 in a
        // four-component carrier at the SAME precision, never RG32F -> RG16F.
        d.Format = depth ? DXGI_FORMAT_R32_FLOAT : mvBytes == 4 ?
            DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
        // Private carriers support shader conversion without modifying the game textures.
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        HRESULT hr = device11->CreateTexture2D(&d, nullptr, &next.shared);
        ComPtr<IDXGIResource1> sharedResource;
        HANDLE handle = nullptr;
        if (SUCCEEDED(hr)) hr = next.shared.As(&sharedResource);
        if (SUCCEEDED(hr)) hr = sharedResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ |
            DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle);
        if (SUCCEEDED(hr)) hr = device12->OpenSharedHandle(handle, IID_PPV_ARGS(&next.resource12));
        if (handle) CloseHandle(handle);
        if (FAILED(hr))
        { reason = "private guide sharing failed: " + std::to_string(static_cast<unsigned int>(hr)); return false; }
        next.bytes = UINT64(d.Width) * d.Height * (depth ? 4 : mvBytes * 2);
        next.rawCopy = depth && input.Format == DXGI_FORMAT_R32_FLOAT;
        if (!next.rawCopy)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC view {};
            view.Format = depth ? format.view : input.Format; view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            ComPtr<ID3D11ShaderResourceView> directView;
            if (source && (input.BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
                SUCCEEDED(device11->CreateShaderResourceView(source, &view, &directView)))
                next.directSource = true;
            else
            {
                d.MiscFlags = 0; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                d.Format = depth ? format.carrier : input.Format;
                hr = device11->CreateTexture2D(&d, nullptr, &next.sourceCarrier);
                if (SUCCEEDED(hr)) hr = device11->CreateShaderResourceView(next.sourceCarrier.Get(), &view, &next.srv);
                next.bytes += UINT64(d.Width) * d.Height * (depth ? format.bytes : mvBytes);
            }
            if (SUCCEEDED(hr)) hr = device11->CreateUnorderedAccessView(next.shared.Get(), nullptr, &next.uav);
            if (FAILED(hr)) { reason = "typed guide carrier/view creation failed"; return false; }
        }
        *this = std::move(next);
        return true;
    }
    bool Copy(ID3D11DeviceContext* context, ID3D11Texture2D* source,
              ID3D11ComputeShader* shader, std::string& reason)
    {
        if (rawCopy)
        {
            context->CopySubresourceRegion(shared.Get(), 0, 0, 0, 0, source, 0, nullptr);
            return true;
        }
        ComPtr<ID3D11ShaderResourceView> current;
        if (directSource)
        {
            const auto format = DepthType(sourceDesc.Format);
            D3D11_SHADER_RESOURCE_VIEW_DESC view {};
            view.Format = format.bytes ? format.view : sourceDesc.Format;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels = 1;
            ComPtr<ID3D11Device> device; context->GetDevice(&device);
            if (FAILED(device->CreateShaderResourceView(source, &view, &current)))
            { reason = "direct native guide view creation failed"; return false; }
        }
        else
        {
            context->CopySubresourceRegion(sourceCarrier.Get(), 0, 0, 0, 0, source, 0, nullptr);
            current = srv;
        }
        auto* input = current.Get(); auto* output = uav.Get();
        context->CSSetShader(shader, nullptr, 0);
        context->CSSetShaderResources(0, 1, &input);
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        context->Dispatch((sourceDesc.Width + 15) / 16, (sourceDesc.Height + 15) / 16, 1);
        input = nullptr; output = nullptr;
        context->CSSetShaderResources(0, 1, &input);
        context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
        return true;
    }
};
inline UINT FormatBytes(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            return 8;
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return 4;
        default:
            return 0;
    }
}
struct Output
{
    D3D11_TEXTURE2D_DESC sourceDesc {};
    ComPtr<ID3D11Texture2D> shared;
    ComPtr<ID3D12Resource> resource12;
    UINT64 bytes = 0;
    bool Matches(const D3D11_TEXTURE2D_DESC& d) const
    {
        return resource12 && d.Width == sourceDesc.Width && d.Height == sourceDesc.Height &&
            d.MipLevels == sourceDesc.MipLevels && d.Format == sourceDesc.Format &&
            d.BindFlags == sourceDesc.BindFlags && SupportedShape(d);
    }
    static UINT64 RequiredBytes(const D3D11_TEXTURE2D_DESC& d)
    {
        if (!SupportedShape(d)) return 0;
        UINT width = d.Width, height = d.Height;
        UINT64 pixels = 0;
        for (UINT mip = 0; mip < d.MipLevels; ++mip)
        {
            pixels += UINT64(width) * height;
            if (width == 1 && height == 1)
            {
                if (mip + 1 != d.MipLevels) return 0; // invalid mip chain
                break;
            }
            width = std::max(1u, width / 2); height = std::max(1u, height / 2);
        }
        return pixels * FormatBytes(d.Format);
    }
    bool Prepare(ID3D11Device* device11, ID3D12Device* device12,
                 const D3D11_TEXTURE2D_DESC& input, std::string& reason)
    {
        if (!RequiredBytes(input) ||
            !(input.BindFlags & D3D11_BIND_UNORDERED_ACCESS))
        { reason = "native output format/shape/UAV contract unsupported"; return false; }
        if (Matches(input)) return true;
        Output next; next.sourceDesc = input;
        auto d = input;
        d.Usage = D3D11_USAGE_DEFAULT; d.CPUAccessFlags = 0;
        d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        HRESULT hr = device11->CreateTexture2D(&d, nullptr, &next.shared);
        ComPtr<IDXGIResource1> sharedResource;
        HANDLE handle = nullptr;
        if (SUCCEEDED(hr)) hr = next.shared.As(&sharedResource);
        if (SUCCEEDED(hr)) hr = sharedResource->CreateSharedHandle(nullptr,
            DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle);
        if (SUCCEEDED(hr)) hr = device12->OpenSharedHandle(handle, IID_PPV_ARGS(&next.resource12));
        if (handle) CloseHandle(handle);
        if (FAILED(hr))
        { reason = "private native output sharing failed: " + std::to_string(static_cast<unsigned int>(hr)); return false; }
        next.bytes = RequiredBytes(input);
        *this = std::move(next);
        return true;
    }
};
class Converter
{
    ComPtr<ID3D11ComputeShader> depthShader, motionShader;
    ComPtr<ID3DDeviceContextState> isolated;
  public:
    bool Initialize(ID3D11Device1* device, std::string& reason)
    {
        if (depthShader && motionShader && isolated) return true;
        static constexpr char shader[] =
            "Texture2D<float> source : register(t0); RWTexture2D<float> dest : register(u0);"
            "[numthreads(16,16,1)] void main(uint3 p:SV_DispatchThreadID) {"
            "uint w,h; dest.GetDimensions(w,h); if(p.x<w && p.y<h) dest[p.xy]=source.Load(int3(p.xy,0)); }";
        ComPtr<ID3DBlob> code, errors;
        HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "NR DX11 depth extraction", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
        if (SUCCEEDED(hr)) hr = device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(),
                                                           nullptr, &depthShader);
        static constexpr char motion[] =
            "Texture2D<float2> source : register(t0); RWTexture2D<float4> dest : register(u0);"
            "[numthreads(16,16,1)] void main(uint3 p:SV_DispatchThreadID) {"
            "uint w,h; dest.GetDimensions(w,h); if(p.x<w && p.y<h) dest[p.xy]=float4(source.Load(int3(p.xy,0)),0,0); }";
        code.Reset(); errors.Reset();
        if (SUCCEEDED(hr)) hr = D3DCompile(motion, sizeof(motion)-1, "NR DX11 motion expansion", nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
        if (SUCCEEDED(hr)) hr = device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(),
                                                           nullptr, &motionShader);
        const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
        D3D_FEATURE_LEVEL selected {};
        if (SUCCEEDED(hr)) hr = device->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION,
            __uuidof(ID3D11Device), &selected, &isolated);
        if (FAILED(hr)) { reason = "DX11 depth shader or isolated context state unavailable"; return false; }
        return true;
    }
    bool Copy(ID3D11DeviceContext* context, Texture& depth, ID3D11Texture2D* depthSource,
              Texture& motion, ID3D11Texture2D* motionSource, std::string& reason)
    {
        ContextGuard guard(context, isolated.Get());
        if (!guard) { reason = "DX11 context state isolation unavailable"; return false; }
        return depth.Copy(context, depthSource, depthShader.Get(), reason) &&
            motion.Copy(context, motionSource, motionShader.Get(), reason);
    }
};
}
