#pragma once
#include <array>

namespace Neurotic::Feed
{
// Declaration is not authentication. Even a partial/unknown feeder declaration
// must never fall through to the engine-native certificate path.
inline constexpr std::array PreparedNgxKeys={"DFC.Feeder.ContractVersion","DFC.Feeder.ProviderId",
    "DFC.Feeder.HostMode","DFC.Feeder.EvaluateCadence","NR.Prepared.ContractVersion"};
template<class Parameters,class Success>bool DeclaresPreparedNgxSource(Parameters* p,Success success)noexcept
{
    if(!p)return false;
    try{for(const auto* key:PreparedNgxKeys){unsigned value=0;if(p->Get(key,&value)==success)return true;}}
    catch(...){return true;}
    return false;
}
}
