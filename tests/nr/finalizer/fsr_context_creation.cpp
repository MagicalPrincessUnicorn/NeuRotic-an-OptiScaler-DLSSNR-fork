#include <iostream>
#if __has_include(<nr/lifecycle/Fsr3ContextCreation.h>)
#include <nr/lifecycle/Fsr3ContextCreation.h>
enum class Kind { Unknown,FG,Swapchain };
int main()
{
    using Table=Neurotic::Lifecycle::Fsr3ContextCreations<void*,void*,Kind>;
    Table table;int checks=0,failures=0;
    const auto check=[&](bool value,const char* why){++checks;if(!value){++failures;std::cerr<<why<<'\n';}};
    auto context=reinterpret_cast<void*>(1),moduleA=reinterpret_cast<void*>(2),moduleB=reinterpret_cast<void*>(3);
    check(!table.ObserveCreate(false,context,moduleA,Kind::FG,Kind::FG),"failed creation has no identity");
    check(!table.Find(context),"failed creation not registered");
    check(!table.ObserveCreate(true,nullptr,moduleA,Kind::FG,Kind::FG),"null context refused");
    check(!table.ObserveCreate(true,context,nullptr,Kind::FG,Kind::FG),"null module refused");
    auto first=table.ObserveCreate(true,context,moduleA,Kind::FG,Kind::FG);
    check(first&&first->generation&&first->module==moduleA,"actual module recorded");
    check(first&&table.Current(*first),"fresh generation current");
    auto other=table.ObserveCreate(true,reinterpret_cast<void*>(4),moduleB,Kind::Swapchain,Kind::Unknown);
    check(other&&other->module==moduleB&&other->requestedKind==Kind::Swapchain&&other->routingKind==Kind::Unknown,
          "main fallback preserves old routing and actual requested kind");
    check(first&&!table.ObserveDestroy(false,*first)&&table.Current(*first),"failed destroy retains exact binding");
    check(first&&table.ObserveDestroy(true,*first)&&!table.Find(context),"success removes exact generation");
    auto reused=table.ObserveCreate(true,context,moduleB,Kind::FG,Kind::FG);
    check(first&&reused&&reused->generation!=first->generation&&!table.Current(*first),"pointer reuse has distinct generation");
    check(first&&reused&&!table.ObserveDestroy(true,*first)&&table.Current(*reused),"stale destroy cannot erase newer generation");
    check(!table.ObserveCreate(true,context,moduleA,Kind::FG,Kind::FG),"duplicate live pointer refuses authority");
    auto conflict=table.Find(context);
    check(conflict&&conflict->ambiguous&&!table.Current(*reused),"conflict invalidates prior observation");
    check(other&&table.Current(*other),"unrelated context unaffected");
    std::cout<<"FSR context creation: "<<checks<<" checks, "<<failures<<" failures; CPU observation only\n";
    return failures?1:0;
}
#else
int main(){std::cerr<<"FAIL exact FSR context creation generation tracking missing\n";return 1;}
#endif
