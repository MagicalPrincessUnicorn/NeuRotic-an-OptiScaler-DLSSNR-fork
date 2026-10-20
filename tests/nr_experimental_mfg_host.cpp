#include "../OptiScaler/mfg/ExperimentalMfgSession.h"
#include <cassert>
#include <thread>
#include <cstdio>
using namespace Neurotic::Mfg::Experimental;
int main() {
    for(auto stage:{Stage::Off,Stage::Waiting,Stage::Preparing,Stage::Ready,Stage::ActivityObserved,Stage::Unavailable})
        assert(!CanReport(Stage::Poisoned,stage));
    assert(CanReport(Stage::Poisoned,Stage::Poisoned));
    assert(!MayPoisonDevice(Family::None,true));
    assert(!MayPoisonDevice(Family::Rtx30,false));
    assert(MayPoisonDevice(Family::Rtx20,true));
    assert(!NewProviderNeedsValidation(42,42,true)); // Repeated selected provider load.
    assert(!NewProviderNeedsValidation(43,42,false)); // Unrelated library load.
    assert(NewProviderNeedsValidation(43,42,true)); // Different FG provider.
    for(auto stage:{Stage::Waiting,Stage::Preparing,Stage::Unavailable,Stage::Poisoned}) {
        assert(!MayEnterVendor(Family::Rtx30,stage,true,false,true));
        assert(!MayEnterVendor(Family::Rtx20,stage,true,false,false));
    }
    assert(MayEnterVendor(Family::None,Stage::Unavailable,false,false,false));
    assert(MayEnterVendor(Family::Rtx20,Stage::Waiting,false,false,false));
    assert(MayEnterVendor(Family::Rtx30,Stage::Ready,true,true,true));
    assert(!MayEnterVendor(Family::Rtx30,Stage::Ready,true,true,false)); // Direct NGX cannot inherit.
    assert(MayEnterVendor(Family::Rtx30,Stage::Unavailable,true,false,true,true)); // Ordinary Off.
    assert(!MayEnterVendor(Family::Rtx30,Stage::Poisoned,false,false,true,true));
    assert(SelectableCeiling(true,5)==5 && SelectableCeiling(true,3)==3);
    assert(SelectableCeiling(true,0)==0 && SelectableCeiling(true,6)==0 && SelectableCeiling(false,5)==0);
    assert(!GameFgScope::Current());
    {GameFgScope fg(true);assert(GameFgScope::Current());
      {GameFgScope sr(false);assert(!GameFgScope::Current());}
      assert(GameFgScope::Current());
      std::thread other([]{assert(!GameFgScope::Current());});other.join();
    }assert(!GameFgScope::Current());
    EarlyCoverage c;
    assert(!c.CanPrepare(42));
    const uintptr_t loaded[]{42,43};c.Begin(loaded);
    c.Covered(42,true);assert(!c.CanPrepare(42)); // Load before host: no inference of non-use.
    c.Covered(44,false);assert(!c.CanPrepare(44));
    c.Covered(44,true);assert(c.CanPrepare(44));
    c.Create();assert(!c.CanPrepare(44)); // Cached direct Create before preparation.
    EarlyCoverage another;another.Begin(loaded);another.Covered(44,true);
    assert(!another.CanPrepare(45)); // Second provider never inherits admission.
    for(auto stage:{Stage::Off,Stage::Waiting,Stage::Preparing,Stage::Unavailable,Stage::Poisoned})
        assert(!IsAdmitted(stage,true));
    assert(IsAdmitted(Stage::Ready,true));assert(IsAdmitted(Stage::ActivityObserved,true));
    assert(!IsAdmitted(Stage::Ready,false));
    struct Options {unsigned structVersion=5;void* onErrorCallback=nullptr;void* next=nullptr;int count=1;};
    Options source{1,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),3},target;
    RestoreBorrowedOptions(target,source);
    assert(target.structVersion==1 && target.onErrorCallback==source.onErrorCallback && target.next==source.next);
    assert(target.count==1);
    assert(ThreadScope::Current()==0);
    {ThreadScope fg(15);assert(ThreadScope::Current()==15);
      {ThreadScope sr(0);assert(ThreadScope::Current()==0);}
      assert(ThreadScope::Current()==15);
      std::thread other([]{assert(ThreadScope::Current()==0);ThreadScope fg(16);assert(ThreadScope::Current()==16);});other.join();
      assert(ThreadScope::Current()==15);
    }assert(ThreadScope::Current()==0);
    assert(Decide({Mode::Dynamic,3},{true,true,5,5}).reason==Reason::DynamicNotQualified);
    std::puts("Experimental MFG host contracts: passed");
}
