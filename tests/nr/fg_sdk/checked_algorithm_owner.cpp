#include <cstdio>
#include <thread>
#include "external/FidelityFX-CheckedClosure/CheckedAlgorithmState.h"
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
using namespace ffx::nr;
static FenceObservation complete(void*){return {1,0};}
static std::shared_ptr<CreatedContext> swap(int& object) {
 auto owner=createdRegistry().create(&object,&object,1);
 Domain domain{};domain.evidence.domain_id=1;domain.evidence.domain_generation=1;domain.evidence.fence_id=1;domain.evidence.fence_generation=1;domain.evidence.required_target=1;domain.evidence.last_successful_target=1;domain.context=&object;domain.observe=complete;
 CHECK(owner->closure.addDomain(domain));owner->closure.sealInventory();owner->enrolled=true;return owner;
}
int main(){
 int object=1,fg=2,calls=0;auto owner=swap(object);
 auto custom=std::make_shared<AlgorithmState>(&fg,[]{return 0;},false);CHECK(!custom->select(owner));
 auto otherDevice=std::make_shared<AlgorithmState>(&fg,[]{return 0;},true,&fg);CHECK(!otherDevice->select(owner));
 auto algorithm=std::make_shared<AlgorithmState>(&fg,[&]{++calls;AlgorithmCleanup::Observe(0);return 0;});
 CHECK(algorithm->select(owner));CHECK(!algorithm->select(owner));
 auto scope=algorithm->enter();CHECK(bool(scope));CHECK(algorithm->inspect().active);
 algorithm->closeAdmission();CHECK(!algorithm->enter());
 AlgorithmReleaseReceipt receipt{};auto drain=owner->closure.begin(3);
 CHECK(algorithm->releaseAfterDrain(drain,1,receipt)!=FFX_NR_COMPLETE);CHECK(calls==0);
 scope={};CHECK(!algorithm->inspect().active);
 CHECK(owner->closure.poll(drain,0,100)==FFX_NR_COMPLETE);auto revision=owner->closure.revision();
 auto forged=drain;++forged.drain_id;CHECK(algorithm->releaseAfterDrain(forged,revision,receipt)!=FFX_NR_COMPLETE);CHECK(calls==0);
 CHECK(algorithm->releaseAfterDrain(drain,revision+1,receipt)!=FFX_NR_COMPLETE);CHECK(calls==0);
 CHECK(algorithm->releaseAfterDrain(drain,revision,receipt)==FFX_NR_COMPLETE);CHECK(calls==1);CHECK(receipt.wholeContextDestroyed);CHECK(receipt.cleanupOperations==1);CHECK(algorithm->inspect().released);
 CHECK(algorithm->releaseAfterDrain(drain,revision,receipt)==FFX_NR_COMPLETE);CHECK(calls==1);CHECK(!algorithm->enter());
 int object2=3,fg2=4,badCalls=0;auto owner2=swap(object2);
 auto failed=std::make_shared<AlgorithmState>(&fg2,[&]{++badCalls;AlgorithmCleanup::Observe(-9);return 0;});
 CHECK(failed->select(owner2));failed->closeAdmission();auto drain2=owner2->closure.begin(4);CHECK(owner2->closure.poll(drain2,0,100)==FFX_NR_COMPLETE);
 CHECK(failed->releaseAfterDrain(drain2,owner2->closure.revision(),receipt)==FFX_NR_API_FAILURE_RETAINED);CHECK(badCalls==1);CHECK(!receipt.wholeContextDestroyed);CHECK(failed->inspect().failed);
 CHECK(failed->releaseAfterDrain(drain2,owner2->closure.revision(),receipt)==FFX_NR_API_FAILURE_RETAINED);CHECK(badCalls==1);CHECK(!failed->enter());
 std::printf("%u checks, %u failures\n",checks,failures);return failures?1:0;
}
