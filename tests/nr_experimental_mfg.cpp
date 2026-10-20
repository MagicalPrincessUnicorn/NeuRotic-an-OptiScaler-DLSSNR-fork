#include "../OptiScaler/mfg/ExperimentalMfgPolicy.h"
#include <cassert>
#include <cstdio>
using namespace Neurotic::Mfg::Experimental;
int main()
{
    Renderer r{true, 0x10de, 0x170, 2, 0x2206, 17, true};
    Preferences p{true, true};
    assert(Select(p,r,false,false)==Family::Rtx30);
    assert(Select({},r,false,false)==Family::None);
    assert(Select(p,r,true,false)==Family::None);
    assert(Select(p,r,false,true)==Family::None);
    r.architecture=0x190;
    assert(Select(p,r,false,false)==Family::None);
    r.architecture=0x170; r.implementation=0;
    assert(Select(p,r,false,false)==Family::None);
    r={true,0x10de,0x160,2,0x1e84,17,true};
    assert(Select(p,r,false,false)==Family::Rtx20);
    r.consumerRtx=false;assert(Select(p,r,false,false)==Family::None);r.consumerRtx=true;
    r.device=0x2182; // GTX 1660
    assert(Select(p,r,false,false)==Family::None);
    r.device=0x1e84;r.bound=false;
    assert(Select(p,r,false,false)==Family::None);
    r.bound=true;r.vendor=0x1002;
    assert(Select(p,r,false,false)==Family::None);
    SessionPreferences prefs;
    prefs.Begin({true,false});
    prefs.Saved({false,true},false);
    assert(prefs.saved.rtx30 && prefs.session.rtx30);
    prefs.Saved({false,true},true);
    assert(!prefs.saved.rtx30 && prefs.saved.rtx20 && prefs.session.rtx30);
    prefs.Begin({false,true});
    assert(prefs.session.rtx30); // Never relatch on reload
    assert(CanEdit(false,Family::Rtx30,Family::Rtx30));
    assert(!CanEdit(false,Family::Rtx20,Family::Rtx30));
    assert(CanEdit(true,Family::Rtx20,Family::None));
    Capability c{true,true,5,3};
    assert(Decide({Mode::Fixed,5},c).reason==Reason::StructuralLimit);
    assert(Decide({Mode::Fixed,3},c).overrideRequest);
    c.ceiling=5;
    assert(Decide({Mode::Fixed,5},c).generated==5);
    for(int n : {-1,0,6,2147483647}) assert(!Decide({Mode::Fixed,n},c).overrideRequest);
    assert(!Decide({Mode::Dynamic,2},c).overrideRequest);
    assert(Decide({Mode::Off,0},c).off);
    assert(!Decide({},c).overrideRequest);
    c.ceiling.reset();assert(!Decide({Mode::Fixed,2},c).overrideRequest);
    c.ceiling=3;c.abi=99;assert(!Decide({Mode::Fixed,2},c).overrideRequest);
    c.abi=5;c.ready=false;assert(!Decide({Mode::Fixed,2},c).overrideRequest);
    std::puts("Experimental MFG policy: all checks passed");
}
