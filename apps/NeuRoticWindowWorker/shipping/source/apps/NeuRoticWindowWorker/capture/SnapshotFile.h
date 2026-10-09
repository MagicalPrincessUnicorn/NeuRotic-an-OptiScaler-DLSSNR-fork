#pragma once
#include "../control/SnapshotLocation.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <wincodec.h>
#include <wrl/client.h>
#include <objidl.h>
#pragma comment(lib,"windowscodecs.lib")
namespace nrw {
template<class Write> void WriteSnapshotPng(const void* pixels,uint32_t width,uint32_t height,uint32_t pitch,Write write) {
 if(!pixels||!width||!height||uint64_t(width)*height>67108864||pitch<uint64_t(width)*4)throw std::runtime_error("Snapshot dimensions are unsupported");
 auto checked=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("PNG snapshot encoding failed");};
 using Microsoft::WRL::ComPtr;
 ComPtr<IWICImagingFactory> factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
 ComPtr<IStream> stream;checked(CreateStreamOnHGlobal(nullptr,TRUE,&stream));
 ComPtr<IWICBitmapEncoder> encoder;checked(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));checked(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
 ComPtr<IWICBitmapFrameEncode> frame;checked(encoder->CreateNewFrame(&frame,nullptr));checked(frame->Initialize(nullptr));checked(frame->SetSize(width,height));
 auto format=GUID_WICPixelFormat32bppBGRA;checked(frame->SetPixelFormat(&format));if(format!=GUID_WICPixelFormat32bppBGRA)throw std::runtime_error("PNG encoder cannot preserve BGRA pixels");
 // Write rows directly from the completed readback, excluding GPU pitch padding.
 for(uint32_t row=0;row<height;++row)checked(frame->WritePixels(1,width*4,width*4,const_cast<BYTE*>(static_cast<const BYTE*>(pixels)+size_t(row)*pitch)));
 checked(frame->Commit());checked(encoder->Commit());
 STATSTG stat{};checked(stream->Stat(&stat,STATFLAG_NONAME));if(stat.cbSize.QuadPart>UINT32_MAX)throw std::runtime_error("PNG snapshot is too large");
 LARGE_INTEGER begin{};checked(stream->Seek(begin,STREAM_SEEK_SET,nullptr));
 BYTE chunk[65536];for(ULONGLONG remaining=stat.cbSize.QuadPart;remaining;){ULONG read=0;const ULONG count=ULONG(std::min<ULONGLONG>(remaining,sizeof(chunk)));checked(stream->Read(chunk,count,&read));if(read!=count)throw std::runtime_error("PNG snapshot stream ended early");write(chunk,read);remaining-=read;}
}
// On-demand lossless BGRA snapshot. Exclusive file creation and pinned ordinary
// parents refuse link traversal; no caller-supplied output path or overwrite.
// Internal publication step; caller keeps the validated folder lease alive.
template<class Encode> std::wstring PublishSnapshot(const std::filesystem::path& folder,Encode encode) {
 GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Snapshot identifier unavailable");wchar_t suffix[40]{};StringFromGUID2(id,suffix,40);
 auto path=folder/(std::wstring(L"NeuRotic-")+suffix+L".png");
 auto temporary=folder/(std::wstring(L".neurotic-snapshot-")+suffix+L".pending");
 HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Snapshot file could not be created");
 struct Output{HANDLE h;bool saved=false;~Output(){if(!saved){FILE_DISPOSITION_INFO d{TRUE};SetFileInformationByHandle(h,FileDispositionInfo,&d,sizeof(d));}CloseHandle(h);}} output{file};
 auto write=[&](const void* data,DWORD bytes){DWORD written=0;if(!WriteFile(file,data,bytes,&written,nullptr)||written!=bytes)throw std::runtime_error("Snapshot write failed");};
 encode(write);
 if(!FlushFileBuffers(file))throw std::runtime_error("Snapshot flush failed");
 // A process interruption can leave a pending file, never a partial PNG advertised as a snapshot.
 auto name=path.wstring();const auto nameBytes=name.size()*sizeof(wchar_t);std::vector<unsigned char> renameBytes(sizeof(FILE_RENAME_INFO)+nameBytes);
 auto rename=reinterpret_cast<FILE_RENAME_INFO*>(renameBytes.data());rename->ReplaceIfExists=FALSE;rename->RootDirectory=nullptr;rename->FileNameLength=DWORD(nameBytes);memcpy(rename->FileName,name.data(),nameBytes);
 if(!SetFileInformationByHandle(file,FileRenameInfo,rename,DWORD(renameBytes.size())))throw std::runtime_error("Snapshot publication failed");
 output.saved=true;return path.wstring();
}
inline std::wstring SaveSnapshot(const void* pixels,uint32_t width,uint32_t height,uint32_t pitch) {
 if(!pixels||!width||!height||uint64_t(width)*height>67108864||pitch<uint64_t(width)*4)throw std::runtime_error("Snapshot dimensions are unsupported");
 SnapshotFolderLease lease;
 return PublishSnapshot(lease.path,[&](auto write){WriteSnapshotPng(pixels,width,height,pitch,write);});
}
}
