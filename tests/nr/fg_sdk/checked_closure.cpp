#include <cstdio>
#include <cstdint>
#include <future>
#include <thread>
#if __has_include("external/FidelityFX-CheckedClosure/CheckedClosure.h")
#include "external/FidelityFX-CheckedClosure/CheckedClosure.h"
#if __has_include("external/FidelityFX-CheckedClosure/CheckedSdkRegistry.h")
#include "external/FidelityFX-CheckedClosure/CheckedSdkRegistry.h"
#endif
using namespace ffx::nr;
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while(0)
struct Fence { uint64_t value=0; int32_t removed=0; };
static FenceObservation observe(void* p) { const auto& f=*static_cast<Fence*>(p); return {f.value,f.removed}; }
static Domain domain(Fence& f, uint64_t id, uint64_t target, uint32_t role) {
    Domain d{}; d.evidence.domain_id=id; d.evidence.domain_generation=1;
    d.evidence.fence_id=id; d.evidence.fence_generation=1; d.evidence.domain_kind=1;
    d.evidence.logical_role_bits=role; d.evidence.required_target=target;
    d.evidence.last_successful_target=target; d.evidence.last_attempted_target=target;
    d.observe=observe; d.context=&f; return d;
}
int main() {
    FfxNrClosureServiceV1 publicClosure{}; publicClosure.size=sizeof(publicClosure); publicClosure.version=1;
    CHECK(ffxNeuRoticQueryClosureServiceV1(&publicClosure)==FFX_NR_UNSUPPORTED); CHECK(publicClosure.begin==nullptr);
    FfxNrDispatchServiceV1 publicDispatch{}; publicDispatch.size=sizeof(publicDispatch); publicDispatch.version=1;
    CHECK(ffxNeuRoticQueryDispatchServiceV1(&publicDispatch)==FFX_NR_UNSUPPORTED); CHECK(publicDispatch.enroll==nullptr);
#if __has_include("external/FidelityFX-CheckedClosure/CheckedSdkRegistry.h")
    { CreatedRegistry registry; int exact=0, context=0, unrelated=0;
      CHECK(!registry.find(&unrelated)); auto created=registry.create(&exact,&context,11); CHECK(bool(created));
      CHECK(registry.find(&exact)==created); CHECK(registry.find(&context)==created);
      CHECK(!registry.create(&exact,&unrelated,11)); CHECK(!registry.find(&unrelated));
      CHECK(registry.remove(&exact)); CHECK(!registry.find(&context));
      auto recreated=registry.create(&exact,&context,11); CHECK(bool(recreated)); CHECK(recreated->identity.incarnation!=created->identity.incarnation);
      created->closure.sealInventory(); auto root=created->closure.enterRoot();
      CHECK(created->publish(0,root.child())); CHECK(!created->publish(0,root.child()));
      auto local=created->take(0); CHECK(bool(local)); CHECK(!created->take(0)); root={};
      auto h=created->closure.begin(1); CHECK(created->closure.poll(h,1,100)==FFX_NR_API_FAILURE_RETAINED);
    }
#else
    const bool registryExists=checks==UINT32_MAX; CHECK(registryExists);
#endif
    // Six independent shared fences and two pool fences: the old three-fence wait cannot pass.
    for(unsigned missing=0;missing<8;++missing) {
        CheckedClosure owner({1,1,1}); Fence fences[8];
        uint64_t targets[]={41,19,73,73,73,73,8,5};
        for(unsigned i=0;i<8;++i) { fences[i].value=targets[i]-(i==missing); CHECK(owner.addDomain(domain(fences[i],i+1,targets[i],1u<<i))); }
        owner.sealInventory(); auto root=owner.enterRoot(); CHECK(bool(root));
        auto child=root.child(); CHECK(bool(child));
        auto handle=owner.begin(17); CHECK(handle.drain_id!=0); CHECK(!owner.enterRoot());
        root={}; CHECK(owner.poll(handle,1,100)==FFX_NR_PENDING);
        child={}; CHECK(owner.poll(handle,1,100)==FFX_NR_PENDING);
        fences[missing].value=targets[missing]; CHECK(owner.poll(handle,2,100)==FFX_NR_COMPLETE);
        CHECK(!owner.enterRoot()); CHECK(owner.consume(handle,owner.revision())==FFX_NR_COMPLETE);
    }
    { CheckedClosure owner({2,1,1}); auto handle=owner.begin(1); CHECK(owner.poll(handle,1,100)==FFX_NR_INCOMPLETE_COVERAGE); }
    { CheckedClosure owner({3,1,1}); Fence f{UINT64_MAX}; CHECK(owner.addDomain(domain(f,1,41,1))); owner.sealInventory(); auto h=owner.begin(1); CHECK(owner.poll(h,1,100)==FFX_NR_DEVICE_REMOVED_RETAINED); f.value=41; CHECK(owner.poll(h,2,100)==FFX_NR_DEVICE_REMOVED_RETAINED); }
    { CheckedClosure owner({4,1,1}); Fence f{41}; CHECK(owner.addDomain(domain(f,1,41,1))); owner.sealInventory(); owner.recordFailure(-1); auto h=owner.begin(1); CHECK(owner.poll(h,1,100)==FFX_NR_API_FAILURE_RETAINED); }
    { CheckedClosure owner({5,1,1}); Fence f{0}; CHECK(owner.addDomain(domain(f,1,41,1))); owner.sealInventory(); auto h=owner.begin(1); CHECK(owner.poll(h,100,100)==FFX_NR_TIMEOUT_RETAINED); f.value=41; CHECK(owner.poll(h,101,100)==FFX_NR_TIMEOUT_RETAINED); CHECK(owner.poll(h,101,200)==FFX_NR_COMPLETE); CHECK(owner.begin(2).drain_id==0); CHECK(owner.begin(1).drain_id==h.drain_id); }
    { CheckedClosure owner({6,1,1}); owner.sealInventory(); auto root=owner.enterRoot(); auto callback=root.callback(); auto h=owner.begin(1); CHECK(owner.poll(h,1,100)==FFX_NR_REENTRANT); callback={}; root={}; CHECK(owner.poll(h,1,100)==FFX_NR_COMPLETE); }
    // Real worker owns a local child even after the publishing root has finished.
    { CheckedClosure owner({7,1,1}); owner.sealInventory(); auto root=owner.enterRoot(); auto local=root.child(); std::promise<void> entered, finish; auto release=finish.get_future(); std::thread worker([token=std::move(local),&entered,&release]() mutable { entered.set_value(); release.wait(); token={}; }); entered.get_future().wait(); root={}; auto h=owner.begin(1); CHECK(owner.poll(h,1,100)==FFX_NR_PENDING); finish.set_value(); worker.join(); CHECK(owner.poll(h,1,100)==FFX_NR_COMPLETE); }
    { CheckedClosure owner({8,1,1}); Fence f{41}; CHECK(owner.addDomain(domain(f,1,41,1))); owner.sealInventory(); auto h=owner.begin(1); auto forged=h; ++forged.context.incarnation; CHECK(owner.poll(forged,1,100)==FFX_NR_STALE_CONTEXT); CHECK(owner.poll(h,1,100)==FFX_NR_COMPLETE); FfxNrDomainEvidenceV1 out{}; uint32_t n=99; CHECK(owner.read(h,owner.revision(),0,&out,0,n)==FFX_NR_INCOMPLETE_COVERAGE); CHECK(n==0); CHECK(owner.read(h,owner.revision(),0,&out,1,n)==FFX_NR_COMPLETE); CHECK(n==1 && out.required_target==41); CHECK(owner.consume(h,owner.revision()-1)==FFX_NR_STALE_CONTEXT); }
    { CheckedClosure owner({9,1,1}); owner.sealInventory(); auto root=owner.enterRoot(); auto borrow=root.borrowList(); auto h=owner.begin(1); root={}; CHECK(owner.poll(h,1,100)==FFX_NR_EXTERNAL_RECORDING_OUTSTANDING); borrow={}; CHECK(owner.poll(h,1,100)==FFX_NR_COMPLETE); }
    // Descendant transfer across callback/child counters never exposes an empty prefix.
    { CheckedClosure owner({10,1,1}); owner.sealInventory(); std::atomic<bool> ready=false,done=false,release=false;
      std::thread worker([&] { auto root=owner.enterRoot(); auto callback=root.callback(); root={}; ready=true;
        for(unsigned i=0;i<100000;++i) { auto child=callback.child(); callback={}; callback=child.callback(); child={}; }
        done=true; while(!release.load()) std::this_thread::yield(); });
      while(!ready.load()) std::this_thread::yield(); auto h=owner.begin(1); bool alwaysPending=true;
      while(!done.load()) if(owner.poll(h,1,100)!=FFX_NR_PENDING) alwaysPending=false;
      CHECK(alwaysPending); CHECK(!owner.prefixFinished()); release=true; worker.join(); CHECK(owner.poll(h,1,100)==FFX_NR_COMPLETE);
    }
    std::printf("%u checks, %u failures\n",checks,failures); return failures?1:0;
}
#else
int main() { std::puts("FAIL: checked SDK closure owner is absent"); return 1; }
#endif
