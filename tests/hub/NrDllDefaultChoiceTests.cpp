#include <install/NrDllInstallChoice.h>
#include <iostream>
#include <stdexcept>
using namespace nh;
static unsigned checks=0;
static void Check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>static void Throws(F&& f,NrDllErrorCode code){try{f();}catch(const NrDllChoiceError& e){Check(e.code==code,"typed choice error");return;}throw std::runtime_error("missing choice error");}
int main(){try{
 NrDllProfile a{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","User profile A",std::string(64,'a'),L"C:/fixture/profiles/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/nvngx_dlssnr.dll",123,true};
 NrDllProfile b{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","User profile B",std::string(64,'b'),L"C:/fixture/profiles/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/nvngx_dlssnr.dll",456,true};
 NrDllCatalog catalog{{a,b},a.id,{}};
 auto chosen=ResolveNrDllChoice({NrDllChoiceMode::Default,{}},catalog,NrDllTargetState::Initial);
 Check(chosen.mode==NrDllCopyMode::Profile&&chosen.profile.id==a.id,"initial default selects its exact profile");
 const auto frozen=NrDllCopyRequest(chosen);
 catalog.defaultId=b.id;catalog.profiles[0].name="renamed after submit";
 Check(NrDllCopyRequest(chosen)==frozen&&frozen.at("profileName")==a.name,"queued identity and label do not follow default or rename");
 chosen=ResolveNrDllChoice({NrDllChoiceMode::Profile,a.id},catalog,NrDllTargetState::Initial);
 Check(chosen.profile.id==a.id,"saved explicit choice ignores changed default");
 catalog.profiles.erase(catalog.profiles.begin());
 Throws([&]{ResolveNrDllChoice({NrDllChoiceMode::Profile,a.id},catalog,NrDllTargetState::Initial);},NrDllErrorCode::MissingProfile);
 catalog.defaultId=a.id;
 Throws([&]{ResolveNrDllChoice({NrDllChoiceMode::Default,{}},catalog,NrDllTargetState::Initial);},NrDllErrorCode::MissingProfile);
 auto manual=ResolveNrDllChoice({NrDllChoiceMode::Manual,{}},catalog,NrDllTargetState::Initial);
 Check(manual.mode==NrDllCopyMode::None&&NrDllCopyRequest(manual)==nlohmann::json{{"mode","none"}},"manual install needs no available profile");
 for(auto state:{NrDllTargetState::Existing,NrDllTargetState::Uncertain}){
  auto update=ResolveNrDllChoice({NrDllChoiceMode::Profile,"removed"},catalog,state);
  Check(update.mode==NrDllCopyMode::Preserve&&NrDllCopyRequest(update)==nlohmann::json{{"mode","preserve"}},"updates and unknown targets have no source dependency");
 }
 catalog.defaultId.clear();
 Check(ResolveNrDllChoice({NrDllChoiceMode::Default,{}},catalog,NrDllTargetState::Initial).mode==NrDllCopyMode::None,"no configured default does not choose first profile");
 catalog.profiles={a,b};catalog.defaultId=a.id;catalog.profiles[0].available=false;
 Throws([&]{ResolveNrDllChoice({NrDllChoiceMode::Default,{}},catalog,NrDllTargetState::Initial);},NrDllErrorCode::MissingProfile);
 catalog.profiles[0]=a;
 auto current=ResolveNrDllChoice({NrDllChoiceMode::Profile,a.id},catalog,NrDllTargetState::Initial);
 NrDllSelection acquired{a,{}};
 Check(NrDllAcquisitionMatches(current,acquired),"selected asset metadata matches");
 acquired.profile.name="renamed after submission";Check(NrDllAcquisitionMatches(current,acquired),"rename does not change selected bytes");
 acquired.profile.sha256=std::string(64,'b');Check(!NrDllAcquisitionMatches(current,acquired),"changed selected bytes refused");
 acquired.profile=a;acquired.profile.id=b.id;Check(!NrDllAcquisitionMatches(current,acquired),"same bytes under another profile are not a fallback");
 acquired.profile=a;acquired.profile.dllPath=b.dllPath;Check(!NrDllAcquisitionMatches(current,acquired),"source path cannot silently move");
 acquired.profile=a;acquired.profile.bytes++;Check(!NrDllAcquisitionMatches(current,acquired),"byte count continuity preserved");
 auto huge=current;huge.profile.bytes=9007199254740993ULL;auto encoded=NrDllCopyRequest(huge);
 Check(encoded.at("bytes").is_number_unsigned()&&nlohmann::json::parse(encoded.dump()).at("bytes").get<std::uint64_t>()==huge.profile.bytes,"queued uint64 identity never rounds through float");
 auto empty=current;empty.profile.bytes=0;Check(NrDllCopyRequest(empty).at("bytes")==0,"queue codec adds no size admission rule");
 auto named=current;named.profile.name="\xe6\x80\xa7\xe8\x83\xbd \xe9\xab\x98";
 Check(NrDllCopyRequest(named).at("profileName")==named.profile.name,"UTF-8 user labels remain data");
 Check(NrDllCopyRequest(current).size()==6&&NrDllCopyRequest(current).at("source").get<std::string>().ends_with("nvngx_dlssnr.dll"),"exact selected source contract fields");
 NrDllCatalog badDefault{{a,b},{},{{{},NrDllErrorCode::CorruptMetadata}}};
 bool defaultRejected=false;try{ResolveNrDllChoice({NrDllChoiceMode::Default,{}},badDefault,NrDllTargetState::Initial);}catch(const NrDllChoiceError& e){defaultRejected=e.code==NrDllErrorCode::CorruptMetadata;}
 Check(defaultRejected,"corrupt default must not become manual no-copy");
 Check(ResolveNrDllChoice({NrDllChoiceMode::Profile,b.id},badDefault,NrDllTargetState::Initial).profile.id==b.id,"explicit profile is independent of default metadata error");
 Check(ResolveNrDllChoice({NrDllChoiceMode::Manual,{}},badDefault,NrDllTargetState::Initial).mode==NrDllCopyMode::None,"manual remains explicit when default metadata is unreadable");
 for(auto target:{NrDllTargetState::Existing,NrDllTargetState::Uncertain})Check(ResolveNrDllChoice({NrDllChoiceMode::Default,{}},badDefault,target).mode==NrDllCopyMode::Preserve,"ordinary update is independent of default metadata error");
 badDefault.defaultId=a.id;badDefault.issues={{b.id,NrDllErrorCode::ChangedAsset}};
 Check(ResolveNrDllChoice({NrDllChoiceMode::Default,{}},badDefault,NrDllTargetState::Initial).profile.id==a.id,"an unrelated unavailable profile does not block a valid default");
 std::cout<<checks<<" immutable NR DLL choice checks PASS; pure request metadata, no file/provider load\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
