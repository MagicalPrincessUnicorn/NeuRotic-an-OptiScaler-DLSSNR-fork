#pragma once
#include "ObservationPublisher.h"
#include <array>

namespace Neurotic::Feed
{
// NGX key spellings are from the checked-out SDK. The caller supplies its success enum.
// Reads occur only in an explicitly active observation scope; no Set/feature/model call exists here.
template<class T,class Parameters,class Result>
std::optional<T> ReadNgx(Parameters* parameters,const char* key,Result success)
{
    T value{};if(!parameters||parameters->Get(key,&value)!=success)return {};
    if constexpr(std::is_floating_point_v<T>)if(!std::isfinite(value))return {};
    return value;
}
template<class T>void Observed(Callback& callback,std::string_view field,const std::optional<T>& value)noexcept
{if(value)callback.Value(field,*value);else callback.Missing(field);}
template<class Parameters,class Result>void ObserveNgxEvaluation(Callback& callback,Parameters* p,Result success)noexcept
{
    if(!callback.Active())return;
    try
    {
        for(const char* key:{"DLSS.Feature.Create.Flags","Width","Height","OutWidth","OutHeight","PerfQualityValue",
            "Reset","DLSS.Render.Subrect.Dimensions.Width","DLSS.Render.Subrect.Dimensions.Height",
            "DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y",
            "DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y",
            "DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y"})
            Observed(callback,key,ReadNgx<unsigned>(p,key,success));
        const auto x=ReadNgx<float>(p,"Jitter.Offset.X",success),y=ReadNgx<float>(p,"Jitter.Offset.Y",success);
        Observed(callback,"Jitter.Offset.X",x);Observed(callback,"Jitter.Offset.Y",y);
        for(const char* key:{"MV.Scale.X","MV.Scale.Y","DLSS.Pre.Exposure","DLSS.Exposure.Scale"})
            Observed(callback,key,ReadNgx<float>(p,key,success));
        const auto resource=[&](const char* field,C::SemanticKind kind){
            const auto value=ReadNgx<void*>(p,field,success);callback.Resource(field,kind,value?*value:nullptr);};
        resource("Color",C::SemanticKind::Color);resource("Output",C::SemanticKind::Color);
        resource("Depth",C::SemanticKind::Depth);resource("MotionVectors",C::SemanticKind::Motion);
        resource("ExposureTexture",C::SemanticKind::Exposure);
        resource("DLSS.Input.Bias.Current.Color.Mask",C::SemanticKind::Mask);
        callback.Jitter(x&&y?C::OptionalFact<C::Vec2>::FromKnown({*x,*y},callback.Evidence()):C::OptionalFact<C::Vec2>{});
    }catch(...){} // observation failure never changes the caller's forwarding/result
}
class NgxCreationSnapshot
{
    inline static constexpr std::array<const char*,6> keys={"DLSS.Feature.Create.Flags","Width","Height","OutWidth","OutHeight","PerfQualityValue"};
    std::array<std::optional<unsigned>,6> values_{};
    bool captured_=false;
  public:
    template<class Parameters,class Result>NgxCreationSnapshot(Parameters* parameters,Result success)noexcept
    {
        if(!Observing())return;
        try{for(std::size_t i=0;i<keys.size();++i)values_[i]=ReadNgx<unsigned>(parameters,keys[i],success);captured_=true;}catch(...){}
    }
    void Publish(SourceDescriptor source,const void* createdSubject,std::uint64_t feature)const noexcept
    {
        if(!captured_||!createdSubject)return;
        Callback callback(source,createdSubject);callback.Value("feature",feature);
        for(std::size_t i=0;i<keys.size();++i)Observed(callback,keys[i],values_[i]);
    }
};
}
