#pragma once
#include "NrAlternateFrameInputs.h"
#include "NativeTemporalInputs.h"
#include "NativeNgxCallCapture.h"
#include <chrono>

namespace DlssNr::AlternateFrame {
// Value projection from the original outer NGX observation. No new identity,
// rights, resource states, provider success or GPU completion is issued here.
inline NativeSourceView ObserveNative(const NativeTemporalInputs::Metadata* metadata,
    ID3D12GraphicsCommandList* list,ID3D12Resource* output,ID3D12Resource* depth,ID3D12Resource* motion)
{
    NativeSourceView v;v.d3d12=true;
    if(!metadata||!metadata->alternateOriginal)return v;
    const auto& m=*metadata;const auto& original=*m.alternateOriginal;
    v.source=m.alternateSource.source;v.predecessor=m.alternateSource.predecessor;
    v.identityObserved=Valid(v.source);v.scope=m.generation;
    v.timeUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    v.clockKnown=true;
    if(original.CommandList()!=list||original.Resource("Output")!=output||original.Resource("Depth")!=depth||
       original.Resource("MotionVectors")!=motion||!output||!depth||!motion)return v;
    const auto out=output->GetDesc(),z=depth->GetDesc(),mv=motion->GetDesc();
    v.width=static_cast<unsigned>(out.Width);v.height=out.Height;v.workWidth=v.width;v.workHeight=v.height;
    v.samples=out.SampleDesc.Count;v.rgba16f=out.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||out.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS;
    v.rgb11f=out.Format==DXGI_FORMAT_R11G11B10_FLOAT;
    // Schema: IsHDR is scene-linear; depth is device-normalized; NGX vectors
    // point from current to previous and their declared scale produces pixels.
    if(m.flags&1u)v.linearDomain=m.generation;
    v.scale.domain=v.linearDomain;
    v.includesJitter=(m.flags&4u)!=0;v.depthInverted=(m.flags&8u)!=0;v.guidesJittered=true;
    unsigned width=0,height=0,dx=0,dy=0,mx=0,my=0,ox=0,oy=0,reset=0;
    const bool raster=original.Get("DLSS.Render.Subrect.Dimensions.Width",&width)==0&&
        original.Get("DLSS.Render.Subrect.Dimensions.Height",&height)==0&&width&&height;
    // NGX's omitted base coordinates are schema defaults, never observations.
    original.Get("DLSS.Input.Depth.Subrect.Base.X",&dx);original.Get("DLSS.Input.Depth.Subrect.Base.Y",&dy);
    original.Get("DLSS.Input.MV.Subrect.Base.X",&mx);original.Get("DLSS.Input.MV.Subrect.Base.Y",&my);
    original.Get("DLSS.Output.Subrect.Base.X",&ox);original.Get("DLSS.Output.Subrect.Base.Y",&oy);
    original.Get("Reset",&reset);v.resetDue=reset!=0;
    if(!raster||ox||oy||m.outputWidth!=v.width||m.outputHeight!=v.height)return v;
    v.depth={dx,dy,width,height,static_cast<unsigned>(z.Width),z.Height};
    v.motion={mx,my,(m.flags&2u)?width:m.outputWidth,(m.flags&2u)?height:m.outputHeight,
        static_cast<unsigned>(mv.Width),mv.Height};
    float sx=0,sy=0,jx=0,jy=0,pre=0;
    if(original.GetOriginalFloat("MV.Scale.X",&sx)||original.GetOriginalFloat("MV.Scale.Y",&sy)||
       original.GetOriginalFloat("Jitter.Offset.X",&jx)||original.GetOriginalFloat("Jitter.Offset.Y",&jy))return v;
    v.motionToSceneUv={sx/v.motion.width,sy/v.motion.height};v.jitterUv={jx/width,jy/height};
    if(original.GetOriginalFloat("DLSS.Pre.Exposure",&pre)==0)v.scale.scale=pre;
    v.motionConvention=v.depthEncoding=Provenance::SchemaDefined;v.originalMetadata=true;
    v.currentAccess=true; // provisional; renderer still requires actual LocalRecordingAction
    v.postSr=true;return v;
}
}
