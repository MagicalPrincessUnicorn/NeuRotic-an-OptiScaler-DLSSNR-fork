// W03 P0 observer calibration. This executable never loads an NGX provider.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT Width=32, Height=16;
void Require(HRESULT result,const char* stage)
{if(FAILED(result))throw std::runtime_error(std::string(stage)+" HRESULT="+std::to_string(static_cast<unsigned>(result)));}
std::string Json(const std::string& value)
{
    std::string out="\"";
    for(char c:value){if(c=='\\'||c=='\"')out+='\\';if(c=='\n'){out+="\\n";continue;}out+=c;}
    return out+'\"';
}
std::uint64_t Hash(const std::vector<std::uint8_t>& bytes)
{
    std::uint64_t hash=14695981039346656037ull;
    for(auto value:bytes){hash^=value;hash*=1099511628211ull;}
    return hash;
}
std::vector<std::uint8_t> Pattern()
{
    std::vector<std::uint8_t> data(Width*Height*4);
    for(UINT y=0;y<Height;++y)for(UINT x=0;x<Width;++x)
    {
        const auto i=(y*Width+x)*4;
        data[i]=static_cast<std::uint8_t>(x*7+3);
        data[i+1]=static_cast<std::uint8_t>(y*11+5);
        data[i+2]=static_cast<std::uint8_t>((x^y)+17);
        data[i+3]=255;
    }
    return data;
}
struct CaseResult
{
    struct Event
    {
        std::string kind,api;
        UINT queueId=0,listId=0,fenceId=0;
        std::uint64_t queueAddress=0,listAddress=0,fenceAddress=0,fenceValue=0;
    };
    std::string scenario,adapter,adapterName,status="INCONCLUSIVE",stage="setup";
    std::uint64_t inputHash=0,outputBeforeHash=0,outputHash=0,adapterLuid=0;
    bool writeRecorded=false,firstSubmitted=false,firstSignaled=false;
    bool secondWait=false,secondSubmitted=false,secondSignaled=false;
    bool cpuMarker=false,readbackObserved=false,oracleMatched=false,outputChanged=false;
    bool simulatedMissingEvent=false,simulatedUnknownFence=false;
    std::vector<Event> events;
    std::string error;
};
template<class T> std::uint64_t Address(T* value)
{return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));}
void Emit(CaseResult& r,const std::string& kind,const std::string& api,
    UINT queueId=0,ID3D12CommandQueue* queue=nullptr,UINT listId=0,
    ID3D12GraphicsCommandList* list=nullptr,UINT fenceId=0,
    ID3D12Fence* fence=nullptr,UINT64 value=0)
{
    r.events.push_back({kind,api,queueId,listId,fenceId,Address(queue),
        Address(list),Address(fence),value});
}
std::string Manifest(const CaseResult& r)
{
    std::string events="[";
    for(const auto& e:r.events)
    {
        if(events.size()>1)events+=",";
        events+="\n    {\"kind\": "+Json(e.kind)+", \"api\": "+Json(e.api)+
            ", \"queue_id\": "+std::to_string(e.queueId)+
            ", \"queue_address\": "+std::to_string(e.queueAddress)+
            ", \"list_id\": "+std::to_string(e.listId)+
            ", \"list_address\": "+std::to_string(e.listAddress)+
            ", \"fence_id\": "+std::to_string(e.fenceId)+
            ", \"fence_address\": "+std::to_string(e.fenceAddress)+
            ", \"fence_value\": "+std::to_string(e.fenceValue)+"}";
    }
    if(events.size()>1)events+='\n';
    events+=']';
    return "{\n  \"schema_version\": 2,\n  \"layer\": \"controlled_d3d12\",\n"
        "  \"instrumentation\": \"host_self_journal_not_external_trace\",\n"
        "  \"provider\": \"NOT_LOADED\",\n  \"scenario\": "+Json(r.scenario)+",\n"
        "  \"adapter\": "+Json(r.adapter)+",\n  \"adapter_name\": "+Json(r.adapterName)+",\n"
        "  \"adapter_luid\": "+std::to_string(r.adapterLuid)+",\n"
        "  \"process_id\": "+std::to_string(GetCurrentProcessId())+",\n"
        "  \"recording_incarnation\": \"one-owned-list-per-queue\",\n"
        "  \"resource_profile\": {\"format\": \"R8G8B8A8_UNORM\", \"width\": 32, \"height\": 16, "
        "\"mip\": 0, \"array_slice\": 0, \"rectangle\": [0, 0, 32, 16]},\n"
        "  \"hash_algorithm\": \"FNV1a64\",\n  \"input_hash\": "+std::to_string(r.inputHash)+",\n"
        "  \"output_before_hash\": "+std::to_string(r.outputBeforeHash)+",\n"
        "  \"output_hash\": "+std::to_string(r.outputHash)+",\n"
        "  \"output_changed\": "+std::string(r.outputChanged?"true":"false")+",\n"
        "  \"events\": "+events+",\n"
        "  \"write_recorded\": "+std::string(r.writeRecorded?"true":"false")+",\n"
        "  \"first_submitted\": "+std::string(r.firstSubmitted?"true":"false")+",\n"
        "  \"first_signaled\": "+std::string(r.firstSignaled?"true":"false")+",\n"
        "  \"second_wait\": "+std::string(r.secondWait?"true":"false")+",\n"
        "  \"second_submitted\": "+std::string(r.secondSubmitted?"true":"false")+",\n"
        "  \"second_signaled\": "+std::string(r.secondSignaled?"true":"false")+",\n"
        "  \"cpu_marker\": "+std::string(r.cpuMarker?"true":"false")+",\n"
        "  \"readback_observed\": "+std::string(r.readbackObserved?"true":"false")+",\n"
        "  \"oracle_matched\": "+std::string(r.oracleMatched?"true":"false")+",\n"
        "  \"simulated_missing_event\": "+std::string(r.simulatedMissingEvent?"true":"false")+",\n"
        "  \"simulated_unknown_fence\": "+std::string(r.simulatedUnknownFence?"true":"false")+",\n"
        "  \"status\": "+Json(r.status)+",\n  \"stage\": "+Json(r.stage)+",\n"
        "  \"error\": "+Json(r.error)+"\n}\n";
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES state,UINT64 bytes)
{
    D3D12_HEAP_PROPERTIES heap{};heap.Type=heapType;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> result;
    Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,
        IID_PPV_ARGS(&result)),"CreateBuffer");
    return result;
}
void Wait(ID3D12Fence* fence,UINT64 value,ID3D12Device* device)
{
    if(fence->GetCompletedValue()==UINT64_MAX||FAILED(device->GetDeviceRemovedReason()))
        throw std::runtime_error("device removed");
    if(fence->GetCompletedValue()>=value)return;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event)throw std::runtime_error("CreateEvent failed");
    const HRESULT hr=fence->SetEventOnCompletion(value,event);
    const DWORD waited=SUCCEEDED(hr)?WaitForSingleObject(event,10000):WAIT_FAILED;
    CloseHandle(event);
    Require(hr,"SetEventOnCompletion");
    if(waited!=WAIT_OBJECT_0||fence->GetCompletedValue()==UINT64_MAX||
       FAILED(device->GetDeviceRemovedReason()))throw std::runtime_error("completion unknown or device removed");
}
CaseResult Run(const std::string& adapterKind,const std::string& scenario)
{
    CaseResult r;r.adapter=adapterKind;r.scenario=scenario;
    const auto expected=Pattern();r.inputHash=Hash(expected);
    if(scenario=="no-op")
    {r.status="FAIL";r.stage="missing_recorded_write";return r;}
    if(scenario=="mock-entry-refusal")
    {r.status="FAIL";r.stage="provider_entry_refused_mock";return r;}
    if(scenario=="mock-release-failure")
    {r.status="INCONCLUSIVE";r.stage="release_failed_mock";return r;}
    const bool cross=scenario=="cross-queue";
    if(!cross&&scenario!="one-queue"&&scenario!="missing-event"&&scenario!="unknown-fence"&&
       scenario!="wrong-target")throw std::runtime_error("unknown scenario");
    ComPtr<IDXGIFactory4> factory;Require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> adapter;
    if(adapterKind=="warp")Require(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"EnumWarpAdapter");
    else if(adapterKind=="physical")
    {
        for(UINT i=0;;++i)
        {
            ComPtr<IDXGIAdapter1> candidate;
            const HRESULT enumerated=factory->EnumAdapters1(i,&candidate);
            if(enumerated==DXGI_ERROR_NOT_FOUND)break;
            Require(enumerated,"EnumAdapters1");
            DXGI_ADAPTER_DESC1 desc{};Require(candidate->GetDesc1(&desc),"GetDesc1");
            if(desc.VendorId==0x10de && !(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))
            {adapter=candidate;break;}
        }
        if(!adapter)throw std::runtime_error("NVIDIA physical adapter unavailable");
    }
    else throw std::runtime_error("adapter must be warp or physical");
    DXGI_ADAPTER_DESC1 adapterDesc{};Require(adapter->GetDesc1(&adapterDesc),"GetDesc1");
    for(wchar_t c:adapterDesc.Description){if(!c)break;r.adapterName+=c>=32&&c<127?static_cast<char>(c):'?';}
    r.adapterLuid=(static_cast<UINT64>(static_cast<UINT>(adapterDesc.AdapterLuid.HighPart))<<32)|
        adapterDesc.AdapterLuid.LowPart;
    ComPtr<ID3D12Device> device;
    Require(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"D3D12CreateDevice");
    D3D12_COMMAND_QUEUE_DESC qdesc{};qdesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> firstQueue,secondQueue;
    Require(device->CreateCommandQueue(&qdesc,IID_PPV_ARGS(&firstQueue)),"CreateCommandQueue first");
    if(cross)Require(device->CreateCommandQueue(&qdesc,IID_PPV_ARGS(&secondQueue)),"CreateCommandQueue second");
    D3D12_RESOURCE_DESC texDesc{};texDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width=Width;texDesc.Height=Height;texDesc.DepthOrArraySize=1;texDesc.MipLevels=1;
    texDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texDesc.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES defaultHeap{};defaultHeap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    Require(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)),"CreateTexture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;
    device->GetCopyableFootprints(&texDesc,0,1,0,&footprint,&rows,&rowBytes,&total);
    if(rows!=Height||rowBytes!=Width*4||total==0)throw std::runtime_error("unexpected texture footprint");
    auto upload=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,total);
    auto zeroUpload=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,total);
    auto beforeReadback=Buffer(device.Get(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,total);
    auto readback=Buffer(device.Get(),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,total);
    void* mapped=nullptr;Require(upload->Map(0,nullptr,&mapped),"Map upload");
    std::memset(mapped,0,static_cast<size_t>(total));
    for(UINT y=0;y<Height;++y)
        std::memcpy(static_cast<std::uint8_t*>(mapped)+footprint.Footprint.RowPitch*y,
            expected.data()+Width*4*y,Width*4);
    upload->Unmap(0,nullptr);
    Require(zeroUpload->Map(0,nullptr,&mapped),"Map zero upload");
    std::memset(mapped,0,static_cast<size_t>(total));
    zeroUpload->Unmap(0,nullptr);
    ComPtr<ID3D12CommandAllocator> firstAllocator,secondAllocator;
    ComPtr<ID3D12GraphicsCommandList> firstList,secondList;
    Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&firstAllocator)),"CreateAllocator first");
    Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,firstAllocator.Get(),nullptr,
        IID_PPV_ARGS(&firstList)),"CreateList first");
    if(cross)
    {
        Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&secondAllocator)),"CreateAllocator second");
        Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,secondAllocator.Get(),nullptr,
            IID_PPV_ARGS(&secondList)),"CreateList second");
    }
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION zeroSrc=src;zeroSrc.pResource=zeroUpload.Get();
    D3D12_TEXTURE_COPY_LOCATION tex{};tex.pResource=texture.Get();tex.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    tex.SubresourceIndex=0;
    D3D12_TEXTURE_COPY_LOCATION beforeDst{};beforeDst.pResource=beforeReadback.Get();
    beforeDst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;beforeDst.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint=footprint;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=texture.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    firstList->CopyTextureRegion(&tex,0,0,0,&zeroSrc,nullptr);
    firstList->ResourceBarrier(1,&barrier);
    firstList->CopyTextureRegion(&beforeDst,0,0,0,&tex,nullptr);
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
    firstList->ResourceBarrier(1,&barrier);
    firstList->CopyTextureRegion(&tex,0,0,0,&src,nullptr);r.writeRecorded=true;
    Emit(r,"command_list_recording","CopyTextureRegion",1,firstQueue.Get(),1,firstList.Get());
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    firstList->ResourceBarrier(1,&barrier);
    if(cross)
    {
        secondList->CopyTextureRegion(&dst,0,0,0,&tex,nullptr);
        Emit(r,"command_list_recording","CopyTextureRegion",2,secondQueue.Get(),2,secondList.Get());
    }
    else firstList->CopyTextureRegion(&dst,0,0,0,&tex,nullptr);
    Require(firstList->Close(),"Close first list");
    Emit(r,"command_list_close","Close",1,firstQueue.Get(),1,firstList.Get());
    if(cross)
    {
        Require(secondList->Close(),"Close second list");
        Emit(r,"command_list_close","Close",2,secondQueue.Get(),2,secondList.Get());
    }
    ComPtr<ID3D12Fence> firstFence,secondFence;
    Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&firstFence)),"CreateFence first");
    if(cross)Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&secondFence)),"CreateFence second");
    ID3D12CommandList* first[]={firstList.Get()};firstQueue->ExecuteCommandLists(1,first);r.firstSubmitted=true;
    Emit(r,"queue_submit","ExecuteCommandLists",1,firstQueue.Get(),1,firstList.Get());
    Require(firstQueue->Signal(firstFence.Get(),1),"Signal first queue");r.firstSignaled=true;
    Emit(r,"fence_signal","Signal",1,firstQueue.Get(),1,firstList.Get(),1,firstFence.Get(),1);
    if(cross)
    {
        Require(secondQueue->Wait(firstFence.Get(),1),"Wait first queue");r.secondWait=true;
        Emit(r,"second_queue_dependency","Wait",2,secondQueue.Get(),2,secondList.Get(),1,firstFence.Get(),1);
        ID3D12CommandList* second[]={secondList.Get()};secondQueue->ExecuteCommandLists(1,second);r.secondSubmitted=true;
        Emit(r,"queue_submit","ExecuteCommandLists",2,secondQueue.Get(),2,secondList.Get());
        Require(secondQueue->Signal(secondFence.Get(),1),"Signal second queue");r.secondSignaled=true;
        Emit(r,"fence_signal","Signal",2,secondQueue.Get(),2,secondList.Get(),2,secondFence.Get(),1);
    }
    std::thread marker([&]{Sleep(2);OutputDebugStringA("W03 P0 delayed CPU marker\n");r.cpuMarker=true;});
    try{Wait(cross?secondFence.Get():firstFence.Get(),1,device.Get());}
    catch(...){marker.join();throw;}
    if(firstFence->GetCompletedValue()<1)throw std::runtime_error("first fence completion unknown");
    Emit(r,"fence_completion","GetCompletedValue",1,firstQueue.Get(),1,firstList.Get(),1,firstFence.Get(),1);
    if(cross)
        Emit(r,"fence_completion","GetCompletedValue",2,secondQueue.Get(),2,secondList.Get(),2,secondFence.Get(),1);
    marker.join();
    D3D12_RANGE range{0,static_cast<SIZE_T>(total)};
    Require(beforeReadback->Map(0,&range,&mapped),"Map before readback");
    std::vector<std::uint8_t> before(expected.size());
    for(UINT y=0;y<Height;++y)
        std::memcpy(before.data()+Width*4*y,
            static_cast<const std::uint8_t*>(mapped)+footprint.Footprint.RowPitch*y,Width*4);
    D3D12_RANGE none{0,0};beforeReadback->Unmap(0,&none);
    Require(readback->Map(0,&range,&mapped),"Map readback");
    std::vector<std::uint8_t> observed(expected.size());
    for(UINT y=0;y<Height;++y)
        std::memcpy(observed.data()+Width*4*y,
            static_cast<const std::uint8_t*>(mapped)+footprint.Footprint.RowPitch*y,Width*4);
    readback->Unmap(0,&none);
    r.readbackObserved=true;r.outputHash=Hash(observed);r.oracleMatched=observed==expected;
    r.outputBeforeHash=Hash(before);r.outputChanged=before!=observed;
    if(r.outputChanged&&std::all_of(before.begin(),before.end(),[](std::uint8_t byte){return byte==0;}))
        Emit(r,"resource_output_change","Map/readback comparison",cross?2u:1u,
            cross?secondQueue.Get():firstQueue.Get(),cross?2u:1u,
            cross?secondList.Get():firstList.Get());
    r.simulatedMissingEvent=scenario=="missing-event";
    r.simulatedUnknownFence=scenario=="unknown-fence";
    if(r.simulatedMissingEvent)
        r.events.erase(std::remove_if(r.events.begin(),r.events.end(),
            [](const CaseResult::Event& event){return event.kind=="command_list_recording";}),r.events.end());
    if(r.simulatedUnknownFence)
        r.events.erase(std::remove_if(r.events.begin(),r.events.end(),
            [](const CaseResult::Event& event){return event.kind=="fence_completion";}),r.events.end());
    if(r.simulatedMissingEvent||r.simulatedUnknownFence)
    {r.status="INCONCLUSIVE";r.stage=r.simulatedMissingEvent?"missing_write_journal_event":"unknown_fence_observation";}
    else if(scenario=="wrong-target")
    {r.status="FAIL";r.stage="target_identity_mismatch";}
    else if(r.oracleMatched&&r.outputChanged&&r.writeRecorded&&r.firstSubmitted&&r.firstSignaled&&
        (!cross||(r.secondWait&&r.secondSubmitted&&r.secondSignaled))&&r.cpuMarker)
    {r.status="PASS_FOR_DECLARED_CASE";r.stage="readback_verified";}
    else{r.status="FAIL";r.stage="readback_or_journal_mismatch";}
    return r;
}
}
int main(int argc,char** argv)
{
    std::string adapter,scenario,output;
    for(int i=1;i<argc;++i)
    {
        if(i+1>=argc){std::cerr<<"missing argument value\n";return 2;}
        const std::string key=argv[i];const std::string value=argv[++i];
        if(key=="--adapter")adapter=value;
        else if(key=="--case")scenario=value;
        else if(key=="--output")output=value;
        else{std::cerr<<"unknown argument\n";return 2;}
    }
    if(adapter.empty()||scenario.empty()||output.empty()){std::cerr<<"--adapter, --case and --output required\n";return 2;}
    CaseResult result;result.adapter=adapter;result.scenario=scenario;
    try{result=Run(adapter,scenario);}
    catch(const std::exception& error)
    {
        result.error=error.what();
        if(adapter=="physical"&&result.error=="NVIDIA physical adapter unavailable")
        {result.status="NOT_RUN";result.stage="adapter_unavailable";}
        else{result.status="INCONCLUSIVE";result.stage="host_or_device_failure";}
    }
    std::ofstream file(output,std::ios::binary|std::ios::trunc);
    if(!file){std::cerr<<"cannot write manifest\n";return 2;}
    file<<Manifest(result);file.close();
    std::cout<<result.scenario<<" "<<result.status<<" "<<result.stage<<"\n";
    return result.status=="PASS_FOR_DECLARED_CASE"?0:1;
}
