#pragma once
#include "NativeFgReceipt.h"
#include <SysUtils.h>
#include <resource.h>
#include <mutex>
#include <optional>

namespace DlssNr::NativeFgTrace
{
// Ordinary configured Trace only. Do not arm FrameTrace/lifecycle journals.
inline bool Enabled() noexcept
{ return !loggerClosed.load(std::memory_order_relaxed) && spdlog::should_log(spdlog::level::trace); }
struct PreserveError
{
    DWORD value = GetLastError();
    ~PreserveError() { SetLastError(value); }
};
struct Identity
{
    DWORD pid = GetCurrentProcessId();
    uint64_t start = 0;
    bool known = false;
    Identity() noexcept
    {
        PreserveError preserve;
        FILETIME creation{},exit{},kernel{},user{};
        known=GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)!=FALSE;
        if (known) start=(uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
    }
};
inline const Identity& Process() { static const Identity value;return value; }
struct Gate
{
    std::mutex mutex;
    NativeFgReceipt::Samples samples;
    bool Take(uint64_t signature) noexcept
    {
        if (!Enabled()) return false;
        try { std::unique_lock lock(mutex,std::try_to_lock);return lock.owns_lock() && samples.Take(signature); }
        catch (...) { return false; }
    }
};
inline std::atomic<uint64_t> nextObservation {0};
struct Ticket
{
    uint64_t id = 0;
    std::optional<NativeFgReceipt::Tags> tags;
};
inline Ticket Begin(const sl::ResourceTag* input,uint32_t count,sl::RenderAPI api,
                    NativeFgReceipt::Context context,bool legacy,void* commandList,bool fgEnabled) noexcept
{
    PreserveError preserve;
    Ticket ticket;
    if (!fgEnabled || !Enabled() || api!=sl::RenderAPI::eD3D12) return ticket;
    try
    {
        static Gate gate;
        if (!gate.Take(NativeFgReceipt::Layout(input,count,context,legacy))) return ticket;
        ticket.tags.emplace(NativeFgReceipt::Capture(input,count,api,context,legacy,commandList));
        ticket.id=++nextObservation;
    }
    catch (...) {} // Failure to observe cannot change original tagging.
    return ticket;
}
inline void Returned(const Ticket& ticket,sl::Result result,uint64_t currentProvider,uint64_t currentEpoch) noexcept
{
    PreserveError preserve;
    if (!ticket.id || !ticket.tags || !Enabled()) return;
    try
    {
        const auto& process=Process();const auto& tags=*ticket.tags;const auto& c=tags.context;
        LOG_TRACE("NR FG receipt tags: build={} pid={} processStart100ns={} processIdentityKnown={} observation={} "
            "provider={} epoch={} frameKey={} viewportKey={} legacy={} upstreamResult={} upstreamAccepted={} "
            "currentProvider={} currentEpoch={} list={:X} declaredCount={} inspected={} recorded={} omitted={} "
            "unknownSchemas={} coverage=partial-batch-only sampleLimit=64 samplePolicy=first8-layoutchange-every600 admissionAuthority=false",
            VER_BUILD_COMMIT,process.pid,process.start,process.known,ticket.id,c.provider,c.epoch,c.key,c.viewport,
            tags.legacy,static_cast<int>(result),result==sl::Result::eOk,currentProvider,currentEpoch,tags.commandList,
            tags.declaredCount,tags.inspected,tags.count,tags.omitted,tags.unknownSchemas);
        for (uint32_t i=0;i<tags.count;++i)
        {
            const auto& value=tags.values[i];
            LOG_TRACE("NR FG receipt plane: observation={} index={} type={} tagVersion={} lifecycle={} resourceSupplied={} "
                "resourceVersion={} resourceSchemaKnown={} supplied={:X} canonical={:X} state={} "
                "left={} top={} width={} height={} optionalWidth={} optionalHeight={} optionalFormat={} "
                "nativeResult={:X} descriptorKnown={} descriptorWidth={} descriptorHeight={} descriptorFormat={} "
                "mips={} layers={} subresource=not-declared-by-tag contentLifetime=not-extended",
                ticket.id,value.index,value.type,value.version,value.lifecycle,value.resourceSupplied,value.resourceVersion,
                value.resourceSchemaKnown,value.supplied,value.canonical,value.state,value.left,value.top,value.width,value.height,
                value.optionalWidth,value.optionalHeight,value.optionalFormat,static_cast<uint32_t>(value.nativeResult),
                value.descriptorKnown,value.descriptorWidth,value.descriptorHeight,value.descriptorFormat,
                value.descriptorMips,value.descriptorLayers);
        }
    }
    catch (...) {}
}
inline thread_local NativeFgReceipt::ReturnHistory returnedPresent;

// Receipt association is narrower than rendering provenance. Every Dispatch
// masks the enclosing association; only its own original call publishes one.
struct ConsumerContext
{
    NativeFgReceipt::Context context {};
    bool associated = false;
};
inline thread_local ConsumerContext currentConsumer;
inline ConsumerContext CurrentConsumer() noexcept { return currentConsumer; }
class ConsumerScope
{
    ConsumerContext previous;
  public:
    ConsumerScope() noexcept : previous(currentConsumer) { currentConsumer = {}; }
    explicit ConsumerScope(NativeFgReceipt::Context value) noexcept : previous(currentConsumer)
    {
        currentConsumer = {value,true};
    }
    ~ConsumerScope() { currentConsumer = previous; }
    ConsumerScope(const ConsumerScope&)=delete;
    ConsumerScope& operator=(const ConsumerScope&)=delete;
    ConsumerScope(ConsumerScope&&)=delete;
    ConsumerScope& operator=(ConsumerScope&&)=delete;
};
template<class Frame> NativeFgReceipt::Context FrameContext(const Frame& frame) noexcept
{
    return {frame.providerGeneration,frame.taggedOutput.outputEpoch,frame.key,frame.viewportKey,frame.sequence};
}
class PresentScope
{
    ConsumerScope consumerEntry;
    NativeFgReceipt::Context context;
  public:
    explicit PresentScope(NativeFgReceipt::Context value) : context(value) { returnedPresent.Enter(); }
    ~PresentScope() { returnedPresent.Leave(finished?context:NativeFgReceipt::Context{},returned,finished&&didForward); }
    bool finished = false;
    HRESULT returned = E_PENDING;
    bool didForward = false;
    void SetContext(NativeFgReceipt::Context value) noexcept { context=value; }
    void End(HRESULT result,bool forwarded,bool valid,bool submitted,const char* refusal,void* chain,void* queue) noexcept
    {
        // Keep depth active through every remaining Dispatch cleanup callback.
        // Only the first-declared guard destructor commits the returned context.
        returned=result;didForward=forwarded;finished=true;
        PreserveError preserve;
        if (!Enabled()) return;
        try
        {
            static Gate gate;
            if (!gate.Take(NativeFgReceipt::Mix(NativeFgReceipt::Mix(context.provider,context.epoch),uint64_t(valid)|(uint64_t(submitted)<<1)))) return;
            const auto& process=Process();
            LOG_TRACE("NR FG receipt present: build={} pid={} processStart100ns={} processIdentityKnown={} "
                "provider={} epoch={} frameKey={} viewportKey={} realSequence={} valid={} nrSubmitted={} "
                "refusal=[{}] originalForwarded={} result={:X} chain={:p} queue={:p} "
                "tupleAssociation=exact-frame-key-only planeOwnership=not-established",
                VER_BUILD_COMMIT,process.pid,process.start,process.known,context.provider,context.epoch,context.key,
                context.viewport,context.sequence,valid,submitted,refusal?refusal:"none",forwarded,static_cast<uint32_t>(result),chain,queue);
        }
        catch (...) {}
    }
    PresentScope(const PresentScope&)=delete;
    PresentScope& operator=(const PresentScope&)=delete;
};
inline bool ConsumerSample(uint64_t provider,uint64_t epoch,bool scoped) noexcept
{
    PreserveError preserve;
    try { static Gate gate;return gate.Take(NativeFgReceipt::Mix(NativeFgReceipt::Mix(provider,epoch),scoped)); }
    catch (...) { return false; }
}
inline void Consumer(uint64_t handle,void* list,void* backbuffer,uint32_t backbufferResult,
                     void* hudless,uint32_t hudlessResult,NativeFgReceipt::Context context,bool scoped) noexcept
{
    PreserveError preserve;
    if (!Enabled()) return;
    try
    {
        const auto& process=Process();
        LOG_TRACE("NR FG receipt evaluate: build={} pid={} processStart100ns={} processIdentityKnown={} "
            "nativeHandle={} list={:p} backbuffer={:p} backbufferResult={} hudless={:p} hudlessResult={} "
            "provider={} epoch={} frameKey={} viewportKey={} realSequence={} scope={} "
            "uiAndAlphaParameter=unknown-public-key uiAlphaParameter=unknown-public-key inputConsumption=not-attested",
            VER_BUILD_COMMIT,process.pid,process.start,process.known,handle,list,backbuffer,backbufferResult,hudless,hudlessResult,
            context.provider,context.epoch,context.key,context.viewport,context.sequence,scoped?"present-call":"unassociated");
    }
    catch (...) {}
}
inline void Handoff(uint64_t handle,void* list,uint64_t provider,unsigned result,const char* reason,
                    uint64_t token,uint64_t sequence,uint64_t viewport,bool observed) noexcept
{
    PreserveError preserve;
    if (!Enabled()) return;
    try
    {
        static Gate gate;
        if (!gate.Take(NativeFgReceipt::Mix(NativeFgReceipt::Mix(provider,result),observed))) return;
        const auto& process=Process();
        LOG_TRACE("NR FG receipt consumer: build={} pid={} processStart100ns={} processIdentityKnown={} "
            "nativeHandle={} list={:p} provider={} completionClaim={} reason=[{}] "
            "producerFrameKey={} producerSequence={} viewportKey={} executionObserver={} "
            "dependencyMeaning=NR-producer-to-FG-only uiInputRetirement=not-established",
            VER_BUILD_COMMIT,process.pid,process.start,process.known,
            handle,list,provider,result,reason?reason:"none",token,sequence,viewport,observed);
    }
    catch (...) {}
}
inline void Completion(uint32_t viewport,sl::Result result,bool abiKnown,uint64_t version,
                       void* fence,uint64_t value,unsigned presented,uint64_t provider,uint64_t epoch) noexcept
{
    PreserveError preserve;
    if (!Enabled()) return;
    try
    {
        static Gate gate;
        if (!gate.Take(NativeFgReceipt::Mix(NativeFgReceipt::Mix(provider,epoch),uint64_t(fence!=nullptr)))) return;
        const auto& process=Process();const auto& previous=returnedPresent.previous;
        const bool candidate=abiKnown && result==sl::Result::eOk &&
            returnedPresent.Candidate(provider,epoch,uint64_t(viewport)+1);
        LOG_TRACE("NR FG receipt completion: build={} pid={} processStart100ns={} processIdentityKnown={} "
            "viewport={} result={} abiKnown={} version={} fence={:p} value={} presented={} provider={} epoch={} "
            "sameThreadPriorPresentCandidate={} priorFrameKey={} priorRealSequence={} insidePresent={} "
            "query=existing-only semanticPlaneRetirement=not-established ownershipAuthority=false",
            VER_BUILD_COMMIT,process.pid,process.start,process.known,viewport,static_cast<int>(result),abiKnown,version,
            fence,value,presented,provider,epoch,candidate,candidate?previous.key:0,candidate?previous.sequence:0,
            returnedPresent.depth!=0);
    }
    catch (...) {}
}
}
