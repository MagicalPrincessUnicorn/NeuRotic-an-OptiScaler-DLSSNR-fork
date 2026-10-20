#include <dlssnr/NativeD3D11Guides.h>
#include <dlssnr/connections/GpuBudget.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
#include <cmath>
#include <cstdio>
using Microsoft::WRL::ComPtr;
using namespace DlssNr::NativeD3D11Guides;
int main(){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;
 assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)));
 HookDevice(d.Get());
 TestSetCaptureActive(true);
 D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=16;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;desc.Format=DXGI_FORMAT_D32_FLOAT;
 ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> dv;assert(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&depth)));assert(SUCCEEDED(d->CreateDepthStencilView(depth.Get(),nullptr,&dv)));
 const char* source="float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),.25,1);} float4 PS():SV_Target{return float4(1,0,0,1);}";
 ComPtr<ID3DBlob> vs,ps,errors;assert(SUCCEEDED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"VS","vs_5_0",0,0,&vs,&errors)));assert(SUCCEEDED(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,&ps,&errors)));
 ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;assert(SUCCEEDED(d->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex)));assert(SUCCEEDED(d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel)));
 auto render=[&](ID3D11DeviceContext* ctx){ctx->ClearDepthStencilView(dv.Get(),D3D11_CLEAR_DEPTH,1,0);ctx->OMSetRenderTargets(0,nullptr,dv.Get());D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};ctx->RSSetViewports(1,&viewport);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vertex.Get(),nullptr,0);ctx->PSSetShader(pixel.Get(),nullptr,0);ctx->Draw(3,0);ctx->ClearDepthStencilView(dv.Get(),D3D11_CLEAR_DEPTH,1,0);};
 auto verify=[&]{ComPtr<ID3D11Texture2D> captured;int direction=-1;assert(TestObservedDepth(c.Get(),desc.Width,desc.Height,&captured,direction)&&direction==0);D3D11_TEXTURE2D_DESC staging{};captured->GetDesc(&staging);staging.Usage=D3D11_USAGE_STAGING;staging.BindFlags=staging.MiscFlags=0;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;assert(SUCCEEDED(d->CreateTexture2D(&staging,nullptr,&read)));c->CopyResource(read.Get(),captured.Get());D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)));float value=*reinterpret_cast<float*>(static_cast<char*>(mapped.pData)+8*mapped.RowPitch+8*sizeof(float));c->Unmap(read.Get(),0);assert(std::abs(value-.25f)<.001f);TestConsumeInterval(c.Get());captured.Reset();assert(!TestObservedDepth(c.Get(),desc.Width,desc.Height,&captured,direction));};
 uint64_t initial=0,current=0;
 assert(TestSourceReset(depth.Get(),16,16,0,initial));TestAcceptHistory(true);
 for(unsigned frame=0;frame<12;++frame){assert(!TestSourceReset(depth.Get(),16,16,0,current));assert(current==initial);render(c.Get());verify();}
 TestAcceptHistory(false);assert(TestSourceReset(depth.Get(),16,16,0,current)&&current==initial);TestAcceptHistory(true);
 assert(TestSourceReset(depth.Get(),16,16,1,current)&&current==initial+1);TestAcceptHistory(true);
 assert(TestSourceReset(depth.Get(),16,16,0,current)&&current==initial+2);TestAcceptHistory(true);
 // Installed observation stays available while NR is off, but no draw or
 // destructive-clear snapshot capture occurs until the next enabled interval.
 TestSetCaptureActive(false);auto disabledBytes=TestSnapshotBytes();render(c.Get());
 ComPtr<ID3D11Texture2D> disabledCapture;int disabledDirection=-1;
 assert(!TestObservedDepth(c.Get(),16,16,&disabledCapture,disabledDirection));assert(TestSnapshotBytes()==disabledBytes);TestConsumeInterval(c.Get());
 TestSetCaptureActive(true);render(c.Get());verify();
 ComPtr<ID3D11DeviceContext> deferred;assert(SUCCEEDED(d->CreateDeferredContext(0,&deferred)));render(deferred.Get());ComPtr<ID3D11CommandList> list;assert(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)));c->ExecuteCommandList(list.Get(),FALSE);verify();
 for(unsigned repeat=0;repeat<4;++repeat){render(deferred.Get());list.Reset();assert(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)));c->ExecuteCommandList(list.Get(),FALSE);verify();}
 // More identities than the two bounded candidate slots, alternating immediate
 // and deferred execution, must recover after each completed interval.
 const auto boundedBytes=TestSnapshotBytes();
 for(unsigned resize=0;resize<12;++resize){
  desc.Width=desc.Height=16+resize;dv.Reset();depth.Reset();
  assert(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&depth)));assert(SUCCEEDED(d->CreateDepthStencilView(depth.Get(),nullptr,&dv)));
  auto before=current;assert(TestSourceReset(depth.Get(),desc.Width,desc.Height,0,current)&&current==before+1);TestAcceptHistory(true);
  assert(!TestSourceReset(depth.Get(),desc.Width,desc.Height,0,current));
  if(resize%2){render(deferred.Get());list.Reset();assert(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)));c->ExecuteCommandList(list.Get(),FALSE);}else render(c.Get());
  verify();assert(TestSnapshotBytes()<=boundedBytes+2*65536);
 }
 assert(DlssNr::Connections::GpuBudget::Bytes()==TestSnapshotBytes());
 // Another core owner consuming the aggregate budget must refuse a larger
 // snapshot even when this observer remains below its local 128 MiB ceiling.
 auto occupied=DlssNr::Connections::GpuBudget::Reserve(DlssNr::Connections::GpuBudget::Limit()-DlssNr::Connections::GpuBudget::Bytes());assert(occupied);
 desc.Width=desc.Height=512;dv.Reset();depth.Reset();assert(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&depth)));assert(SUCCEEDED(d->CreateDepthStencilView(depth.Get(),nullptr,&dv)));
 render(c.Get());ComPtr<ID3D11Texture2D> budgetRejected;int budgetDirection=-1;assert(!TestObservedDepth(c.Get(),512,512,&budgetRejected,budgetDirection));TestConsumeInterval(c.Get());occupied.reset();
 render(c.Get());verify();assert(DlssNr::Connections::GpuBudget::Bytes()==TestSnapshotBytes());
 c->ExecuteCommandList(list.Get(),FALSE);ComPtr<ID3D11Texture2D> replay;int direction=-1;assert(!TestObservedDepth(c.Get(),16,16,&replay,direction));
 std::puts("PASS D3D11 WARP actual immediate/deferred Draw, pre-clear depth preservation, freshness, NR-off capture suppression/re-enable, 12 sustained history frames, 12 identity/resize changes, bounded snapshot reuse, aggregate budget refusal/recovery and untracked replay refusal");
}
