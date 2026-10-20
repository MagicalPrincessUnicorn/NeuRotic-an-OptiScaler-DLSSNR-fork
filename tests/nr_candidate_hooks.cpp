#include "nr/diagnostics/candidate/CreateValue.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using namespace DlssNr::CandidateObserver;
static bool _skipCommitedResource=false,skipPlacedResource=false;
struct Option {bool value=false;bool value_or_default()const{return value;}};
struct Config {Option UESpoofIntelAtomics64;static Config* Instance(){static Config c;return &c;}};
// Only the alternate-provider branch condition is controlled by this host fixture.
struct UE_D3D12_RESOURCE_DESC {static constexpr bool bRequires64BitAtomicSupport=true;};
#define LOG_DEBUG(...) ((void)0)
static unsigned originals=0,scans=0,alternates=0;
static HRESULT hostResult=S_OK;
static D3D12_RESOURCE_DESC* expectedDesc=nullptr;
static void** expectedOutput=nullptr;
static ID3D12Device* expectedDevice=reinterpret_cast<ID3D12Device*>(0x3456);
static ID3D12Heap* expectedHeap=reinterpret_cast<ID3D12Heap*>(0x4567);
static const D3D12_HEAP_PROPERTIES* expectedProps=nullptr;
static const D3D12_CLEAR_VALUE* expectedClear=nullptr;
static void* object=reinterpret_cast<void*>(0x2345);
static bool nestedAlternate=false;
static HRESULT hkCreateCommittedResource(ID3D12Device*,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,
 const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
static HRESULT originalCommitted(ID3D12Device* d,const D3D12_HEAP_PROPERTIES* props,D3D12_HEAP_FLAGS flags,
 const D3D12_RESOURCE_DESC* desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID iid,void** out) {
 assert(d==expectedDevice && props==expectedProps && flags==D3D12_HEAP_FLAG_NONE && desc==expectedDesc);
 assert(state==D3D12_RESOURCE_STATE_COMMON && clear==expectedClear && out==expectedOutput);
 (void)iid;++originals;if(hostResult==S_OK && out)*out=object;return hostResult;
}
static HRESULT originalPlaced(ID3D12Device* d,ID3D12Heap* heap,UINT64 offset,const D3D12_RESOURCE_DESC* desc,
 D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID iid,void** out) {
 assert(heap==expectedHeap && offset==4096);
 return originalCommitted(d,expectedProps,D3D12_HEAP_FLAG_NONE,desc,state,clear,iid,out);
}
static auto o_CreateCommittedResource=&originalCommitted;
static auto o_CreatePlacedResource=&originalPlaced;
namespace DlssNr::ExposureScan {void NoteResource(const D3D12_RESOURCE_DESC* desc,ID3D12Resource* resource) {
 assert(desc==expectedDesc);assert(resource==object);++scans;
}}
namespace IGDExtProxy {
static HRESULT CreateCommitedResource(const D3D12_HEAP_PROPERTIES* props,D3D12_HEAP_FLAGS flags,const D3D12_RESOURCE_DESC* desc,
 D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID iid,void** out) {
 ++alternates;
 if(nestedAlternate) {
   auto saved=expectedDesc;expectedDesc=const_cast<D3D12_RESOURCE_DESC*>(desc);
   auto value=hkCreateCommittedResource(expectedDevice,props,flags,desc,state,clear,iid,out);expectedDesc=saved;return value;
 }
 if(out)*out=object;return hostResult;
}
static HRESULT CreatePlacedResource(ID3D12Heap*,UINT64,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,
 const D3D12_CLEAR_VALUE*,REFIID,void** out) {++alternates;if(out)*out=object;return hostResult;}
}
#include "production-hooks.inc"
int main(int argc,char** argv) {
 assert(argc>=2);auto destination=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(destination);
 const std::string mode=argc>2?argv[2]:"normal";nestedAlternate=mode=="nested";
 D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=1920;desc.Height=1080;
 desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_D32_FLOAT;desc.SampleDesc.Count=1;
 desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;D3D12_HEAP_PROPERTIES props{};expectedProps=&props;expectedDesc=&desc;
 void* out=nullptr;expectedOutput=&out;
 auto committed=[&](REFIID iid){return hkCreateCommittedResource(expectedDevice,&props,D3D12_HEAP_FLAG_NONE,&desc,
   D3D12_RESOURCE_STATE_COMMON,nullptr,iid,expectedOutput);};
 assert(committed(__uuidof(ID3D12Resource))==S_OK);assert(originals==1 && scans==1 && out==object && !Enabled());
 SetEnvironmentVariableA("NEUROTIC_CANDIDATE_OBSERVER","1");
 SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION","dddddddddddddddddddddddddddddddd");
 SetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY",destination.c_str());StartCold(false);
 for(int i=0;i<1000 && !Enabled();i++)Sleep(1);assert(Enabled());ObserveCoverage(0x22);
 assert(committed(__uuidof(ID3D12Resource))==S_OK);
 assert(hkCreatePlacedResource(expectedDevice,expectedHeap,4096,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,
   __uuidof(ID3D12Resource),expectedOutput)==S_OK);
 hostResult=E_FAIL;expectedOutput=reinterpret_cast<void**>(1);assert(committed(__uuidof(ID3D12Resource))==E_FAIL);
 hostResult=S_FALSE;expectedOutput=nullptr;assert(committed(__uuidof(ID3D12Resource))==S_FALSE);
 hostResult=S_OK;expectedOutput=&out;assert(committed(__uuidof(IUnknown))==S_OK);
 Config::Instance()->UESpoofIntelAtomics64.value=true;assert(committed(__uuidof(ID3D12Resource))==S_OK);
 Config::Instance()->UESpoofIntelAtomics64.value=false;
 assert(originals==(nestedAlternate?7u:6u) && scans==(nestedAlternate?5u:4u) && alternates==1);
 if(mode=="stress") {
   const auto before=originals,beforeScan=scans;
   for(unsigned repeat=0;repeat<10000;repeat++)assert(committed(__uuidof(ID3D12Resource))==S_OK);
   assert(originals==before+10000 && scans==beforeScan+10000);
 }
 RequestClose(Reason::Requested);
 const auto beforeClose=originals,beforeCloseScan=scans;
 assert(committed(__uuidof(ID3D12Resource))==S_OK && originals==beforeClose+1 && scans==beforeCloseScan+1);
 const auto trace=destination/L"candidate-dddddddddddddddddddddddddddddddd"/L"trace.jsonl";
 bool footer=false;for(int i=0;i<1000 && !footer;i++) {
   std::ifstream input(trace);std::string line;while(std::getline(input,line))footer|=line.find("\"type\":\"footer\"")!=std::string::npos;
   if(!footer)Sleep(2);
 }
 assert(footer);Snapshot s{};assert(TryRead(s)==ReadResult::Closed);
 if(mode=="stress")assert(s.count>=2 && s.count<=64 && !s.coverage.complete);
 else assert(s.count==2);
 if(nestedAlternate)assert(!s.coverage.complete);
 assert(s.coverage.familyStates==0x22); // read-only Preflight consumer has explicit coverage
 for(unsigned i=0;i<s.count;i++)assert(!s.candidates[i].mayAdmit && s.candidates[i].descriptorKnown);
 std::cout<<"production callback forwarding/outcomes -> native trace/snapshot passed\n";
}
