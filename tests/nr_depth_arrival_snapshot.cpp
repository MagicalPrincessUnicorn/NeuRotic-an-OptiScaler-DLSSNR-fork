#include "NrConfigFixture.h"
#include <cstdio>
#include <cstdlib>
#include <future>
#include <thread>
struct WithDepth : TestConfig { NrOptional<int32_t,NoDefault> DepthResourceBarrier; };
template<class Option> void AdapterArrival(Option& value,int32_t state)
{
    if constexpr(requires{value.set_volatile_value_if_changed(state);})value.set_volatile_value_if_changed(state);
    else value.set_volatile_value(state);
}
template<class Snapshot> std::optional<int32_t> Declared(const Snapshot& snapshot)
{
    if constexpr(requires{snapshot.depthResourceArrival;})return snapshot.depthResourceArrival;
    else return std::nullopt;
}
static void Check(bool value,const char* reason){if(!value){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
int main()
{
    WithDepth config;Check(!Declared(NrConfigSnapshot<WithDepth>(config)),"absent declaration stays absent");
    config.DepthResourceBarrier=8;
    const NrConfigSnapshot<WithDepth> captured(config);
    config.DepthResourceBarrier=64;
    Check(Declared(captured)==8,"captured adapter declaration survives later configuration edit");
    Check(Declared(NrConfigSnapshot<WithDepth>(config))==64,"new snapshot observes new explicit declaration");
    TestConfig noDeclaration;Check(!Declared(NrConfigSnapshot<TestConfig>(noDeclaration)),"unrelated CPU source grants no declared state");
    AdapterArrival(config.DepthResourceBarrier,64);
    const auto revision=NrConfigSynchronization::ObservationRevision();
    AdapterArrival(config.DepthResourceBarrier,64);
    Check(NrConfigSynchronization::ObservationRevision()==revision,"unchanged adapter arrival does not dirty observations");
    std::promise<void> started;std::future<void> writer;
    {
        NrConfigSynchronization::Transaction transaction;
        writer=std::async(std::launch::async,[&]{started.set_value();AdapterArrival(config.DepthResourceBarrier,8);});
        started.get_future().wait();
        Check(writer.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout,"adapter writer excludes snapshot transaction");
        Check(Declared(NrConfigSnapshot<WithDepth>(config))==64,"snapshot retains arrival during blocked writer");
    }
    writer.get();Check(Declared(NrConfigSnapshot<WithDepth>(config))==8,"writer publishes after transaction ends");
    std::atomic<bool> done{false};
    std::thread mutations([&]{for(int i=0;i<20000;++i){config.DepthResourceBarrier.reset();AdapterArrival(config.DepthResourceBarrier,64);}done=true;});
    do {const auto value=Declared(NrConfigSnapshot<WithDepth>(config));Check(!value||*value==8||*value==64,"concurrent reset and adapter write produce a complete optional");} while(!done.load());
    mutations.join();
    std::puts("PASS: immutable arrival, writer exclusion, concurrent reset/capture, unchanged revision");
}
