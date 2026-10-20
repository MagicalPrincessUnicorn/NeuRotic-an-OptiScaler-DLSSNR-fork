#include "session_policy.h"
#include <cassert>
int main()
{
    nrpg::Episode e;
    assert(!e.Armed());
    assert(e.Begin(7));
    assert(!e.Armed() && !e.History());
    e.Provider();
    assert(e.Armed() && !e.History());
    e.Delivered();
    assert(e.Begin(8));
    assert(!e.Armed());
    e.Provider();
    assert(e.History());
    const auto firstGeneration=e.generation;
    e.Drop();
    assert(e.generation>firstGeneration && !e.History());
    assert(e.Begin(9));e.Provider();assert(!e.History());
    e.Delivered();assert(e.Begin(11));e.Provider();assert(!e.History());
    e.Quarantine();assert(!e.Begin(12) && !e.Armed());
    nrpg::Episode max;
    assert(!max.Begin(16777215));
    assert(!nrpg::MayReuse(true,false,false));
    assert(!nrpg::MayReuse(false,true,true));
    assert(nrpg::MayReuse(true,true,true));
    assert(!nrpg::SdrAllowed(0));assert(nrpg::SdrAllowed(1));
    assert(!nrpg::SdrAllowed(2));assert(!nrpg::SdrAllowed(3));
}
