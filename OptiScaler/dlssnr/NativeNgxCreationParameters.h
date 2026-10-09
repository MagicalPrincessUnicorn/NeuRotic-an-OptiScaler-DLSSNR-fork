#pragma once
#include <array>
#include <optional>
#include <string_view>

namespace DlssNr
{
// Original creation values only. The successful feature registry incarnation
// retains this immutable copy; evaluation parameters never replace missing facts.
class NativeNgxCreationParameters
{
    inline static constexpr std::array keys={"DLSS.Feature.Create.Flags","Width","Height","OutWidth","OutHeight","PerfQualityValue"};
    std::array<std::optional<unsigned>,keys.size()> values_{};
    bool captured_=false;
  public:
    template<class Parameters,class Result>static NativeNgxCreationParameters Capture(Parameters* parameters,Result success)noexcept
    {
        try
        {
            NativeNgxCreationParameters result;
            if(!parameters)return result;
            for(std::size_t i=0;i<keys.size();++i)
            {unsigned value=0;if(parameters->Get(keys[i],&value)==success)result.values_[i]=value;}
            result.captured_=true;return result;
        }
        catch(...){return {};}
    }
    bool Captured()const noexcept{return captured_;}
    std::optional<unsigned> Value(std::string_view key)const noexcept
    {for(std::size_t i=0;i<keys.size();++i)if(key==keys[i])return values_[i];return {};}
    bool operator==(const NativeNgxCreationParameters&)const=default;
};
}
