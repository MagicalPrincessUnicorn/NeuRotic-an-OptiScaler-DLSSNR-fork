#pragma once
#include "FgLifecycle.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>

namespace DlssNr::DredDiagnostics
{
inline bool Enabled() noexcept
{ static const bool enabled = FgLifecycle::Enabled() && FgLifecycle::OptIn("NEUROTIC_DRED"); return enabled; }
inline std::atomic<bool> configured {false};
inline std::once_flag configuration;
// Called before the hook's capability probing, which can itself create a device.
inline void Configure(decltype(&D3D12GetDebugInterface) getDebugInterface) noexcept
{
    if (!Enabled()) return;
    try { std::call_once(configuration, [&] {
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
        const HRESULT result = getDebugInterface ? getDebugInterface(IID_PPV_ARGS(&settings)) : E_NOINTERFACE;
        if (SUCCEEDED(result) && settings)
        {
            settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            configured.store(true);
        }
        NR_FG_EVENT("dred-config", "configured={} result={} boundary=before-hook-device-creation "
            "earlier-unobserved-devices=unknown", configured.load(), static_cast<uint32_t>(result));
    }); } catch (...) { }
}
inline void Name(ID3D12Object* object, const wchar_t* name) noexcept
{ if (Enabled() && object) object->SetName(name); }
inline void Device(IUnknown* object, HRESULT creationResult) noexcept
{
    if (!Enabled()) return;
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    const auto result = SUCCEEDED(creationResult) && object ? object->QueryInterface(IID_PPV_ARGS(&dred)) : E_NOINTERFACE;
    NR_FG_EVENT("dred-device", "device={:p} creationResult={} configured={} interfaceResult={} available={}",
        static_cast<void*>(object), static_cast<uint32_t>(creationResult), configured.load(),
        static_cast<uint32_t>(result), SUCCEEDED(result));
}
inline std::atomic<bool> reported {false};
inline std::string Narrow(const wchar_t* name)
{
    if (!name) return "unknown";
    // Names originate from the runtime; cap reads/output and escape control bytes.
    std::string result;
    for (unsigned i = 0; i < 96 && name[i]; ++i)
        result.push_back(name[i] >= 32 && name[i] < 127 ? static_cast<char>(name[i]) : '?');
    return result;
}
inline void Collect(ID3D12Device* device, HRESULT reason) noexcept
{
    if (!Enabled() || !device || SUCCEEDED(reason) || reported.exchange(true)) return;
    try
    {
        FgLifecycle::Journal report(L"DRED.log");
        NR_FG_EVENT("dred-fault", "device={:p} reason={} reportAvailable={}", static_cast<void*>(device),
            static_cast<uint32_t>(reason), report.file != INVALID_HANDLE_VALUE);
        report.Write("fault", "device={:p} reason={} configured={} scope=first-device-removal "
            "maxNodes=64 maxOperationsPerNode=32 maxAllocationsPerList=128",
            static_cast<void*>(device), static_cast<uint32_t>(reason), configured.load());
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
        const auto query = device->QueryInterface(IID_PPV_ARGS(&dred));
        if (FAILED(query)) { report.Write("unavailable", "interfaceResult={}", static_cast<uint32_t>(query)); }
        else
        {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs {};
            const auto result = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
            report.Write("breadcrumbs", "result={}", static_cast<uint32_t>(result));
            if (SUCCEEDED(result))
            {
                auto* node = breadcrumbs.pHeadAutoBreadcrumbNode;
                unsigned n = 0;
                for (; node && n < 64; node = node->pNext, ++n)
                {
                    const UINT completed = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                    report.Write("breadcrumb-node", "node={} list={:p} queue={:p} listName={} queueName={} "
                        "completed={} count={} completionKnown={}", n, static_cast<void*>(node->pCommandList),
                        static_cast<void*>(node->pCommandQueue), Narrow(node->pCommandListDebugNameW),
                        Narrow(node->pCommandQueueDebugNameW), completed, node->BreadcrumbCount,
                        node->pLastBreadcrumbValue != nullptr);
                    const UINT begin = completed > 8 ? completed - 8 : 0;
                    for (UINT i = begin; node->pCommandHistory && i < node->BreadcrumbCount && i - begin < 32; ++i)
                        report.Write("breadcrumb-op", "node={} index={} op={}", n, i,
                            static_cast<unsigned>(node->pCommandHistory[i]));
                    for (UINT i = 0; node->pBreadcrumbContexts && i < node->BreadcrumbContextsCount && i < 8; ++i)
                        report.Write("breadcrumb-context", "node={} index={} text={}", n,
                            node->pBreadcrumbContexts[i].BreadcrumbIndex,
                            Narrow(node->pBreadcrumbContexts[i].pContextString));
                }
                report.Write("breadcrumbs-end", "nodes={} truncated={}", n, node != nullptr);
            }
            D3D12_DRED_PAGE_FAULT_OUTPUT1 fault {};
            const auto faultResult = dred->GetPageFaultAllocationOutput1(&fault);
            report.Write("page-fault", "result={} address={}", static_cast<uint32_t>(faultResult), fault.PageFaultVA);
            if (SUCCEEDED(faultResult))
            {
                auto allocations = [&](const char* kind, const D3D12_DRED_ALLOCATION_NODE1* node) {
                    unsigned n = 0;
                    for (; node && n < 128; node = node->pNext, ++n)
                        report.Write(kind, "index={} type={} name={} object={:p}", n,
                            static_cast<unsigned>(node->AllocationType), Narrow(node->ObjectNameW),
                            static_cast<const void*>(node->pObject));
                    report.Write("allocations-end", "list={} nodes={} truncated={}", kind, n, node != nullptr);
                };
                allocations("existing-allocation", fault.pHeadExistingAllocationNode);
                allocations("recently-freed-allocation", fault.pHeadRecentFreedAllocationNode);
            }
        }
        report.Write("report-end", "complete=true localization=approximate gpuWaitsAdded=false");
        if (report.file != INVALID_HANDLE_VALUE) { FlushFileBuffers(report.file); CloseHandle(report.file); }
    }
    catch (...) { NR_FG_EVENT("dred-unavailable", "reason=report-exception"); }
}
}
