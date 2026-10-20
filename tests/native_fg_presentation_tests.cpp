#include "dlssnr/NativeFgPresentation.h"
#include <cassert>
#include <iostream>
int main(){using namespace DlssNr::NativeFg;PresentationObservation o;
 assert(o.Update(false,false,false,0,0,0)==PresentationState::Off);
 assert(o.Update(true,false,false,0,0,0)==PresentationState::RestartRequired);
 assert(o.Update(true,true,false,0,0,0)==PresentationState::Unavailable);
 assert(o.Update(true,true,true,1,0,1)==PresentationState::Waiting);
 assert(o.Update(true,true,true,1,1,1.1)==PresentationState::Generating);
 assert(o.Update(true,true,true,1,1,3)==PresentationState::Stale);
 assert(o.Update(true,true,true,2,20,3.1)==PresentationState::Stale);
 assert(o.Update(true,true,true,2,21,3.2)==PresentationState::Generating);
 assert(o.Update(true,true,false,2,21,3.3)==PresentationState::Unavailable);
 assert(o.Update(true,true,true,2,21,3.4)==PresentationState::Stale);
 assert(o.Update(true,true,true,2,1,3.5)==PresentationState::Stale);
 std::cout<<"PASS off, startup refusal, waiting, actual progress, expiry, owner recreation, route loss, counter reset\n";
}
