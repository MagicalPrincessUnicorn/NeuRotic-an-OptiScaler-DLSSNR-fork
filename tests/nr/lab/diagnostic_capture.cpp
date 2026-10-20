#include "../../../tools/nr/lab/LabCommon.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "../../../tools/nr/lab/OwnedDiagnosticsCapture.h"
#include <iostream>
int main() {
    using namespace lab;using Microsoft::WRL::ComPtr;
    try {
        auto setup=owned_diagnostics::Setup();Require(setup["debug_layer_enabled"].get<bool>(),"DEBUG_LAYER_REQUIRED_FOR_COLLECTOR_TEST");
        ComPtr<IDXGIFactory4> factory;Require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"factory");
        ComPtr<IDXGIAdapter> adapter;Require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))),"WARP");
        ComPtr<ID3D12Device> device;Require(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"device");
        ComPtr<ID3D12InfoQueue> q;Require(SUCCEEDED(device.As(&q)),"info queue");
        q->ClearStorageFilter();q->ClearRetrievalFilter();q->ClearStoredMessages();
        Require(SUCCEEDED(q->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_WARNING,"collector warning sentinel")),"warning");
        Require(SUCCEEDED(q->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"collector error sentinel")),"error");
        auto r=owned_diagnostics::Messages(q.Get());
        Require(r["captured"]==2 && r["error_count"]==1 && r["warning_count"]==1,"MESSAGE_SEVERITY_OR_COUNT_LOST");
        Require(r["messages"][0]["description"].get<std::string>().find("collector warning sentinel")!=std::string::npos,"DESCRIPTION_LOST");
        Require(r["messages"][0]["id"].is_number_integer() && r["messages"][0]["category"].is_number_integer(),"MESSAGE_IDENTITY_LOST");
        D3D12_MESSAGE_SEVERITY severity=D3D12_MESSAGE_SEVERITY_WARNING;
        D3D12_INFO_QUEUE_FILTER filter{};filter.DenyList.NumSeverities=1;filter.DenyList.pSeverityList=&severity;
        Require(SUCCEEDED(q->AddRetrievalFilterEntries(&filter)),"retrieval filter");r=owned_diagnostics::Messages(q.Get());
        Require(r["stored"]==2 && r["visible"]==1 && !r["complete"].get<bool>(),"FILTERED_CAPTURE_CLAIMED_COMPLETE");
        q->ClearRetrievalFilter();Require(SUCCEEDED(q->SetMessageCountLimit(2)),"message limit");
        Require(SUCCEEDED(q->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO,"discard sentinel")),"discard message");r=owned_diagnostics::Messages(q.Get());
        Require(r["discarded"].get<uint64_t>()>0 && !r["complete"].get<bool>(),"DISCARDED_CAPTURE_CLAIMED_COMPLETE");
        q->ClearStoredMessages();Require(SUCCEEDED(q->SetMessageCountLimit(4096)),"restore limit");
        std::string large(70000,'x');Require(SUCCEEDED(q->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO,large.c_str())),"large message");
        r=owned_diagnostics::Messages(q.Get());Require(r["captured"]==0 && r["uncaptured"]==1 && r["read_failures"]==1 && !r["complete"].get<bool>(),"OVERSIZE_MESSAGE_NOT_ACCOUNTED");
        q->ClearStoredMessages();std::string medium(50000,'y');
        for(unsigned i=0;i<50;++i)Require(SUCCEEDED(q->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_INFO,medium.c_str())),"budget message");
        r=owned_diagnostics::Messages(q.Get());Require(r["captured"].get<uint64_t>()<50 && r["uncaptured"].get<uint64_t>()>0 && r["captured_storage_bytes"].get<uint64_t>()<=owned_diagnostics::ByteLimit && !r["complete"].get<bool>(),"BYTE_BUDGET_NOT_ACCOUNTED");
        Require(SUCCEEDED(q->AddStorageFilterEntries(&filter)),"storage filter");
        Require(SUCCEEDED(q->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_WARNING,"denied sentinel")),"denied message");
        r=owned_diagnostics::Messages(q.Get());Require(r["storage_denied"].get<uint64_t>()>0 && !r["complete"].get<bool>(),"DENIED_CAPTURE_CLAIMED_COMPLETE");
        auto absent=owned_diagnostics::Messages(nullptr);Require(!absent["available"].get<bool>() && !absent["complete"].get<bool>(),"MISSING_QUEUE_CLAIMED_COMPLETE");
        std::cout<<"PASS: severity/ID/text; retrieval filtering; discarded messages; oversized message; byte budget; storage filtering; unavailable queue (7 groups)\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
