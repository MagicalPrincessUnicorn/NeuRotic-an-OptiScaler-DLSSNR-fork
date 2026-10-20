#include "nr_color_warp_fixture.h"
static double pq(double n){const double p=pow(n/10000.0,2610.0/16384);return pow((3424.0/4096+2413.0/128*p)/(1+2392.0/128*p),2523.0/32);}
static double nits(double v){const double p=pow(v,32.0/2523);return 10000*pow(std::max(p-3424.0/4096,0.0)/(2413.0/128-2392.0/128*p),16384.0/2610);}
int main(){std::cout.setf(std::ios::unitbuf);Warp w;std::vector<uint32_t> pixels;
 for(double n:{0.,80.,200.,1000.,10000.}){auto v=(uint32_t)std::lround(pq(n)*1023);for(UINT a=0;a<4;++a)pixels.push_back(v|(v<<10)|(v<<20)|(a<<30));}
 for(UINT v=0;v<1024;++v){UINT a=v%4;pixels.push_back(v|(v<<10)|(v<<20)|(a<<30));pixels.push_back(v|(a<<30));pixels.push_back((v<<10)|(a<<30));pixels.push_back((v<<20)|(a<<30));}
 const UINT width=(UINT)pixels.size();auto in=w.texture(DXGI_FORMAT_R10G10B10A2_UNORM,width,1,D3D12_RESOURCE_STATE_COPY_DEST);auto linear=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,width,1,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto out=w.texture(DXGI_FORMAT_R10G10B10A2_UNORM,width,1,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);w.upload(in.Get(),pixels.data(),4);
 FT_Dx12 decode("PQ decode",w.d.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,FT_Dx12::Transfer::Pq2020ToScRgb);
 FT_Dx12 encode("PQ encode",w.d.Get(),DXGI_FORMAT_R10G10B10A2_UNORM,FT_Dx12::Transfer::ScRgbToPq2020);
 assert(decode.Ready()&&encode.Ready());assert(decode.BindImmutableDescriptors(in.Get(),linear.Get()));assert(encode.BindImmutableDescriptors(linear.Get(),out.Get(),in.Get()));
 w.run([&](auto* l){assert(decode.Dispatch(l,in.Get(),linear.Get()));});auto decoded=w.read(linear.Get(),8);
 double maxAbs=0,sumAbs=0;int maxUlp=0;const double m[3][3]={{1.660491002108434,-.58764113878855,-.072849863319884},{-.124550474521591,1.13289989712596,-.008349422604369},{-.018150763354905,-.100578898008007,1.118729661362913}};
 for(UINT i=0;i<width;++i){auto* h=(const HALF*)(decoded.data()+i*8);double v[3];for(int c=0;c<3;++c)v[c]=nits(((pixels[i]>>(10*c))&1023)/1023.0)/80;
 for(int c=0;c<3;++c){double ref=0;for(int k=0;k<3;++k)ref+=m[c][k]*v[k];float actual=XMConvertHalfToFloat(h[c]);assert(std::isfinite(actual));HALF expected=XMConvertFloatToHalf((float)ref);int ulp=std::abs(int(h[c])-int(expected));maxUlp=std::max(maxUlp,ulp);maxAbs=std::max(maxAbs,std::abs(actual-ref));sumAbs+=std::abs(actual-ref);}
 assert(h[3]==XMConvertFloatToHalf(float(pixels[i]>>30)/3));}
 std::cout<<"DECODE maxHalfULP="<<maxUlp<<" maxAbs="<<maxAbs<<" meanAbs="<<sumAbs/(width*3)<<'\n';if(maxUlp>2){std::cerr<<"FAIL: PQ conversion reference mismatch\n";return 1;}
 w.run([&](auto* l){Warp::barrier(l,linear.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);assert(encode.Dispatch(l,linear.Get(),out.Get()));});auto encoded=w.read(out.Get(),4);int maxCode=0;
 for(UINT i=0;i<width;++i){uint32_t actual;memcpy(&actual,encoded.data()+i*4,4);assert((actual>>30)==(pixels[i]>>30));for(int c=0;c<3;++c)maxCode=std::max(maxCode,std::abs(int((actual>>(c*10))&1023)-int((pixels[i]>>(c*10))&1023)));}
 std::cout<<"ROUNDTRIP max10bitCodeError="<<maxCode<<'\n';assert(maxCode<=1);
 // Controlled signed FP16 edit: validate inverse against independent double reference.
 std::vector<HALF> edits(width*4);memcpy(edits.data(),decoded.data(),decoded.size());
 for(UINT i=0;i<width;++i)for(int c=0;c<3;++c)edits[i*4+c]=XMConvertFloatToHalf(XMConvertHalfToFloat(edits[i*4+c])*1.01f);
 auto edited=w.texture(DXGI_FORMAT_R16G16B16A16_FLOAT,width,1,D3D12_RESOURCE_STATE_COPY_DEST);w.upload(edited.Get(),edits.data(),8);
 FT_Dx12 editedEncode("edited PQ",w.d.Get(),DXGI_FORMAT_R10G10B10A2_UNORM,FT_Dx12::Transfer::ScRgbToPq2020);
 assert(!editedEncode.BindImmutableDescriptors(edited.Get(),out.Get()));
 assert(editedEncode.BindImmutableDescriptors(edited.Get(),out.Get(),in.Get()));
 assert(!editedEncode.BindImmutableDescriptors(linear.Get(),out.Get(),in.Get()));
 w.run([&](auto* l){assert(editedEncode.Dispatch(l,edited.Get(),out.Get()));});auto editedResult=w.read(out.Get(),4);
 const double inv[3][3]={{.627403895934699,.329283038377884,.043313065687417},{.069097289358232,.919540395075459,.011362315566309},{.016391438875150,.088013307877226,.895595253247624}};
 int editError=0;
 for(UINT i=0;i<width;++i){uint32_t actual;memcpy(&actual,editedResult.data()+i*4,4);assert((actual>>30)==(pixels[i]>>30));auto* h=(const HALF*)(decoded.data()+i*8);
 for(int c=0;c<3;++c){double v=nits(((pixels[i]>>(10*c))&1023)/1023.0)/80;
 for(int k=0;k<3;++k)v+=inv[c][k]*(XMConvertHalfToFloat(edits[i*4+k])-XMConvertHalfToFloat(h[k]));
 int ref=(int)std::lround(pq(std::clamp(v*80,0.0,10000.0))*1023);editError=std::max(editError,std::abs(ref-int((actual>>(10*c))&1023)));}}
 std::cout<<"EDIT max10bitCodeError="<<editError<<'\n';assert(editError<=1);w.clean();}
