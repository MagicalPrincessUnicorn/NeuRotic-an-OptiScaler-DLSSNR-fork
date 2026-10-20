from pathlib import Path
root=Path(__file__).resolve().parents[1];out=root/'builds/audit-api'
def extract(name):
 s=(root/'OptiScaler/gpu_time/GpuTime_Dx12.cpp').read_text();start=s.index(name);op=s.index('{',start);i=op+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
s=r'''
#include <Windows.h>
#include <array>
#include <optional>
#include <memory>
#include <vector>
#include <cassert>
#include <cstdio>
#define LOG_WARN(...) (void)0
#define SAFE_RELEASE(p) if(p){p->Release();p=nullptr;}
using UINT64=uint64_t;
struct D3D12_RANGE {size_t Begin,End;};
constexpr int D3D12_QUERY_TYPE_TIMESTAMP=0;
static unsigned maps=0,queries=0,releases=0;
struct ID3D12QueryHeap{void Release(){++releases;}};
struct ID3D12Resource{UINT64 values[6]={10,30,20,40,30,50};HRESULT Map(int,D3D12_RANGE*,void**out){++maps;*out=values;return S_OK;}void Unmap(int,D3D12_RANGE*){} void Release(){++releases;}};
struct ID3D12CommandQueue{void GetTimestampFrequency(UINT64* f){*f=999999;}};
struct ID3D12GraphicsCommandList{void EndQuery(ID3D12QueryHeap*,int,int){++queries;}void ResolveQueryData(ID3D12QueryHeap*,int,int,int,ID3D12Resource*,size_t){}};
namespace DlssNr::GpuSafety {
struct Recording {bool reusable=false,readable=false;};using Ticket=std::shared_ptr<Recording>;
static std::vector<Ticket> tickets;
Ticket Record(ID3D12GraphicsCommandList*){auto t=std::make_shared<Recording>();tickets.push_back(t);return t;}
bool Reusable(const Ticket&t){return !t||t->reusable;}
bool Readable(const Ticket&t){return t&&t->readable;}
UINT64 TimestampFrequency(const Ticket&){return 1000;}
}
'''
h=(root/'OptiScaler/gpu_time/GpuTime_Dx12.h').read_text();start=h.index('class GpuTime_Dx12');end=h.index('class ScopedGpuTime_Dx12');h=h[start:end].replace('ID3D12Device* device','void* device');s+=h
s+='GpuTime_Dx12::GpuTime_Dx12(void*,bool tracked):_completionTracked(tracked){static ID3D12QueryHeap heap;static ID3D12Resource data;_queryHeap=&heap;_readbackBuffer=&data;_init=true;}\n'
for name in ['GpuTime_Dx12::~GpuTime_Dx12()','void GpuTime_Dx12::Start(','void GpuTime_Dx12::End(','std::optional<double> GpuTime_Dx12::ReadGpuTime(']:s+=extract(name)+'\n'
s+=r'''
int main(){
 ID3D12GraphicsCommandList cmd;ID3D12CommandQueue queue;
 {
  GpuTime_Dx12 timer(nullptr);
  for(int i=0;i<3;++i){timer.Start(&cmd);timer.End(&cmd);assert(!timer.ReadGpuTime(&queue));}
  assert(maps==0&&queries==6);timer.Start(&cmd);timer.End(&cmd);assert(queries==6);
  auto& tickets=DlssNr::GpuSafety::tickets;tickets[0]->readable=true;
  auto value=timer.ReadGpuTime(&queue);assert(value&&*value==20.0&&maps==1);
  assert(!timer.ReadGpuTime(&queue)&&maps==1); // unread only, exact producer frequency
  timer.Start(&cmd);timer.End(&cmd);assert(queries==6); // readable does not release executable replay
  tickets[0]->reusable=true;timer.Start(&cmd);timer.End(&cmd);assert(queries==8);
 }
 assert(releases==0); // pending query objects retained at feature destruction
 std::puts("PASS production GPU timer completion, unread samples, producer frequency and pending destruction");
}
'''
(out/'gpu-time.cpp').write_text(s)
(out/'run-gpu-time.cmd').write_text('@echo off\ncall "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX builds/audit-api/gpu-time.cpp /Fo:builds/audit-api/ /Fe:builds/audit-api/gpu-time.exe\nif errorlevel 1 exit /b 1\nbuilds\\audit-api\\gpu-time.exe\n')
