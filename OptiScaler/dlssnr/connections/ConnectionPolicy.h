#pragma once
#include <cstdint>
#include <string>
#include "MixedInputSelection.h"
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
bool RequestSignalPreferences(int depth,int motion,bool strict,int color=0) noexcept;
bool RequestSavedSignalPreferences(int depth,int motion,bool strict,int color=0) noexcept;
Mixed::Policy SignalPreferences() noexcept;
struct SignalPolicyWire {
 uint32_t size=sizeof(SignalPolicyWire),version=1,depth=0,motion=0,strict=0,reserved=0;
 uint64_t revision=0;
};
struct SignalStatusWire {
 uint32_t size=sizeof(SignalStatusWire),version=1,depth=0,motion=0;
 uint32_t depthReason=1,motionReason=1,consumed=0,reserved=0;
 uint64_t revision=0,capture=0,generation=0;
 // SourceClass + 1; zero means unobserved. Transport and origin stay distinct.
 uint32_t depthOrigin=0,motionOrigin=0;
};
// V1 remains byte-for-byte compatible. V2 adds independent scene color.
struct SignalPolicyWireV2 : SignalPolicyWire {
 uint32_t color=0,reserved2=0;
 SignalPolicyWireV2(){size=sizeof(*this);version=2;}
};
struct SignalStatusWireV2 : SignalStatusWire {
 uint32_t color=0,colorReason=1,colorOrigin=0,reserved2=0;
 SignalStatusWireV2(){size=sizeof(*this);version=2;}
};
static_assert(sizeof(SignalPolicyWire)==32&&sizeof(SignalStatusWire)==64);
static_assert(sizeof(SignalPolicyWireV2)==40&&sizeof(SignalStatusWireV2)==80);
struct SignalStatusSnapshot {SignalStatusWireV2 status{};bool fresh=false;SignalStatusWireV2 used{};bool usedFresh=false;};
SignalStatusSnapshot QueryInputSignals(uint64_t now) noexcept;
// Reporting only: a completed consumer remains observable briefly after clean
// retirement. This snapshot never grants ownership or acquisition rights.
SignalStatusSnapshot QueryCompletedInputSignals(uint64_t now) noexcept;
// Revokes diagnostic admissions across an off/resume transition. Does not
// retire resources or alter rendering/selection ownership.
void InvalidateSignalReceipts() noexcept;
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
NR_CONNECTION_EXPORT NeuRotic_QuerySignalPolicyV1(void*,uint32_t);
NR_CONNECTION_EXPORT NeuRotic_PublishInputSignalsV1(uint32_t source,uint64_t ownerToken,const void*,uint32_t);

NR_CONNECTION_EXPORT NeuRotic_QuerySignalPolicyV2(void*,uint32_t);
NR_CONNECTION_EXPORT NeuRotic_PublishInputSignalsV2(uint32_t source,uint64_t ownerToken,const void*,uint32_t);
