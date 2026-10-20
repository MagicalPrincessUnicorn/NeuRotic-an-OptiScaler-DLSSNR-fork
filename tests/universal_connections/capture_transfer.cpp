#include "dlssnr/connections/ConnectionSelection.h"
#ifndef NR_TRANSFER_BASELINE
#include "dlssnr/CaptureSettings.h"
#else
namespace DlssNr::CaptureSettings {
int TransferChoice(int transport,bool){return transport;}
struct Policy {int transport;bool fallback;};
Policy TransferPolicy(int choice){return {choice,true};}
}
#endif
#include <iostream>
int main(){
 using namespace DlssNr;using namespace Connections;unsigned cases=0;
 for(int t=0;t<3;++t)for(bool fallback:{false,true})for(bool gpu:{false,true})for(bool cpu:{false,true}){
  const auto normalized=CaptureSettings::TransferPolicy(CaptureSettings::TransferChoice(t,fallback));
  const RouteCapability routes[]={{Source::BuiltIn,true,true,true,gpu,cpu,true,true}};
  const auto old=Decide({Source::Automatic,Transport(t),fallback},routes,{});
  const auto now=Decide({Source::Automatic,Transport(normalized.transport),normalized.fallback},routes,{});
  if(old.usable!=now.usable||(old.usable&&(old.source!=now.source||old.transport!=now.transport))){std::cerr<<"FAIL transfer="<<t<<" fallback="<<fallback<<" GPU="<<gpu<<" CPU="<<cpu<<'\n';return 1;}
  ++cases;
 }
 if(CaptureSettings::TransferChoice(9,false)!=-1)return 2;
 std::cout<<"PASS "<<cases<<" legacy transfer combinations preserve effective selection; invalid settings remain visible\n";
}
