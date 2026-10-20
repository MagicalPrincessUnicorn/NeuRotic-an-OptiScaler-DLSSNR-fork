// GPL-3.0. HDR container/pixel-format handling adapted from Win32CaptureSample,
// Robert Mikhayelyan, MIT, commit 49fefe79fd9b11025f0b5eb91783a98888516070,
// Win32CaptureSample/App.cpp. See Licenses/Win32CaptureSample-LICENSE.txt.
// NeuRotic retains its owned readback, file namespace and publication contracts;
// exact FP16 format and lossless options replace the sample's conversion fallback.
#pragma once
#include "SnapshotFile.h"
#include "WorkerColor.h"
#include <ocidl.h>
#include <oleauto.h>
#include <sstream>
#pragma comment(lib,"oleaut32.lib")
namespace nrw {
inline void HdrSnapshotCheck(HRESULT hr,const char* phase){
    if(FAILED(hr)){std::ostringstream text;text<<"HDR JPEG XR "<<phase<<" failed (HRESULT 0x"<<std::hex<<uint32_t(hr)<<')';throw std::runtime_error(text.str());}
}
inline bool HdrSnapshotPixelFormat(const GUID& format){return format==GUID_WICPixelFormat64bppRGBAHalf;}
template<class Write>void WriteSnapshotHdrJxr(const void* pixels,uint32_t width,uint32_t height,uint32_t pitch,Write write){
    const auto bytes=color::FrameBytes(width,height,color::Storage::Rgba16Float);
    if(!pixels||!bytes||uint64_t(width)*height>67108864||uint64_t(width)*8>UINT32_MAX||pitch<uint64_t(width)*8)
        throw std::runtime_error("HDR snapshot dimensions or FP16 row pitch are unsupported");
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;HdrSnapshotCheck(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IStream> stream;HdrSnapshotCheck(CreateStreamOnHGlobal(nullptr,TRUE,&stream),"owned stream");
    ComPtr<IWICBitmapEncoder> encoder;HdrSnapshotCheck(factory->CreateEncoder(GUID_ContainerFormatWmp,nullptr,&encoder),"codec creation");
    HdrSnapshotCheck(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"encoder initialization");
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;
    HdrSnapshotCheck(encoder->CreateNewFrame(&frame,&properties),"frame creation");
    if(!properties)throw std::runtime_error("HDR JPEG XR codec did not expose lossless encoding options");
    PROPBAG2 names[9]{};VARIANT values[9]{};
    const wchar_t* keys[]={L"Lossless",L"UseCodecOptions",L"Quality",L"AlphaQuality",L"Subsampling",L"Overlap",L"InterleavedAlpha",L"CompressedDomainTranscode",L"ImageQuality"};
    for(unsigned i=0;i<9;++i){names[i].pstrName=const_cast<wchar_t*>(keys[i]);VariantInit(&values[i]);}
    for(unsigned i:{0u,1u,6u,7u}){values[i].vt=VT_BOOL;values[i].boolVal=i<2?VARIANT_TRUE:VARIANT_FALSE;}
    for(unsigned i:{2u,3u,4u,5u}){values[i].vt=VT_UI1;values[i].bVal=i==4?3:i==5?0:1;}
    values[8].vt=VT_R4;values[8].fltVal=1.f;
    HdrSnapshotCheck(properties->Write(9,names,values),"lossless/alpha options");
    HdrSnapshotCheck(frame->Initialize(properties.Get()),"frame initialization");
    HdrSnapshotCheck(frame->SetSize(width,height),"frame extent");
    auto format=GUID_WICPixelFormat64bppRGBAHalf;HdrSnapshotCheck(frame->SetPixelFormat(&format),"FP16 pixel format");
    if(!HdrSnapshotPixelFormat(format))throw std::runtime_error("HDR JPEG XR codec cannot preserve RGBA FP16 pixels; format substitution refused");
    // Native WIC floating-point RGB is scRGB. Do not install an sRGB ICC profile,
    // premultiply, normalize, clamp or tone-map the actual captured channel values.
    const uint32_t rowBytes=width*8;
    for(uint32_t row=0;row<height;++row)
        HdrSnapshotCheck(frame->WritePixels(1,rowBytes,rowBytes,const_cast<BYTE*>(static_cast<const BYTE*>(pixels)+size_t(row)*pitch)),"FP16 row encoding");
    HdrSnapshotCheck(frame->Commit(),"frame commit");HdrSnapshotCheck(encoder->Commit(),"encoder commit");
    STATSTG stat{};HdrSnapshotCheck(stream->Stat(&stat,STATFLAG_NONAME),"stream size");
    if(stat.cbSize.QuadPart>UINT32_MAX)throw std::runtime_error("HDR JPEG XR snapshot is too large");
    LARGE_INTEGER begin{};HdrSnapshotCheck(stream->Seek(begin,STREAM_SEEK_SET,nullptr),"stream rewind");
    BYTE chunk[65536];for(ULONGLONG remaining=stat.cbSize.QuadPart;remaining;){ULONG read=0;const ULONG count=ULONG(std::min<ULONGLONG>(remaining,sizeof(chunk)));
        HdrSnapshotCheck(stream->Read(chunk,count,&read),"stream read");if(read!=count)throw std::runtime_error("HDR JPEG XR snapshot stream ended early");write(chunk,read);remaining-=read;}
}
inline std::wstring SaveSnapshotHdrJxr(const void* pixels,uint32_t width,uint32_t height,uint32_t pitch,const color::Representation& representation){
    if(!color::Hdr(representation))throw std::runtime_error("HDR snapshot requires the actual negotiated scRGB FP16 representation");
    SnapshotFolderLease lease;
    return PublishSnapshot(lease.path,[&](auto write){WriteSnapshotHdrJxr(pixels,width,height,pitch,write);},L".jxr");
}
}
