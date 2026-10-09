#pragma once
#include <cstdint>
#include <string>
namespace DlssNr::Connections {
enum class InputPhase { Applied, Waiting, Switching, RestartRequired, Blocked };
struct InputSelectionSnapshot {
 uint64_t serial=0;
 int requestedSource=0,requestedTransport=0,appliedSource=0,appliedTransport=0;
 bool allowCpuFallback=true;
 InputPhase phase=InputPhase::Applied;
 std::string reason;
};
bool RequestInputSelection(int source,int transport,bool allowCpuFallback);
InputSelectionSnapshot QueryInputSelection();
// Only the existing output supervisor drives these. Commit requires its API-
// specific GPU/consumer proof, with admission held closed through session update.
bool BeginInputSelection(uint64_t serial);
// advanceSession must be atomic-only: no config locks, GPU or vendor calls.
bool CommitInputSelection(uint64_t serial, bool (*advanceSession)() noexcept = nullptr);
void RefuseInputSelection(uint64_t serial,InputPhase,const std::string& reason);
void EndInputSelection() noexcept;
struct PolicyWire { uint32_t size=sizeof(PolicyWire),version=1,source=0,transport=0,allowCpuFallback=1,claimedSource=0,unsafe=0,nativeUsable=0; };
static_assert(sizeof(PolicyWire)==32);
// Core owner calls this with actual native readiness, never module presence.
void SetNativeUsable(bool) noexcept;
bool NativeUsable() noexcept;
bool ReserveNativeOwner(uint64_t ownerToken) noexcept;
bool PreparedOwnerClaimed() noexcept;
bool PreparedPresentHandled() noexcept;
// A completed built-in episode still owns this Present call. Its policy claim
// may retire so the following interval can choose a newly ready source.
class PresentArbitrationScope {
 bool previous;
public:
 PresentArbitrationScope() noexcept;
 ~PresentArbitrationScope();
 PresentArbitrationScope(const PresentArbitrationScope&)=delete;
 PresentArbitrationScope& operator=(const PresentArbitrationScope&)=delete;
};
}
#if defined(_WIN32) && defined(NR_CONNECTION_IMPLEMENTATION)
#define NR_CONNECTION_EXPORT extern "C" __declspec(dllexport) unsigned __cdecl
#else
#define NR_CONNECTION_EXPORT extern "C" unsigned
#endif
NR_CONNECTION_EXPORT NeuRotic_QueryConnectionPolicyV1(void*,uint32_t);
NR_CONNECTION_EXPORT NeuRotic_ClaimPreparedConnectionV1(uint32_t source,uint32_t transport,uint64_t ownerToken);
NR_CONNECTION_EXPORT NeuRotic_RetirePreparedConnectionV1(uint32_t source,uint64_t ownerToken,uint32_t clean);
