#ifndef CHARACTER_CAPTURE_TEST
#include "pch.h"
#endif
#include "CharacterCaptureDx11.h"
#include "CharacterThumbnailBytecode.h"
#include <mutex>
#include <chrono>
#include <thread>
#include <cstring>
namespace Neurotic::Semantic::Character {
using Microsoft::WRL::ComPtr;
struct CharacterCaptureDx11::Impl {
 struct Slot {
  CapturePhase phase=CapturePhase::Free;
  ComPtr<ID3D11DeviceContext> recording;
  ComPtr<ID3D11Texture2D> input,thumbnail,readback,source;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11UnorderedAccessView> uav;
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11Query> done;
  D3D11_TEXTURE2D_DESC sourceDesc{};
  CpuFrame frame;
 };
 std::mutex mutex;std::atomic<std::uint64_t> generation{1};
 std::array<Slot,3> slots;
 ComPtr<ID3D11Device> device,requestedDevice;
 ComPtr<ID3D11ComputeShader> shader;
 D3D11_TEXTURE2D_DESC requested{};
 std::thread::id renderThread;
 bool dead=false;
 static bool Same(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b){return a.Width==b.Width&&a.Height==b.Height&&a.Format==b.Format;}
 static std::pair<unsigned,unsigned> Size(const D3D11_TEXTURE2D_DESC& d){const auto scale=std::min(1.,960./std::max(d.Width,d.Height));return {std::max(1u,static_cast<unsigned>(d.Width*scale)),std::max(1u,static_cast<unsigned>(d.Height*scale))};}
 bool Pending() const {for(auto& s:slots)if(s.phase==CapturePhase::Submitted)return true;return false;}
 // Caller owns the serialized immediate-context boundary. Worker never enters.
 bool OwnsBoundary(ID3D11Device* boundary) const {return device.Get()==boundary&&renderThread==std::this_thread::get_id();}
 void Pump(ID3D11Device* boundary){
  if(!device||dead||!Pending()||!OwnsBoundary(boundary))return;
  if(FAILED(device->GetDeviceRemovedReason())){dead=true;for(auto& s:slots)s=Slot{};return;}
  ComPtr<ID3D11DeviceContext> immediate;device->GetImmediateContext(&immediate);
  for(auto& s:slots)if(s.phase==CapturePhase::Submitted){
   BOOL finished=FALSE;const auto status=immediate->GetData(s.done.Get(),&finished,sizeof(finished),D3D11_ASYNC_GETDATA_DONOTFLUSH);
   if(status!=S_OK||!finished)continue;
   s.source.Reset();
   if(s.frame.captureGeneration!=generation){s.phase=CapturePhase::Free;continue;}
   D3D11_MAPPED_SUBRESOURCE mapped{};const auto result=immediate->Map(s.readback.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
   if(FAILED(result))continue;
   for(unsigned y=0;y<s.frame.height;++y)std::memcpy(s.frame.pixels.data()+static_cast<size_t>(y)*s.frame.stride,static_cast<const unsigned char*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch,s.frame.stride);
   immediate->Unmap(s.readback.Get(),0);s.phase=CapturePhase::Complete;
  }
 }
 bool PrepareSlot(Slot& s){
  const auto [w,h]=Size(requested);const size_t bytes=static_cast<size_t>(w)*h*4;
  if(s.input&&Same(s.sourceDesc,requested)){s.frame.pixels.resize(bytes);return true;}
  s=Slot{};s.sourceDesc=requested;
  if(FAILED(device->CreateDeferredContext(0,&s.recording)))return false;
  auto input=requested;input.SampleDesc={1,0};input.Usage=D3D11_USAGE_DEFAULT;input.BindFlags=D3D11_BIND_SHADER_RESOURCE;input.CPUAccessFlags=input.MiscFlags=0;
  if(FAILED(device->CreateTexture2D(&input,nullptr,&s.input))||FAILED(device->CreateShaderResourceView(s.input.Get(),nullptr,&s.srv)))return false;
  auto target=input;target.Width=w;target.Height=h;target.Format=DXGI_FORMAT_R8G8B8A8_UNORM;target.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  if(FAILED(device->CreateTexture2D(&target,nullptr,&s.thumbnail))||FAILED(device->CreateUnorderedAccessView(s.thumbnail.Get(),nullptr,&s.uav)))return false;
  target.Usage=D3D11_USAGE_STAGING;target.BindFlags=0;target.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  if(FAILED(device->CreateTexture2D(&target,nullptr,&s.readback)))return false;
  D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=32;buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
  if(FAILED(device->CreateBuffer(&buffer,nullptr,&s.constants))||FAILED(device->CreateQuery(&query,&s.done)))return false;
  s.frame.width=w;s.frame.height=h;s.frame.stride=w*4;s.frame.pixels.resize(bytes);return true;
 }
 bool Submit(ID3D11Device* current,ID3D11Texture2D* source,CpuFrame metadata){
  if(!current||!source||metadata.captureGeneration!=generation)return false;
  // SINGLETHREADED explicitly forbids device work on our control worker.
  if(current->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)return false;
  ComPtr<ID3D11Device> sourceDevice;source->GetDevice(&sourceDevice);if(sourceDevice.Get()!=current)return false;
  D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
  if(!desc.Width||!desc.Height||desc.Width>32768||desc.Height>32768||desc.ArraySize!=1||desc.MipLevels!=1||!desc.SampleDesc.Count||desc.Usage!=D3D11_USAGE_DEFAULT)return false;
  const bool sdr=metadata.colorPolicy==0&&(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM);
  if(!sdr&&!(metadata.colorPolicy==1&&desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM)&&!(metadata.colorPolicy==2&&desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT))return false;
  if(desc.SampleDesc.Count>1){UINT support=0;if(FAILED(current->CheckFormatSupport(desc.Format,&support))||!(support&D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE))return false;}
  requestedDevice=current;requested=desc;
  if(device.Get()!=current||!shader||dead)return false;
  // Only the render owner that issued pending work may touch its immediate context.
  // Once retired, a serialized renderer may move to another thread.
  if(!Pending())renderThread=std::this_thread::get_id();
  if(!OwnsBoundary(current))return false;
  Pump(current);if(dead)return false;
  const auto [w,h]=Size(desc);Slot* chosen=nullptr;
  for(auto& s:slots)if(s.phase==CapturePhase::Free&&s.recording&&s.input&&s.srv&&s.uav&&s.readback&&s.constants&&s.done&&Same(s.sourceDesc,desc)&&s.frame.pixels.size()==static_cast<size_t>(w)*h*4){chosen=&s;break;}
  if(!chosen)return false;auto& s=*chosen;
  metadata.width=w;metadata.height=h;metadata.stride=w*4;metadata.pixels.swap(s.frame.pixels);s.frame=std::move(metadata);
  struct Constants {unsigned sw,sh,tw,th,policy;float exposure;unsigned padding[2];} constants{desc.Width,desc.Height,w,h,s.frame.colorPolicy,1,{0,0}};
  if(desc.SampleDesc.Count>1)s.recording->ResolveSubresource(s.input.Get(),0,source,0,desc.Format);
  else s.recording->CopyResource(s.input.Get(),source);
  s.recording->UpdateSubresource(s.constants.Get(),0,nullptr,&constants,0,0);
  s.recording->CSSetShader(shader.Get(),nullptr,0);auto* srv=s.srv.Get();auto* uav=s.uav.Get();auto* cb=s.constants.Get();
  s.recording->CSSetShaderResources(0,1,&srv);s.recording->CSSetUnorderedAccessViews(0,1,&uav,nullptr);s.recording->CSSetConstantBuffers(0,1,&cb);
  s.recording->Dispatch((w+7)/8,(h+7)/8,1);
  uav=nullptr;s.recording->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
  s.recording->CopyResource(s.readback.Get(),s.thumbnail.Get());s.recording->End(s.done.Get());
  ComPtr<ID3D11CommandList> commands;if(FAILED(s.recording->FinishCommandList(FALSE,&commands))){s.recording->ClearState();return false;}
  // mutex serializes this final admission and source acquisition with resize.
  s.source=source;s.phase=CapturePhase::Submitted;
  ComPtr<ID3D11DeviceContext> immediate;current->GetImmediateContext(&immediate);immediate->ExecuteCommandList(commands.Get(),TRUE);return true;
 }
};
CharacterCaptureDx11::CharacterCaptureDx11():impl_(std::make_unique<Impl>()){}
CharacterCaptureDx11::~CharacterCaptureDx11()=default;
std::uint64_t CharacterCaptureDx11::AdmissionToken() const noexcept{return impl_->generation;}
void CharacterCaptureDx11::InvalidateAdmission(){std::lock_guard lock(impl_->mutex);++impl_->generation;for(auto& s:impl_->slots)if(s.phase==CapturePhase::Complete)s.phase=CapturePhase::Free;}
bool CharacterCaptureDx11::TrySubmit(ID3D11Device* device,ID3D11Texture2D* source,CpuFrame metadata){std::unique_lock lock(impl_->mutex,std::try_to_lock);return lock&&impl_->Submit(device,source,std::move(metadata));}
bool CharacterCaptureDx11::TrySubmitSwapchain(ID3D11Device* device,IDXGISwapChain* chain,CpuFrame metadata){
 std::unique_lock lock(impl_->mutex,std::try_to_lock);if(!lock||!device||!chain||metadata.captureGeneration!=impl_->generation)return false;
 ComPtr<ID3D11Texture2D> source;
 // D3D11 presents rotate buffer identities internally; buffer zero is its render surface.
 if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&source))))return false;
 return impl_->Submit(device,source.Get(),std::move(metadata));
}
void CharacterCaptureDx11::PrepareRequested(){
 std::unique_lock lock(impl_->mutex,std::try_to_lock);if(!lock||!impl_->requestedDevice)return;
 if(impl_->device!=impl_->requestedDevice){
  if(impl_->Pending())return;
  for(auto& s:impl_->slots)s=Impl::Slot{};
  impl_->shader.Reset();impl_->device=impl_->requestedDevice;impl_->dead=false;
 }
 if(impl_->dead)return;
 if(!impl_->shader&&FAILED(impl_->device->CreateComputeShader(g_CharacterThumbnail,sizeof(g_CharacterThumbnail),nullptr,&impl_->shader)))return;
 for(auto& s:impl_->slots)if(s.phase==CapturePhase::Free&&!impl_->PrepareSlot(s))s=Impl::Slot{};
}
bool CharacterCaptureDx11::TryTakeNewest(CpuFrame& output){
 std::unique_lock lock(impl_->mutex,std::try_to_lock);if(!lock)return false;Impl::Slot* newest=nullptr;
 for(auto& s:impl_->slots)if(s.phase==CapturePhase::Complete&&s.frame.captureGeneration==impl_->generation&&(!newest||s.frame.key.sequence>newest->frame.key.sequence))newest=&s;
 if(!newest)return false;output=std::move(newest->frame);
 for(auto& s:impl_->slots)if(s.phase==CapturePhase::Complete)s.phase=CapturePhase::Free;
 return true;
}
void CharacterCaptureDx11::PollRetirement(ID3D11Device* boundary){std::unique_lock lock(impl_->mutex,std::try_to_lock);if(lock)impl_->Pump(boundary);}
void CharacterCaptureDx11::ReleaseDevice(ID3D11Device* boundary){
 if(!boundary)return;
 std::lock_guard lock(impl_->mutex);
 if(impl_->device.Get()!=boundary){
  // A renderer can disappear while its first preparation request is queued.
  if(impl_->requestedDevice.Get()==boundary){++impl_->generation;impl_->requestedDevice.Reset();impl_->requested={};}
  return;
 }
 ++impl_->generation;
 // D3D11 owns the queued command stream and defers destruction of resources
 // referenced by its pending GPU work. Release only our COM references; never
 // query, map, flush or issue commands through the disappearing renderer.
 for(auto& slot:impl_->slots)slot=Impl::Slot{};
 impl_->shader.Reset();impl_->device.Reset();impl_->requestedDevice.Reset();
 impl_->requested={};impl_->renderThread={};impl_->dead=false;
}
bool CharacterCaptureDx11::RetireSources(ID3D11Device* boundary,unsigned timeoutMs){
 const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(std::min(timeoutMs,500u));bool flushed=false;
 do{
  {std::unique_lock lock(impl_->mutex,std::try_to_lock);if(lock){
   if(!impl_->Pending())return true;
   if(!impl_->OwnsBoundary(boundary))return false;
   impl_->Pump(boundary);if(!impl_->Pending())return true;
   // A resize may prevent another Present from flushing the recorded query.
   // Only this explicit retirement boundary may flush, never ordinary polling.
   if(!flushed&&timeoutMs&&impl_->device&&!impl_->dead){ComPtr<ID3D11DeviceContext> context;impl_->device->GetImmediateContext(&context);context->Flush();flushed=true;}
  }}
  if(!timeoutMs)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }while(std::chrono::steady_clock::now()<end);
 return false; // All pending references survive a timeout.
}
}
