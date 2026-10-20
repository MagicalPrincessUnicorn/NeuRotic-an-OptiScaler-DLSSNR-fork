#pragma once
// Installation decisions for the optional deferred-clear observation extension.
// Inherited spoof/scan attachment requests remain independent of this extension.
namespace Neurotic::D3D12::NativeRtvHookPolicy {
struct Install {
    bool attempted, pinned, transactionBegan, currentThreadEnlisted, otherThreadsEnlisted;
    bool Ready() const noexcept {
        return attempted && pinned && transactionBegan && currentThreadEnlisted && otherThreadsEnlisted;
    }
    bool AttachCommitted(bool install, bool haveOriginal, bool legacyRequested) const noexcept {
        return install && haveOriginal && (legacyRequested || Ready());
    }
    bool RetainCommitted(bool retained, bool install, bool attached, bool committed) const noexcept {
        return retained || (install && Ready() && attached && committed);
    }
    bool Coverage(bool committed, bool committedAttached, bool descriptorsAttached) const noexcept {
        return Ready() && committed && committedAttached && descriptorsAttached;
    }
};
}
