#include "nr_color_compose_fixture.h"
#include <dlssnr/PresentColor.h>
int main(){Warp w;constexpr UINT W=16,H=8;std::vector<uint32_t> pixels(W*H);for(UINT i=0;i<W*H;++i)pixels[i]=((i*83)%1024)|(((i*97)%1024)<<10)|(((i*53)%1024)<<20)|((i%4)<<30);
 for(UINT scale:{1u,2u})for(bool edit:{false,true}){
 auto pq=w.texture(DXGI_FORMAT_R10G10B10A2_UNORM,W,H,D3D12_RESOURCE_STATE_COPY_DEST);w.upload(pq.Get(),pixels.data(),4);
 auto linear=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 FT_Dx12 decode("pipeline decode",w.d.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,FT_Dx12::Transfer::Pq2020ToScRgb);assert(decode.BindImmutableDescriptors(pq.Get(),linear.Get()));w.run([&](auto* l){assert(decode.Dispatch(l,pq.Get(),linear.Get()));Warp::barrier(l,linear.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
 auto proxy=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto keep=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 DlssNr::HdrObservation::Snapshot observed;observed.registered=true;observed.format=DXGI_FORMAT_R10G10B10A2_UNORM;observed.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
 const auto selected=DlssNr::PresentColor::Select(observed,observed.format);assert(selected.Supported());const auto operation=selected.Operation(observed.format);
 DlssNrConstants c{};c.Width=W;c.Height=H;Neurotic::Context::ApplyModelColor({operation.hdr?0u:1u,operation.modelWhitePoint,operation.signedOriginal?1u:0u,0u,0.0f,0u},c);
 c.TransferStrength=c.ColourStrength=1;c.MaxRatio=2;c.ApplyModel=1;c.Transfer=1;
 Compose encode(w,c,linear.Get(),nullptr,nullptr,proxy.Get(),keep.Get());w.run([&](auto* l){encode.dispatch(l,W,H);Warp::barrier(l,proxy.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Warp::barrier(l,keep.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
 auto reduced=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,W/scale,H/scale,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto downC=c;downC.Mode=2;downC.Width=W/scale;downC.Height=H/scale;
 Compose down(w,downC,proxy.Get(),nullptr,nullptr,reduced.Get(),nullptr);w.run([&](auto* l){down.dispatch(l,W/scale,H/scale);});auto modelBytes=w.read(reduced.Get(),4);
 if(edit)for(size_t i=0;i<modelBytes.size();i+=4)modelBytes[i]=(unsigned char)std::min(255,int(modelBytes[i])+16);
 auto model=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,W/scale,H/scale,D3D12_RESOURCE_STATE_COPY_DEST);w.upload(model.Get(),modelBytes.data(),4);
 auto composed=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 w.run([&](auto* l){Warp::barrier(l,reduced.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
 c.Mode=1;Compose resolve(w,c,reduced.Get(),model.Get(),keep.Get(),composed.Get(),nullptr);w.run([&](auto* l){resolve.dispatch(l,W,H);});
 auto rgb=w.read(composed.Get(),8);for(size_t i=0;i<rgb.size();i+=2){HALF h;memcpy(&h,rgb.data()+i,2);assert(std::isfinite(XMConvertHalfToFloat(h)));}
 auto out=w.texture(DXGI_FORMAT_R10G10B10A2_UNORM,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 FT_Dx12 inverse("pipeline inverse",w.d.Get(),DXGI_FORMAT_R10G10B10A2_UNORM,FT_Dx12::Transfer::ScRgbToPq2020);assert(inverse.BindImmutableDescriptors(composed.Get(),out.Get(),pq.Get()));
 w.run([&](auto* l){Warp::barrier(l,composed.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);assert(inverse.Dispatch(l,composed.Get(),out.Get()));});auto result=w.read(out.Get(),4);unsigned changed=0;
 for(UINT i=0;i<W*H;++i){uint32_t actual;memcpy(&actual,result.data()+i*4,4);assert((actual>>30)==(pixels[i]>>30));if(actual!=pixels[i])++changed;}
 if(!edit)assert(changed==0);else assert(changed>0);
 std::cout<<"PASS: connected PQ -> proxy/model surrogate -> composed scRGB -> PQ scale="<<scale<<" edit="<<edit<<" changed="<<changed<<'\n';
 }
 // Direct signed scRGB bypasses PQ transfer. The original keeps negative and
 // >1 values and alpha, while the model proxy is nonnegative. A changed model
 // must still affect output; no-edit and zero-strength must reproduce input.
 for(unsigned mode=0;mode<3;++mode){
 std::vector<HALF> original(W*H*4);const float rgbValues[]={-.25f,2.5f,12.5f},alphaValues[]={0.0f,.25f,1.0f};
 for(UINT i=0;i<W*H;++i){for(UINT k=0;k<3;++k)original[i*4+k]=XMConvertFloatToHalf(rgbValues[(i+k)%3]);original[i*4+3]=XMConvertFloatToHalf(alphaValues[i%3]);}
 auto source=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_COPY_DEST);w.upload(source.Get(),original.data(),8);
 auto proxy=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),keep=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 DlssNr::HdrObservation::Snapshot observed;observed.registered=true;observed.format=DXGI_FORMAT_R16G16B16A16_FLOAT;observed.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
 const auto selected=DlssNr::PresentColor::Select(observed,observed.format);assert(selected.Supported());const auto operation=selected.Operation(observed.format);
 assert(operation.conversion==Neurotic::Context::PresentColorConversion::Copy&&operation.workingNitsPerUnit==80.0f);
 DlssNrConstants c{};c.Width=W;c.Height=H;Neurotic::Context::ApplyModelColor({operation.hdr?0u:1u,operation.modelWhitePoint,operation.signedOriginal?1u:0u,0u,0.0f,0u},c);
 c.TransferStrength=c.ColourStrength=mode==2?0.0f:1.0f;c.MaxRatio=2;c.ApplyModel=1;c.Transfer=1;
 Compose encode(w,c,source.Get(),nullptr,nullptr,proxy.Get(),keep.Get());w.run([&](auto* l){encode.dispatch(l,W,H);Warp::barrier(l,keep.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
 auto modelBytes=w.read(proxy.Get(),4);for(UINT i=0;i<W*H;++i)if(XMConvertHalfToFloat(original[i*4])<0)assert(modelBytes[i*4]==0);
 if(mode)for(size_t i=0;i<modelBytes.size();i+=4)modelBytes[i]=(unsigned char)std::min(255,int(modelBytes[i])+16);
 w.run([&](auto* l){Warp::barrier(l,proxy.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
 auto model=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,W,H,D3D12_RESOURCE_STATE_COPY_DEST);w.upload(model.Get(),modelBytes.data(),4);
 auto output=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);c.Mode=1;
 Compose resolve(w,c,proxy.Get(),model.Get(),keep.Get(),output.Get(),nullptr);w.run([&](auto* l){resolve.dispatch(l,W,H);});const auto result=w.read(output.Get(),8);unsigned changed=0;
 for(UINT i=0;i<W*H;++i){HALF actual[4];memcpy(actual,result.data()+i*8,8);assert(actual[3]==original[i*4+3]);for(UINT k=0;k<3;++k){assert(std::isfinite(XMConvertHalfToFloat(actual[k])));if(actual[k]!=original[i*4+k])++changed;}}
 if(mode==1)assert(changed>0);else assert(changed==0);
 std::cout<<"PASS: direct signed scRGB (-.25,2.5,12.5), alpha0/.25/1 mode="<<mode<<" changed="<<changed<<'\n';
 }
 w.clean();}
