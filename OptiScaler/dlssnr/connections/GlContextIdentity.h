#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <atomic>
namespace DlssNr::Connections {
struct GlContextIdentity {std::uint64_t contextIncarnation=0,shareGroupIncarnation=0;std::uint32_t ownerThread=0;};
// One observation owner per actual context incarnation. Call Invalidate on deletion
// or share-group replacement before any namespace can be reused. A current-context
// snapshot alone does not authorize guessing context creation or share lineage.
class GlContextLease {
public:
    GlContextLease(HGLRC,GlContextIdentity);
    bool IsCurrent()const;
    void Invalidate();
    // Only after a successful make-current; excludes simultaneous use on old thread.
    bool TransferToCurrentThread();
    GlContextIdentity Identity()const;
private:
    HGLRC context_=nullptr;
    std::uint64_t contextIncarnation_=0,shareGroupIncarnation_=0;
    std::atomic<std::uint32_t> thread_{0};std::atomic<bool> valid_{true};
};
}
