#pragma once
#include <inputs/universal_feeder/PreparedGuideContract.h>
#include <d3d12.h>

namespace DlssNr::PreparedGuides
{
inline constexpr unsigned AbiVersion=1;
enum class Result : unsigned { Refused=0, Recorded=1, UnsupportedMask=2, InvalidDescriptor=3,
    DeviceMismatch=4, ResourceMismatch=5, RecordingUnavailable=6, Disabled=7,
    // Known lifecycle preparation (creation, retirement, or its pending/successful
    // release gate): no composed output or release proof. Retry on a fresh capture
    // only after this recording is sealed and GPU-completed. Failed release refuses.
    Preparing=8 };
inline const char* ResultName(Result r) noexcept {
 switch(r){case Result::Refused:return "Refused";case Result::Recorded:return "Recorded";
 case Result::UnsupportedMask:return "UnsupportedMask";case Result::InvalidDescriptor:return "InvalidDescriptor";
 case Result::DeviceMismatch:return "DeviceMismatch";case Result::ResourceMismatch:return "ResourceMismatch";
 case Result::RecordingUnavailable:return "RecordingUnavailable";case Result::Disabled:return "Disabled";
 case Result::Preparing:return "Preparing";default:return "Unknown";}
}
// Optional, same-thread result detail; copies text into the caller's buffer.
using ReasonFn=unsigned(__cdecl*)(char*,unsigned);
// In-process ABI only. A helper sends Descriptor and duplicated handles, never
// this pointer-bearing object. Caller owns a private DIRECT list and all images;
// color arrives/leaves UAV, guides NON_PIXEL_SHADER_RESOURCE. Record is neither
// submit nor completion and must not be reported as displayed output.
struct Dispatch
{
    unsigned size=sizeof(Dispatch),version=AbiVersion;
    Neurotic::Feed::Prepared::Descriptor source;
    ID3D12GraphicsCommandList* list=nullptr;
    ID3D12CommandQueue* queue=nullptr;
    ID3D12Resource *color=nullptr,*depth=nullptr,*motion=nullptr,*distrust=nullptr;
    unsigned workWidth=0,workHeight=0;
    unsigned requireModelDistrust=0,reserved=0;
};
using PrepareFn=unsigned(__cdecl*)(ID3D12GraphicsCommandList*);
using RecordFn=unsigned(__cdecl*)(const Dispatch*);
// Prepare installs the existing recording observers before caller Reset.
// Caller must Seal after its accepted submission; completion remains GPU-owned.
using SealFn=unsigned(__cdecl*)(ID3D12GraphicsCommandList*);
}
