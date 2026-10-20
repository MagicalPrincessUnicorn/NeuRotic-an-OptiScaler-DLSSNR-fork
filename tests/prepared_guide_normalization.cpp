#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <string_view>
#include "../OptiScaler/inputs/universal_feeder/PreparedNgxOrigin.h"
#if __has_include("../OptiScaler/nr/context/PreparedGuideNormalization.h")
#include "../OptiScaler/nr/context/PreparedGuideNormalization.h"
#define PREPARED_IMPLEMENTED
#endif
static void Check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
int main()
{
    struct Params { const char* present=nullptr;int Get(const char* key,unsigned* value){
        if(present&&std::string_view(key)==present){*value=99;return 0;}return 1;} } params;
    Check(!Neurotic::Feed::DeclaresPreparedNgxSource(&params,0),"ordinary NGX has no feeder declaration");
    for(const auto* key:Neurotic::Feed::PreparedNgxKeys){params.present=key;
        Check(Neurotic::Feed::DeclaresPreparedNgxSource(&params,0),"partial or unknown feeder tuple cannot fall through as native");}
#ifndef PREPARED_IMPLEMENTED
    Check(false,"prepared-source normalization is not implemented");
#else
    namespace P=Neurotic::Feed::Prepared;
    namespace C=Neurotic::Contracts;
    P::Descriptor d;
    d.producer=7;d.session=9;d.stream=11;d.device=13;d.generation=1;
    d.capture=2;d.previousCapture=1;
    d.color={1920,1080,0,0,1920,1080};d.depth=d.color;d.motion={240,135,0,0,240,135};
    d.motionGridWidth=8;d.motionGridHeight=8;
    d.motionUnits=C::MotionUnits::Pixels;
    d.colorDomain=C::ColorDomain::EncodedDisplay;
    auto n=Neurotic::Context::NormalizePreparedGuides(d);
    Check(bool(n),"block-grid source with pixel displacement accepted");
    Check(n.value->motionPixelScale.x==1 && n.value->motionPixelScale.y==1,"sample grid is not displacement units");
    d.motionUnits=C::MotionUnits::Blocks;
    n=Neurotic::Context::NormalizePreparedGuides(d);
    Check(bool(n)&&n.value->motionPixelScale.x==8,"block displacement explicitly converted");
    d.motionUnits=C::MotionUnits::ActiveRasterUV;
    n=Neurotic::Context::NormalizePreparedGuides(d);
    Check(bool(n)&&n.value->motionPixelScale.x==1920,"UV uses color raster, not flow texture size");
    d.motionDirection=C::MotionDirection::PreviousToCurrent;
    Check(!Neurotic::Context::NormalizePreparedGuides(d),"backward field cannot be negated into forward correspondence");
    d.motionDirection=C::MotionDirection::CurrentToPrevious;
    d.motionOrigin=C::SourceClass::Native;
    Check(!Neurotic::Context::NormalizePreparedGuides(d),"prepared declaration cannot self-certify native provenance");
    d.motionOrigin=C::SourceClass::Derived;
    d.previousCapture=d.capture;
    Check(!Neurotic::Context::NormalizePreparedGuides(d),"same capture is not temporal history");
    d.previousCapture=1;d.scaleX=std::numeric_limits<float>::quiet_NaN();
    Check(!Neurotic::Context::NormalizePreparedGuides(d),"nonfinite scale rejected");
    d.scaleX=1;d.depth.x=1;
    Check(!Neurotic::Context::NormalizePreparedGuides(d),"active rectangle overflow rejected");
    d.depth.x=0;d.motion.width=239;
    Check(!Neurotic::Context::NormalizePreparedGuides(d),"incomplete block-grid coverage rejected");
    d.motion.width=240;d.version=99;
    Check(!Neurotic::Context::NormalizePreparedGuides(d),"unknown version rejected");
    d.version=1;d.flags|=P::Reset;d.previousCapture=0;
    Check(bool(Neurotic::Context::NormalizePreparedGuides(d)),"explicit reset permits absent history");
    d.flags|=P::HasDistrust;d.distrust=d.color;d.maskCapture=1;
    Check(!Neurotic::Context::NormalizePreparedGuides(d),"stale mask rejected");
    d.maskCapture=2;
    Check(bool(Neurotic::Context::NormalizePreparedGuides(d)),"matched distrust accepted");
#ifdef NR_PREPARED_CONTINUITY_V1
    d.flags&=~P::Reset;auto next=d;next.capture=3;next.previousCapture=2;next.maskCapture=3;
    Check(Neurotic::Context::PreparedHistoryContinuous(d,next),"exact prepared history continues");
    next.device=99;
    Check(!Neurotic::Context::PreparedHistoryContinuous(d,next),"device identity breaks history");
    next=d;next.capture=4;next.previousCapture=3;
    Check(!Neurotic::Context::PreparedHistoryContinuous(d,next),"skipped consumer capture breaks history");
#else
    Check(false,"prepared consumer history isolation missing");
#endif
    std::puts("PASS: prepared semantic normalization");
#endif
}
