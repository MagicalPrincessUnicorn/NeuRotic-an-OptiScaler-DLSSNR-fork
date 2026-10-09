#pragma once
namespace DlssNr { class NativeRendererInvocationBorrow; }
#include "NativeFrameBridge.h"
#include "NativeTemporalRuntime.h"

namespace Neurotic::Protocol
{
// Call-scoped output of the authenticated C06/C12/C03 ingress owner. These are
// current callback borrows, not serialized identities or retained COM ownership.
// colour/target are the original composition resources. They are NOT already
// encoded ModelProxy/ModelTarget views; the existing renderer realizes those
// plans and the allocation owner associates each actual prepared resource.
struct NativeTypedEvaluation
{
    DlssNrFrameInfo frame{};
    ID3D12Resource* colour=nullptr;
    ID3D12Resource* target=nullptr;
    ID3D12Resource* depth=nullptr;
    ID3D12Resource* motion=nullptr;
    std::optional<int> hostQuality;
    NativeExecutionObserver* observer=nullptr;
    DlssNr::NativeRendererInvocationBorrow* rendererBorrow=nullptr;
};
}
