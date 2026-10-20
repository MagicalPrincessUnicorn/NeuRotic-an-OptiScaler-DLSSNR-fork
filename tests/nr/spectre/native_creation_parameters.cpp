#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#if __has_include(<dlssnr/NativeNgxCreationParameters.h>)
#include <dlssnr/NativeNgxCreationParameters.h>
#include <dlssnr/NativeFeatureRegistry.h>
struct Parameters
{
    static constexpr std::array names={"DLSS.Feature.Create.Flags","Width","Height","OutWidth","OutHeight","PerfQualityValue"};
    std::array<std::optional<unsigned>,6> values={2u,640u,360u,1280u,720u,1u};
    bool throws=false;unsigned reads=0;
    int Get(const char* key,unsigned* value)
    {
        ++reads;if(throws)throw std::runtime_error("creation parameter owner failed");
        for(std::size_t i=0;i<names.size();++i)if(names[i]==std::string_view(key))
        {if(!values[i])return 1;*value=*values[i];return 0;}return 1;
    }
};
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    using Capture=DlssNr::NativeNgxCreationParameters;
    Capture empty;check(!empty.Captured()&&!empty.Value("DLSS.Feature.Create.Flags"),"absent creation is not default zero flags");
    Parameters original;const auto captured=Capture::Capture(&original,0);
    check(captured.Captured()&&original.reads==6,"one bounded original creation read");
    bool exact=true;for(std::size_t i=0;i<Parameters::names.size();++i)exact=exact&&captured.Value(Parameters::names[i])==original.values[i];
    check(exact,"all six original creation facts preserved");
    original.values[0]=15u;original.values[3]=4096u;
    check(captured.Value("DLSS.Feature.Create.Flags")==2u&&captured.Value("OutWidth")==1280u,"later overrides cannot mutate creation facts");
    check(!captured.Value("unknown")&&!captured.Value("Reset"),"unobserved creation keys remain missing");
    original.values[0].reset();const auto missing=Capture::Capture(&original,0);
    check(missing.Captured()&&!missing.Value("DLSS.Feature.Create.Flags")&&missing.Value("OutWidth")==4096u,"missing creation flag stays missing");
    original.values[0]=0u;const auto zero=Capture::Capture(&original,0);
    check(zero.Value("DLSS.Feature.Create.Flags")==0u,"observed zero flag is known");
    original.throws=true;const auto failedRead=Capture::Capture(&original,0);
    check(!failedRead.Captured()&&!failedRead.Value("Width"),"exception never retains partial creation snapshot");
    check(!Capture::Capture(static_cast<Parameters*>(nullptr),0).Captured(),"null original parameter table remains unknown");
    DlssNr::NativeFeatureRegistry<unsigned> registry;registry.Set(7,11,captured);
    auto snapshot=registry.Read(7);auto pin=registry.Pin(7,snapshot);
    check(pin&&pin->Current()&&pin->Value().originalCreation==captured,"successful feature incarnation retains exact original creation");
    auto forged=snapshot;forged.originalCreation=zero;auto authentic=registry.Pin(7,forged);
    check(authentic&&authentic->Value().originalCreation==captured,"pin uses registry facts rather than caller substituted snapshot");
    registry.Set(7,11,zero);auto replacement=registry.Read(7);
    check(!pin->Current()&&replacement.generation!=snapshot.generation&&replacement.originalCreation==zero&&
          pin->Value().originalCreation==captured,"replacement separates original creation generations");
    check(!registry.Pin(7,snapshot),"stale generation cannot rebind current creation");
    registry.Set(9,11);check(!registry.Read(9).originalCreation.Captured(),"legacy registration does not invent creation facts");
    auto moved=std::move(*authentic);
    check(moved.Value().originalCreation==captured&&!moved.Current(),"moved pin preserves historical creation without currency");
    std::cout<<"Native creation parameters: "<<count<<" checks, "<<failed<<" failures; synthetic CPU parameter owner\n";
    return failed?1:0;
}
#else
int main(){std::cerr<<"FAIL original creation facts are not retained by feature incarnation\n";return 1;}
#endif
