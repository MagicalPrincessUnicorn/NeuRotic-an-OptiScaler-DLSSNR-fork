#include <iostream>
#include <dlssnr/NativeProviderLifetime.h>
#if __has_include(<dlssnr/Feature18HostContract.h>)
#include <dlssnr/Feature18HostContract.h>
int main(){
 using namespace DlssNr;int checks=0,failed=0;
 auto check=[&](bool ok,const char* text){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<text<<'\n';}};
 check(!ProviderLifetimeContract::NgxFeature18().Supported(),"unmatched route remains unavailable");
 check(!ProviderLifetimeContract::NgxFeature18("wrong adapter").Supported(),"unknown adapter refused");
 auto contract=ProviderLifetimeContract::NgxFeature18(Feature18HostContractIdentity);
 check(contract.Supported()&&contract.Identity()==Feature18HostContractIdentity,"generic selected-DLL host policy accepted");
 check(std::string_view(Feature18HostContractIdentity).find("sha256=")==std::string_view::npos,"host policy has no provider hash");
 check(std::string_view(Feature18HostContractIdentity).find("AlphaFeature18")==std::string_view::npos,"host policy has no provider build/cohort");
 FeatureReleaseAttempt release;int calls=0;
 auto success=[&]{++calls;return FeatureReleaseResult{sizeof(FeatureReleaseResult),FeatureReleaseDisposition::Returned,1,0};};
 check(!release.Try(false,success)&&calls==0,"release before drainage refuses without entering");
 check(release.Try(true,success)&&calls==1,"checked native success after drainage");
 check(release.Try(true,success)&&calls==1,"completed feature never double releases");
 FeatureReleaseAttempt failure;
 check(!failure.Try(true,[&]{++calls;return FeatureReleaseResult{sizeof(FeatureReleaseResult),FeatureReleaseDisposition::Returned,static_cast<int>(0xBAD00002u),0};}),"native failure preserved");
 check(failure.Result().nativeResult==static_cast<int>(0xBAD00002u),"exact native error");
 check(!failure.Try(true,success)&&calls==2,"failed release does not retry");
 FeatureReleaseAttempt exception;
 check(!exception.Try(true,[&]()->FeatureReleaseResult{++calls;throw 1;}),"exception remains uncertain");
 check(!exception.Try(true,success)&&calls==3,"exception cannot double-release");
 FeatureReleaseAttempt legacy;
 check(!legacy.Try(true,[]{return FeatureReleaseResult{};}),"legacy/absent checked result is not success");
 check(!legacy.Try(true,success)&&calls==3,"unavailable result cannot become success on retry");
 release={};
 check(release.Try(true,success)&&calls==4,"fresh feature generation performs its own checked release");
 std::cout<<"Feature18 host contract: "<<checks<<" checks, "<<failed<<" failures; CPU only\n";return failed?1:0;
}
#else
int main(){std::cerr<<"FAIL approved matched host contract and checked release missing\n";return 1;}
#endif
