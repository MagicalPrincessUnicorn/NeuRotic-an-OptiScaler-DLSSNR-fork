#include "dlssnr/NativeGuideRoute.h"
#include "dlssnr/connections/ConnectionPolicy.h"
#include "dlssnr/NrInputRestart.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
using namespace DlssNr;
int main(int argc,char** argv) {
 const int source=argc>1?std::atoi(argv[1]):0;
 const int transport=argc>2?std::atoi(argv[2]):0;
 const bool fallback=argc>3?std::atoi(argv[3])!=0:true;
 assert(!DlssNr::InputRestartRequired({source,transport,false},{source,transport,true}));
 NativeGuides::ConfigureInput(source,transport,fallback);
 assert(NativeGuides::SelectedSource()==source);
#ifdef _WIN64
 assert(NativeGuides::ObserveBuiltIn()==(source==0||source==2));
#else
 assert(!NativeGuides::ObserveBuiltIn());
#endif
 // Discovery does not claim ownership or silently change the saved source.
 Connections::PolicyWire p;
 assert(NeuRotic_QueryConnectionPolicyV1(&p,sizeof(p))&&p.claimedSource==0);
 Connections::SetNativeUsable(true);
 if(source==0)assert(!NeuRotic_ClaimPreparedConnectionV1(2,1,101));
 Connections::SetNativeUsable(false);
 const auto selected=transport==2?2u:1u;
 const bool allowed=source==0||source==2;
 assert(bool(NeuRotic_ClaimPreparedConnectionV1(2,selected,101))==allowed);
 if(allowed) {
  assert(!NeuRotic_ClaimPreparedConnectionV1(2,selected,102));
  assert(!Connections::ReserveNativeOwner(103));
  assert(!NeuRotic_RetirePreparedConnectionV1(2,102,1));
  assert(NeuRotic_RetirePreparedConnectionV1(2,101,1));
  assert(NeuRotic_QueryConnectionPolicyV1(&p,sizeof(p))&&p.claimedSource==0);
  if(transport==0&&!fallback)assert(!NeuRotic_ClaimPreparedConnectionV1(2,2,101));
  assert(NeuRotic_ClaimPreparedConnectionV1(2,selected,101));
  assert(!NeuRotic_RetirePreparedConnectionV1(2,101,0));
  assert(!NeuRotic_RetirePreparedConnectionV1(2,101,1));
  assert(!Connections::ReserveNativeOwner(103));
 }
 NativeGuides::ConfigureInput(source==0?2:0,2,false);
 assert(NativeGuides::SelectedSource()==source&&NativeGuides::SelectedTransport()==transport);
 NativeGuides::ConfigureCpuFallback(true);assert(NativeGuides::SelectedCpuFallback());
 NativeGuides::ConfigureCpuFallback(false);assert(!NativeGuides::SelectedCpuFallback());
 std::puts("PASS source preference, native priority, competing owners, clean/unsafe retirement and transport policy");
}
