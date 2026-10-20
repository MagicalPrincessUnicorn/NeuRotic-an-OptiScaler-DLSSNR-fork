#include "nr/semantic/character/CharacterCaptureDx11.h"
#include <d3d11sdklayers.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <cstring>
#include "character_test_errors.h"
using namespace Neurotic::Semantic::Character;
using Microsoft::WRL::ComPtr;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void HR(HRESULT r){Check(SUCCEEDED(r),"D3D11 host operation failed");}
static std::vector<unsigned char> ReadSource(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source){
 D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);const unsigned bpp=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;
 if(desc.SampleDesc.Count>1){desc.SampleDesc={1,0};desc.BindFlags=desc.MiscFlags=0;ComPtr<ID3D11Texture2D> resolved;HR(device->CreateTexture2D(&desc,nullptr,&resolved));context->ResolveSubresource(resolved.Get(),0,source,0,desc.Format);return ReadSource(device,context,resolved.Get());}
 desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;HR(device->CreateTexture2D(&desc,nullptr,&staging));context->CopyResource(staging.Get(),source);
 D3D11_MAPPED_SUBRESOURCE mapped{};HR(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));std::vector<unsigned char> result(static_cast<size_t>(desc.Width)*desc.Height*bpp);
 for(unsigned y=0;y<desc.Height;++y)std::memcpy(result.data()+static_cast<size_t>(y)*desc.Width*bpp,static_cast<unsigned char*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch,desc.Width*bpp);
 context->Unmap(staging.Get(),0);return result;
}
int main(int argc,char**){try{
 CharacterTestQuietFailures();
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 const auto driver=argc>1?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP;
 auto created=D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
 const bool debug=SUCCEEDED(created);
 if(!debug)HR(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
 ComPtr<ID3D11InfoQueue> info;if(SUCCEEDED(device.As(&info)))for(auto severity:{D3D11_MESSAGE_SEVERITY_CORRUPTION,D3D11_MESSAGE_SEVERITY_ERROR,D3D11_MESSAGE_SEVERITY_WARNING})info->SetBreakOnSeverity(severity,FALSE);
 CharacterCaptureDx11 capture;CpuFrame metadata;metadata.key.sequence=1;metadata.key.captureNs=100;
 for(unsigned policy=0;policy<4;++policy){
  D3D11_TEXTURE2D_DESC td{};td.Width=policy?113:1001;td.Height=policy?79:563;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
  td.Format=policy==0?DXGI_FORMAT_R8G8B8A8_UNORM:policy<3?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;
  ComPtr<ID3D11Texture2D> source;HR(device->CreateTexture2D(&td,nullptr,&source));
  ComPtr<ID3D11RenderTargetView> rtv;HR(device->CreateRenderTargetView(source.Get(),nullptr,&rtv));
  const float color[]{.25f,.5f,.75f,1},white[]{1,1,1,1};context->ClearRenderTargetView(rtv.Get(),policy<2?color:white);
  const auto original=ReadSource(device.Get(),context.Get(),source.Get());
  auto* bound=rtv.Get();context->OMSetRenderTargets(1,&bound,nullptr);D3D11_VIEWPORT vp{3,4,57,43,0,1};context->RSSetViewports(1,&vp);
  metadata.captureGeneration=capture.AdmissionToken();metadata.colorPolicy=policy?policy-1:0;metadata.key.sequence=policy+1;
  const bool initial=capture.TrySubmit(device.Get(),source.Get(),metadata);
  if(policy!=2)Check(!initial,"cold capture allocated on render thread");
  if(!initial){std::thread prepare([&]{capture.PrepareRequested();});prepare.join();Check(capture.TrySubmit(device.Get(),source.Get(),metadata),"warmed D3D11 capture rejected");}
  ComPtr<ID3D11RenderTargetView> after;context->OMGetRenderTargets(1,&after,nullptr);Check(after.Get()==rtv.Get(),"capture changed game render target");
  UINT count=1;D3D11_VIEWPORT actual{};context->RSGetViewports(&count,&actual);Check(actual.TopLeftX==3&&actual.Width==57,"capture changed game viewport");
  context->Flush();CpuFrame output;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(std::chrono::steady_clock::now()<deadline){capture.PollRetirement(device.Get());bool ready=false;std::thread worker([&]{ready=capture.TryTakeNewest(output);});worker.join();if(ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  Check(output.width==(policy?113u:960u)&&output.height==(policy?79u:539u)&&SameKey(output.key,metadata.key),"thumbnail dimensions or frame identity incorrect");
  const int sdr[]{64,128,191};for(size_t i=0;i<output.pixels.size();i+=4){for(unsigned c=0;c<3;++c)Check(std::abs(int(output.pixels[i+c])-(policy<2?sdr[c]:policy==2?253:143))<=2,"SDR/PQ/scRGB pixel mismatch");Check(output.pixels[i+3]==255,"alpha mismatch");}
  Check(capture.RetireSources(device.Get(),500),"completed source not retired");
  Check(ReadSource(device.Get(),context.Get(),source.Get())==original,"capture modified original source pixels");
  const auto old=capture.AdmissionToken();capture.InvalidateAdmission();metadata.captureGeneration=old;Check(!capture.TrySubmit(device.Get(),source.Get(),metadata),"stale admission accepted");
 }
 // Legacy MSAA swapchains require a private resolve; plain CopyResource is invalid.
 {
  D3D11_TEXTURE2D_DESC ms{};ms.Width=1001;ms.Height=563;ms.MipLevels=ms.ArraySize=1;ms.SampleDesc={4,0};ms.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ms.BindFlags=D3D11_BIND_RENDER_TARGET;
  UINT qualities=0;HR(device->CheckMultisampleQualityLevels(ms.Format,4,&qualities));Check(qualities>0,"host lacks required 4x MSAA fixture");
  ComPtr<ID3D11Texture2D> msSource;HR(device->CreateTexture2D(&ms,nullptr,&msSource));ComPtr<ID3D11RenderTargetView> msView;HR(device->CreateRenderTargetView(msSource.Get(),nullptr,&msView));
  const float color[]{.25f,.5f,.75f,1};context->ClearRenderTargetView(msView.Get(),color);const auto original=ReadSource(device.Get(),context.Get(),msSource.Get());
  CharacterCaptureDx11 msCapture;CpuFrame msMeta;msMeta.key.sequence=77;msMeta.key.captureNs=1234;msMeta.captureGeneration=msCapture.AdmissionToken();
  Check(!msCapture.TrySubmit(device.Get(),msSource.Get(),msMeta),"MSAA cold capture prepared on render");msCapture.PrepareRequested();
  Check(msCapture.TrySubmit(device.Get(),msSource.Get(),msMeta),"4x MSAA capture refused");Check(msCapture.RetireSources(device.Get(),500),"MSAA capture did not retire");CpuFrame resolved;Check(msCapture.TryTakeNewest(resolved),"MSAA pixels missing");
  Check(resolved.width==960&&resolved.height==539&&SameKey(resolved.key,msMeta.key),"MSAA thumbnail metadata mismatch");
  for(size_t i=0;i<resolved.pixels.size();i+=4)Check(std::abs(int(resolved.pixels[i])-64)<=1&&std::abs(int(resolved.pixels[i+1])-128)<=1&&std::abs(int(resolved.pixels[i+2])-191)<=1&&resolved.pixels[i+3]==255,"MSAA resolve pixels incorrect");
  Check(ReadSource(device.Get(),context.Get(),msSource.Get())==original,"MSAA source altered");
 }
 // A real shared fence stalls the GPU, making slot pressure and resize timeout deterministic.
 ComPtr<IDXGIDevice> dxgiDevice;HR(device.As(&dxgiDevice));ComPtr<IDXGIAdapter> adapter;HR(dxgiDevice->GetAdapter(&adapter));
 ComPtr<ID3D12Device> fenceDevice;HR(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&fenceDevice)));
 ComPtr<ID3D12Fence> hold;HR(fenceDevice->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&hold)));HANDLE shared=nullptr;HR(fenceDevice->CreateSharedHandle(hold.Get(),nullptr,GENERIC_ALL,nullptr,&shared));
 ComPtr<ID3D11Device5> device5;HR(device.As(&device5));ComPtr<ID3D11Fence> fence11;const auto opened=device5->OpenSharedFence(shared,IID_PPV_ARGS(&fence11));CloseHandle(shared);HR(opened);
 ComPtr<ID3D11DeviceContext4> context4;HR(context.As(&context4));
 D3D11_TEXTURE2D_DESC td{};td.Width=128;td.Height=73;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> source;HR(device->CreateTexture2D(&td,nullptr,&source));ComPtr<ID3D11RenderTargetView> view;HR(device->CreateRenderTargetView(source.Get(),nullptr,&view));const float bgra[]{.25f,.5f,.75f,1};context->ClearRenderTargetView(view.Get(),bgra);
 metadata.captureGeneration=capture.AdmissionToken();metadata.colorPolicy=0;
 Check(!capture.TrySubmit(device.Get(),source.Get(),metadata),"changed source prepared during render");capture.PrepareRequested();
 HR(context4->Wait(fence11.Get(),1));
 for(unsigned i=0;i<3;++i){metadata.key.sequence=20+i;Check(capture.TrySubmit(device.Get(),source.Get(),metadata),"three-slot capacity missing");}
 Check(!capture.TrySubmit(device.Get(),source.Get(),metadata),"pending slot reused");Check(!capture.RetireSources(device.Get(),0),"pending source falsely retired");
 HR(hold->Signal(1));context->Flush();ReadSource(device.Get(),context.Get(),source.Get());
 CpuFrame newest;capture.PollRetirement(nullptr);Check(!capture.TryTakeNewest(newest),"foreign boundary polled completed GPU work");Check(!capture.RetireSources(nullptr,0),"foreign boundary retired GPU work");
 bool wrongThreadRetired=false,wrongThreadPublished=false;std::thread foreignThread([&]{capture.PollRetirement(device.Get());wrongThreadRetired=capture.RetireSources(device.Get(),0);wrongThreadPublished=capture.TryTakeNewest(newest);});foreignThread.join();
 Check(!wrongThreadRetired&&!wrongThreadPublished,"foreign thread accessed submitting immediate context");
 Check(capture.RetireSources(device.Get(),500),"unblocked source not retired");Check(capture.TryTakeNewest(newest)&&newest.key.sequence==22,"newest completed frame selection incorrect");
 Check(newest.pixels[0]==64&&newest.pixels[1]==128&&newest.pixels[2]==191,"BGRA channel mapping incorrect");
 capture.PrepareRequested();metadata.captureGeneration=capture.AdmissionToken();Check(capture.TrySubmit(device.Get(),source.Get(),metadata),"recycled slot unavailable");capture.InvalidateAdmission();Check(capture.RetireSources(device.Get(),500),"invalidated work not retired");Check(!capture.TryTakeNewest(newest),"invalidated completed frame escaped");
 // Hidden host swapchain; no game or visible window is launched.
 ComPtr<IDXGIFactory2> factory;HR(adapter->GetParent(IID_PPV_ARGS(&factory)));HWND window=CreateWindowExW(0,L"STATIC",L"Inspector DX11 test",WS_OVERLAPPEDWINDOW,0,0,128,96,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);Check(window!=nullptr,"hidden host window creation failed");
 DXGI_SWAP_CHAIN_DESC1 swapDesc{};swapDesc.Width=128;swapDesc.Height=96;swapDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;swapDesc.SampleDesc.Count=1;swapDesc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;swapDesc.BufferCount=2;swapDesc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
 ComPtr<IDXGISwapChain1> chain;HR(factory->CreateSwapChainForHwnd(device.Get(),window,&swapDesc,nullptr,nullptr,&chain));
 metadata.captureGeneration=capture.AdmissionToken();Check(!capture.TrySubmitSwapchain(device.Get(),chain.Get(),metadata),"swapchain cold preparation on render");capture.PrepareRequested();
 Check(capture.TrySubmitSwapchain(device.Get(),chain.Get(),metadata),"swapchain capture refused");capture.InvalidateAdmission();Check(capture.RetireSources(device.Get(),500),"swapchain source retirement failed");
 HR(chain->ResizeBuffers(2,160,100,DXGI_FORMAT_UNKNOWN,0));metadata.captureGeneration=capture.AdmissionToken();Check(!capture.TrySubmitSwapchain(device.Get(),chain.Get(),metadata),"resized swapchain used stale resources");capture.PrepareRequested();Check(capture.TrySubmitSwapchain(device.Get(),chain.Get(),metadata),"resized swapchain not captured");Check(capture.RetireSources(device.Get(),500),"resized swapchain not retired");chain.Reset();DestroyWindow(window);
 // A device transition cannot publish old-device CPU results into the new stream.
 ComPtr<ID3D11Device> replacement;ComPtr<ID3D11DeviceContext> replacementContext;
 HR(D3D11CreateDevice(nullptr,driver,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&replacement,nullptr,&replacementContext));
 ComPtr<ID3D11Texture2D> replacementSource;HR(replacement->CreateTexture2D(&td,nullptr,&replacementSource));
 Check(!capture.TrySubmit(replacement.Get(),source.Get(),metadata),"foreign-device source accepted");
 Check(!capture.TrySubmit(replacement.Get(),replacementSource.Get(),metadata),"new device reused previous resources");capture.PrepareRequested();
 Check(!capture.TryTakeNewest(newest),"old-device CPU result escaped after device change");
 Check(capture.TrySubmit(replacement.Get(),replacementSource.Get(),metadata),"replacement device capture failed");Check(capture.RetireSources(replacement.Get(),500),"replacement device retirement failed");
 metadata.colorPolicy=99;Check(!capture.TrySubmit(replacement.Get(),replacementSource.Get(),metadata),"unknown color encoding accepted");metadata.colorPolicy=0;
 // Destruction releases CPU ownership; D3D11's command stream keeps queued resources alive.
 // A timeout is not treated as GPU completion, even immediately before shutdown.
 {
  CharacterCaptureDx11 ending;metadata.captureGeneration=ending.AdmissionToken();Check(!ending.TrySubmit(device.Get(),source.Get(),metadata),"shutdown fixture cold capture accepted");ending.PrepareRequested();
  HR(context4->Wait(fence11.Get(),2));Check(ending.TrySubmit(device.Get(),source.Get(),metadata),"shutdown pending capture refused");Check(!ending.RetireSources(device.Get(),0),"shutdown falsely completed pending GPU use");
 }
 HR(hold->Signal(2));context->Flush();ReadSource(device.Get(),context.Get(),source.Get());
 // Releasing the owning renderer may happen without ResizeBuffers or another
 // Present. The next device must recover even while old GPU work is still held.
 {
  CharacterCaptureDx11 released;CpuFrame oldMeta;oldMeta.key.sequence=100;oldMeta.key.captureNs=5678;oldMeta.captureGeneration=released.AdmissionToken();
  Check(!released.TrySubmit(device.Get(),source.Get(),oldMeta),"release fixture cold capture accepted");
  released.ReleaseDevice(device.Get());Check(released.AdmissionToken()!=oldMeta.captureGeneration,"cold release did not invalidate pending preparation admission");released.PrepareRequested();
  Check(!released.TrySubmit(device.Get(),source.Get(),oldMeta),"cold released admission accepted");oldMeta.captureGeneration=released.AdmissionToken();
  Check(!released.TrySubmit(device.Get(),source.Get(),oldMeta),"cold release prepared old request");released.PrepareRequested();
  HR(context4->Wait(fence11.Get(),3));Check(released.TrySubmit(device.Get(),source.Get(),oldMeta),"release fixture pending capture refused");
  released.ReleaseDevice(replacement.Get());Check(released.AdmissionToken()==oldMeta.captureGeneration,"foreign-device release invalidated capture");
  Check(!released.RetireSources(device.Get(),0),"release fixture lost pending ownership");
  std::thread releaseBoundary([&]{released.ReleaseDevice(device.Get());});releaseBoundary.join();
  Check(released.AdmissionToken()!=oldMeta.captureGeneration,"release boundary did not invalidate admission");
  CpuFrame newMeta;newMeta.key.sequence=101;newMeta.key.captureNs=6789;newMeta.captureGeneration=released.AdmissionToken();
  Check(!released.TrySubmit(replacement.Get(),replacementSource.Get(),newMeta),"release recovery reused old device resources");released.PrepareRequested();
  Check(released.TrySubmit(replacement.Get(),replacementSource.Get(),newMeta),"old pending device blocked release recovery");
  Check(released.RetireSources(replacement.Get(),500),"release recovery did not retire");CpuFrame next;Check(released.TryTakeNewest(next)&&next.key.sequence==101,"release recovery published old device frame");
  HR(hold->Signal(3));context->Flush();ReadSource(device.Get(),context.Get(),source.Get());
 }
 if(info)for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;info->GetMessage(i,nullptr,&n);std::vector<unsigned char> data(n);auto* m=reinterpret_cast<D3D11_MESSAGE*>(data.data());HR(info->GetMessage(i,m,&n));if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::cerr<<m->pDescription<<'\n';throw std::runtime_error("D3D11 debug error");}}
 std::cout<<"PASS D3D11 "<<(argc>1?"hardware":"WARP")<<" SDR8/BGRA/SDR10/PQ/scRGB, private 4x MSAA resolve, source pixels preserved, game context restored, worker CPU handoff, three pending slots, blocked GPU timeout, foreign boundary/thread refusal, newest completion, stale admission, real swapchain resize, device replacement, pending shutdown and release-boundary recovery; debug="<<debug<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
