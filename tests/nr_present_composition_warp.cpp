#include "nr_color_compose_fixture.h"
#include "nr_post_present_quality_fixture.h"
int main(int argc,char** argv){std::cout.setf(std::ios::unitbuf);Warp w;
 if(argc==2 && std::strcmp(argv[1],"--quality-smoke")==0) return PostPresentQuality::Run(w,false);
 if(argc==2 && std::strcmp(argv[1],"--quality-geometry")==0) return PostPresentQuality::Run(w,true);
 if(argc!=1) {std::cerr<<"Usage: composition-warp [--quality-smoke|--quality-geometry]\n";return 2;}
 int identityFailures=0;constexpr UINT W=16,H=8;std::vector<HALF> pixels(W*H*4);
 for(UINT i=0;i<W*H;++i){float values[]={float(i%8)*0.1f-0.3f,(i==127?65504.0f:float(i%17)*3.f),float(i%11)*0.002f,float(i%4)/3};if(i==126){values[0]=values[1]=65504.0f;values[2]=0.0f;}for(int c=0;c<4;++c)pixels[i*4+c]=XMConvertFloatToHalf(values[c]);}
 for(bool signedOriginal:{false,true})for(UINT scale:{1u,2u})for(bool apply:{true,false})for(bool edited:{false,true}){
 auto src=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_COPY_DEST);w.upload(src.Get(),pixels.data(),8);
 auto proxy=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto keep=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto result=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 DlssNrConstants c{};c.Width=W;c.Height=H;c.WhitePoint=2.5f;c.TransferStrength=c.ColourStrength=1;c.MaxRatio=2;c.ApplyModel=apply;c.Transfer=1;
 c.SignedPresent=signedOriginal;
 Compose encode(w,c,src.Get(),nullptr,nullptr,proxy.Get(),keep.Get());w.run([&](auto* l){encode.dispatch(l,W,H);Warp::barrier(l,proxy.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Warp::barrier(l,keep.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
 auto reduced=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,W/scale,H/scale,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto downC=c;downC.Mode=2;downC.Width=W/scale;downC.Height=H/scale;
 Compose down(w,downC,proxy.Get(),nullptr,nullptr,reduced.Get(),nullptr);w.run([&](auto* l){down.dispatch(l,W/scale,H/scale);Warp::barrier(l,reduced.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
 auto model=w.texture(DXGI_FORMAT_R8G8B8A8_UNORM,W/scale,H/scale,D3D12_RESOURCE_STATE_COPY_DEST);
 w.run([&](auto* l){Warp::barrier(l,reduced.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);});
 auto modelBytes=w.read(reduced.Get(),4);
 if(edited)for(size_t i=0;i<modelBytes.size();i+=4){modelBytes[i]=(unsigned char)std::min(255,int(modelBytes[i])+16);modelBytes[i+1]=(unsigned char)std::max(0,int(modelBytes[i+1])-48);}
 w.upload(model.Get(),modelBytes.data(),4);
 w.run([&](auto* l){Warp::barrier(l,reduced.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});
 c.Mode=1;Compose resolve(w,c,reduced.Get(),model.Get(),keep.Get(),result.Get(),nullptr);w.run([&](auto* l){resolve.dispatch(l,W,H);});auto actual=w.read(result.Get(),8);

 if(signedOriginal && edited && apply && scale==1){
 auto wide=w.texture(DXGI_FORMAT_R32G32B32A32_FLOAT,W,H,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 Compose inspect(w,c,reduced.Get(),model.Get(),keep.Get(),wide.Get(),nullptr);w.run([&](auto* l){inspect.dispatch(l,W,H);});auto wideBytes=w.read(wide.Get(),16);
 const auto* v=(const float*)wideBytes.data();std::cout<<"Pre-store FP16 boundary red="<<v[126*4]<<'\n';
 assert(v[126*4]<=65504.0f && std::isfinite(v[126*4]));
 }
 if(signedOriginal && (!edited || !apply)){if(memcmp(actual.data(),pixels.data(),actual.size())!=0){std::cerr<<"FAIL: signed original identity scale="<<scale<<" apply="<<apply<<'\n';return 1;}}
 else if(!signedOriginal && (!edited || !apply)) {auto retained=pixels;for(size_t i=0;i<retained.size();++i)if(i%4!=3)retained[i]=XMConvertFloatToHalf(std::max(0.f,XMConvertHalfToFloat(retained[i])));if(!PostPresentQuality::CheckResult(retained,actual,true)){std::cerr<<"FAIL: unsigned original identity scale="<<scale<<" apply="<<apply<<'\n';++identityFailures;}}
 if(signedOriginal && edited && apply && scale==1){auto* v=(const HALF*)actual.data();std::cout<<"FP16 boundary red="<<XMConvertHalfToFloat(v[126*4])<<" green="<<XMConvertHalfToFloat(v[126*4+1])<<"\n";}
 for(size_t i=0;i<pixels.size();++i){HALF h;memcpy(&h,actual.data()+i*2,2);if(signedOriginal)assert(std::isfinite(XMConvertHalfToFloat(h)));if(i%4==3)assert(h==pixels[i]);}
 }
 w.clean();std::cout<<"Composition identity failures="<<identityFailures<<'\n';return identityFailures?1:0;}
