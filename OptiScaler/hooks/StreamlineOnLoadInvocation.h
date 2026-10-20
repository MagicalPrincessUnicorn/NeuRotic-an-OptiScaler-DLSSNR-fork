#pragma once
#include "StreamlineModuleEpoch.h"

namespace Neurotic::Hooks
{
// Borrowed only for the current transform, inside the binding's real image lease.
// Identity metadata does not own parameters/caps or establish their layout.
template<class Parameters>
struct StreamlineOnLoadInvocation
{
    using Load=bool(*)(Parameters*,const char*,const char**);
    const Load original;
    Parameters* const parameters;
    const void* const binding;
    const StreamlineModuleEpochs::Epoch* const codeEpoch;
};
}
