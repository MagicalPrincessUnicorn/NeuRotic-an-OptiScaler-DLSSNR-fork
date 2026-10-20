#include <iostream>
#include <atomic>
#include <thread>
#include <future>
#if __has_include(<nr/lifecycle/Fsr3CallbackBinding.h>)
#include <nr/lifecycle/Fsr3CallbackBinding.h>
struct Owner { int calls=0; };
int main(){
 int checks=0,failed=0;auto check=[&](bool ok,const char* s){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<s<<'\n';}};
 using Bindings=Neurotic::Lifecycle::Fsr3CallbackBindings<Owner,8>;Owner owner;Bindings bindings;int context=0;
 auto* a=bindings.Open(owner,&context);check(a!=nullptr,"context gets binding");
 check(bindings.Open(owner,&context)==a,"per-frame configuration retains stable pointer");
 check(a->Invoke([](Owner& o){return ++o.calls;})==1,"live callback uses exact owner");
 bool escaped=false;try{a->Invoke([](Owner&)->int{throw 1;});}catch(int){escaped=true;}
 check(escaped,"callback exception propagates while clearing active depth");
 a->Mark(9);check(a->Duplicate(9)&&!a->Duplicate(8),"full frame duplicate belongs to binding");
 check(bindings.Close(),"throwing callback cannot strand drainage");check(!a->Invoke([](Owner& o){return ++o.calls;}),"closed callback cannot enter owner");
 auto* b=bindings.Open(owner,&context);check(b&&b!=a&&!b->Duplicate(9),"same native address new generation cannot inherit frame");
 check(!a->Invoke([](Owner& o){return ++o.calls;})&&owner.calls==1,"old queued callback cannot reach replacement");
 std::promise<void> entered,release,closing;auto gate=release.get_future().share();std::atomic<bool> closed=false;
 std::thread callback([&]{b->Invoke([&](Owner& o){entered.set_value();gate.wait();return ++o.calls;});});
 entered.get_future().wait();std::thread close([&]{closing.set_value();bindings.Close();closed=true;});
 closing.get_future().wait();check(!closed,"closure waits for entered CPU callback");release.set_value();callback.join();close.join();
 check(closed&&!b->Invoke([](Owner& o){return ++o.calls;}),"closure prevents all future entries");
 auto* c=bindings.Open(owner,&context);bool destroyed=false,pending=false;
 auto teardown=[&]{if(!bindings.Close()){pending=true;return;}destroyed=true;pending=false;};
 check(c->Invoke([&](Owner&){teardown();check(pending&&!destroyed,"reentrant closure defers teardown");
    check(!bindings.Open(owner,&context),"closed pending binding cannot reopen");return 7;})==7,"reentrant close does not deadlock");
 check(!c->Invoke([](Owner&){return 1;}),"reentrant close leaves binding inert");
 teardown();check(destroyed&&!pending,"serialized retry after callback exit permits CPU teardown");
 auto* d=bindings.Open(owner,&context);
 std::promise<void> enteredTry,releaseTry;auto tryGate=releaseTry.get_future().share();
 std::thread tryCallback([&]{d->Invoke([&](Owner&){enteredTry.set_value();tryGate.wait();return 1;});});
 enteredTry.get_future().wait();
 check(!bindings.TryClose()&&bindings.Current()==d,"contended try-close retains binding without blocking");
 releaseTry.set_value();tryCallback.join();
 check(bindings.TryClose()&&!bindings.Current(),"idle try-close drains CPU entry");
 check(!d->Invoke([](Owner&){return 1;}),"try-close tombstone rejects queued callback");
 auto* e=bindings.Open(owner,&context);
 check(e->Invoke([&](Owner&){check(!bindings.TryClose(),"reentrant try-close retains active callback");
    check(!e->Invoke([](Owner&){return 1;}),"reentrant try-close disarms entry");return 2;})==2,"reentrant try-close returns");
 check(bindings.TryClose()&&bindings.TryClose(),"try-close retry and empty close are idempotent");
 for(int i=0;i<3;++i){check(bindings.Open(owner,&context)!=nullptr,"bounded remaining slot");bindings.Close();}
 check(!bindings.Open(owner,&context),"exhaustion refuses rather than reusing old callback address");
 check(!a->Invoke([](Owner&){return 1;}),"old tombstone remains readable after exhaustion");
 std::cout<<"FSR callback binding: "<<checks<<" checks, "<<failed<<" failures; CPU only\n";return failed?1:0;
}
#else
int main(){std::cerr<<"FAIL immutable generation callback binding missing\n";return 1;}
#endif
