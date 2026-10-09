#pragma once
#include "FinalRealFrameTypes.h"

namespace Neurotic::Lifecycle
{
class NativeInvocationOwner;
// An owner-produced capability, never serialized or reconstructed from C11.
// It covers the invocation's physical/provider/callback/history tails. Only
// the finalizer can close its separate claim after checking identity exclusion.
class NativeUnsealedRetirement
{
    friend class NativeInvocationOwner;
    friend class FinalRealFrameFinalizer;
#ifdef NR_NATIVE_FINALIZER_TESTING
    friend struct NativeUnsealedTestAccess;
#endif
    PrimaryNrClaimHandle claim_;
    C::NrExecutionRecipe recipe_;
    C::NativeSampleIdentityV1 sample_;
    C::RecordKey recording_;
    NativeUnsealedRetirement(PrimaryNrClaimHandle claim,const C::NrExecutionRecipe& recipe,
        const C::NativeSampleIdentityV1& sample,const C::RecordKey& recording)
        :claim_(claim),recipe_(recipe),sample_(sample),recording_(recording){}
};
}
