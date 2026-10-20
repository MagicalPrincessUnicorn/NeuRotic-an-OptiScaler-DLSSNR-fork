#include "../../apps/NeuRoticWindowWorker/nr/OwnedFrameRing.h"
#include <cassert>
#include <cstdio>
struct Slot {bool retired=true;int stamp=0;};
int main(){
 nrw::OwnedFrameRing<Slot> ring;
 auto a=ring.TryAcquire(),b=ring.TryAcquire(),c=ring.TryAcquire();
 assert(a&&b&&c&&!ring.TryAcquire());
 a->stamp=17;a->retired=true;auto consumer=a;a.reset();
 assert(!ring.TryAcquire()&&consumer->stamp==17);
 auto* identity=consumer.get();consumer.reset();auto reused=ring.TryAcquire();
 assert(reused.get()==identity);reused.reset();
 // A failed completion retains storage even with no external consumer.
 assert(!ring.TryAcquire());
 b->retired=true;b.reset();assert(ring.TryAcquire());
 std::puts("PASS bounded owned frame ring retains pending work and live consumers");
}
