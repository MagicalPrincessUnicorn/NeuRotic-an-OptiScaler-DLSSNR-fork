#include <dlssnr/NativeFeatureRegistry.h>
#include <stdexcept>
#include <string>
#include <iostream>
int main()
{
    unsigned checks=0;auto check=[&](auto&& condition){++checks;if(!condition)throw std::runtime_error("check "+std::to_string(checks));};
    DlssNr::NativeFeatureRegistry<void*> registry;
    registry.Set(1,reinterpret_cast<void*>(1));
    const auto first=registry.Read(1);
    auto pin=registry.Pin(1,first);check(pin);
    const auto lifetime=pin->Lifetime();check(lifetime);
    check(lifetime->Generation()==first.generation);
    check(!lifetime->Inspect().released);
    check(pin->MarkOpaqueEntry());check(!pin->MarkOpaqueEntry());
    check(lifetime->Inspect().opaqueEntries==1);
    check(lifetime->Inspect().activeCallbacks==1);
    check(!registry.BeginRelease(1,first));
    pin.reset();check(lifetime->Inspect().activeCallbacks==0);
    auto release=registry.BeginRelease(1,first);check(release);
    check(lifetime->Inspect().admissionClosed);
    check(!lifetime->Inspect().released);
    check(release->Complete(true));check(lifetime->Inspect().released);
    check(!lifetime->Inspect().coverageUnknown);
    registry.Set(1,reinterpret_cast<void*>(1));
    auto second=registry.Read(1);auto next=registry.Pin(1,second);
    auto failed=next->Lifetime();next.reset();
    auto failure=registry.BeginRelease(1,second);check(failure);
    check(!failure->Complete(false));check(failed->Inspect().coverageUnknown);
    auto retry=registry.BeginRelease(1,second);check(retry);check(retry->Complete(true));
    check(failed->Inspect().released&&failed->Inspect().coverageUnknown);
    registry.Set(1,reinterpret_cast<void*>(1));auto third=registry.Read(1);
    auto old=registry.Pin(1,third);auto stale=old->Lifetime();
    registry.Set(1,reinterpret_cast<void*>(2));
    check(stale->Inspect().coverageUnknown&&!stale->Inspect().released);
    old.reset();check(stale->Inspect().activeCallbacks==0);
    auto fourth=registry.Read(1);auto current=registry.Pin(1,fourth);
    auto bypass=current->Lifetime();registry.MarkUntrackedEvaluation(1,fourth);
    check(bypass->Inspect().coverageUnknown);
    current.reset();auto abandoned=registry.BeginRelease(1,fourth);abandoned.reset();
    check(bypass->Inspect().admissionClosed&&!bypass->Inspect().released);
    check(!registry.Pin(1,fourth));
    DlssNr::NativeFeatureRegistry<void*> closing;closing.Set(1,reinterpret_cast<void*>(1));
    auto late=closing.Pin(1,closing.Read(1));check(late);check(!closing.StopCallbacks());
    check(!late->MarkOpaqueEntry());check(late->Lifetime()->Inspect().coverageUnknown);
    std::cout<<"selected_sr_lifetime: "<<checks<<" checks passed\n";
}

