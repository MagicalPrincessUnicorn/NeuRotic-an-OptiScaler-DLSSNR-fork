#pragma once
#include <dxgi.h>
#include <atomic>

namespace DxgiSwapchainExtent
{
// Zero is automatic for the legacy/HWND APIs. It is not a measured small extent.
// Explicit-small bypass remains a conservative auxiliary-surface policy only.
inline bool BypassSmallExplicitWindowExtent(UINT width,UINT height) noexcept
{
    return width!=0&&height!=0&&(width<100||height<100);
}
struct Observation
{
    HRESULT creationResult=E_FAIL,descriptionResult=E_PENDING;
    DXGI_SWAP_CHAIN_DESC actual{};
    bool DescriptionKnown() const noexcept {return SUCCEEDED(creationResult)&&SUCCEEDED(descriptionResult);}
    bool PositiveExtent() const noexcept {return DescriptionKnown()&&actual.BufferDesc.Width&&actual.BufferDesc.Height;}
};
inline Observation ObserveCreated(HRESULT result,IDXGISwapChain* chain)
{
    Observation observed;observed.creationResult=result;
    if(SUCCEEDED(result))observed.descriptionResult=chain?chain->GetDesc(&observed.actual):E_POINTER;
    return observed;
}
inline bool ApplyObservedDescription(const Observation& observed,DXGI_SWAP_CHAIN_DESC& description) noexcept
{
    if(!observed.PositiveExtent())return false;
    description=observed.actual;
    return true;
}
inline bool ApplyObservedExtent(const Observation& observed,DXGI_SWAP_CHAIN_DESC& description,float& width,float& height) noexcept
{
    if(!ApplyObservedDescription(observed,description))return false;
    width=static_cast<float>(observed.actual.BufferDesc.Width);
    height=static_cast<float>(observed.actual.BufferDesc.Height);return true;
}
inline HRESULT RequireMatchingBufferExtent(IDXGISwapChain* hidden,IDXGISwapChain* visible)
{
    const auto target=ObserveCreated(S_OK,visible),current=ObserveCreated(S_OK,hidden);
    if(FAILED(target.descriptionResult))return target.descriptionResult;
    if(FAILED(current.descriptionResult))return current.descriptionResult;
    return target.PositiveExtent()&&current.PositiveExtent()&&
        target.actual.BufferDesc.Width==current.actual.BufferDesc.Width&&
        target.actual.BufferDesc.Height==current.actual.BufferDesc.Height?S_OK:DXGI_ERROR_INVALID_CALL;
}
// Automatic axes on a hidden interop HWND resolve differently from the application's
// HWND. Before accepting the pair, match the hidden buffers to the actual visible
// buffers. Call only while the interop owner holds no buffers or outstanding copies.
inline HRESULT MatchHiddenBufferExtent(IDXGISwapChain* hidden,IDXGISwapChain* visible)
{
    const auto target=ObserveCreated(S_OK,visible),current=ObserveCreated(S_OK,hidden);
    if(FAILED(target.descriptionResult))return target.descriptionResult;
    if(FAILED(current.descriptionResult))return current.descriptionResult;
    if(!target.PositiveExtent()||!current.PositiveExtent())return DXGI_ERROR_INVALID_CALL;
    if(target.actual.BufferDesc.Width==current.actual.BufferDesc.Width&&
       target.actual.BufferDesc.Height==current.actual.BufferDesc.Height)return S_OK;
    const auto result=hidden->ResizeBuffers(0,target.actual.BufferDesc.Width,target.actual.BufferDesc.Height,
        DXGI_FORMAT_UNKNOWN,current.actual.Flags);
    if(FAILED(result))return result;
    const auto final=ObserveCreated(result,hidden);
    if(FAILED(final.descriptionResult))return final.descriptionResult;
    return final.PositiveExtent()&&final.actual.BufferDesc.Width==target.actual.BufferDesc.Width&&
        final.actual.BufferDesc.Height==target.actual.BufferDesc.Height?S_OK:DXGI_ERROR_INVALID_CALL;
}
// Diagnostic budgets never affect admission, creation, resizing or state refresh.
inline std::atomic<unsigned> creationReceipts{0},presentReceipts{0},pairRefusalReceipts{0};
inline bool TakeReceipt(std::atomic<unsigned>& budget) noexcept
{
    unsigned used=budget.load(std::memory_order_relaxed);
    while(used<64){if(budget.compare_exchange_weak(used,used+1,std::memory_order_relaxed))return true;}
    return false;
}
}
