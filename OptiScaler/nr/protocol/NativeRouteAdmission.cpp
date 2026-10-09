#include "NativeRouteAdmission.h"
#include <nr/lifecycle/NativeSessionLifetime.h>
namespace Neurotic::Protocol
{
static bool SameLease(const Lifecycle::LeaseBinding& a,const Lifecycle::LeaseBinding& b)noexcept
{
    return a.description==b.description&&a.consumer==b.consumer&&a.purpose==b.purpose&&a.plan==b.plan&&
        a.profile==b.profile&&a.route==b.route&&a.contentBasis==b.contentBasis&&a.cpuReadback==b.cpuReadback&&
        a.callbackScoped==b.callbackScoped&&a.dependencyRequired==b.dependencyRequired&&a.sameRecording==b.sameRecording;
}
bool SameNativeInvocation(const NativeInvocationSnapshot& a,const NativeInvocationSnapshot& b)noexcept
{
    if(a.metadata!=b.metadata||a.SourceBoundRequest()!=b.SourceBoundRequest()||a.Sample()!=b.Sample()||a.Delivery()!=b.Delivery()||a.product!=b.product||a.anchor!=b.anchor||a.generation!=b.generation||a.frame!=b.frame||a.consumer!=b.consumer||
       a.recording!=b.recording||a.reservation!=b.reservation||a.generations!=b.generations||a.handoff!=b.handoff||
       a.leases.Size()!=b.leases.Size())return false;
    for(std::size_t i=0;i<a.leases.Size();++i)if(!SameLease(*a.leases.Get(i),*b.leases.Get(i)))return false;
    return true;
}
bool NativeInvocationMatches(const NativeInvocationSnapshot& s,const Orchestration::MaterializationRecord& m)noexcept
{
    const auto& recipe=s.product.recipe;const auto& profile=s.product.profileCertificate;
    const auto& dependencies=s.product.dependencies;const auto& stable=m.preparation.dependencies;
    if(!s.Sample()||s.Sample()->Check()!=C::Error::None||s.Delivery()!=m.delivery.kind||
       m.delivery.Check()!=C::Error::None||m.initialSample.Check()!=C::Error::None||
       s.Sample()->session!=m.initialSample.session||s.Sample()->stream!=m.initialSample.stream||
       s.Sample()->view!=m.initialSample.view||s.Sample()->feature!=m.initialSample.feature||
       s.Sample()->ingress!=m.initialSample.ingress)return false;
    if(!m.plan.nativeDelivery||m.plan.nativeDelivery!=m.anchor.nativeDelivery||
        recipe.nativeDelivery!=m.plan.nativeDelivery||!recipe.nativeSample||recipe.nativeSample!=s.frame.nativeSample)return false;
    if(dependencies.profile!=stable.profile||dependencies.strategy!=stable.strategy||dependencies.placement!=stable.placement||
       !Context::SemanticEqual(dependencies.provider,stable.provider)||dependencies.structuralPlans.Size()!=stable.structuralPlans.Size())return false;
    const bool freshNativePublication=(recipe.placement==C::Placement::NativeBefore||
        recipe.placement==C::Placement::NativeAfter)&&s.Sample()&&
        s.Sample()->producerOrdinal>m.initialSample.producerOrdinal&&
        recipe.nativeSample==s.frame.nativeSample&&
        s.product.inputRepresentations.Size()==dependencies.structuralPlans.Size()&&
        std::all_of(s.product.inputRepresentations.begin(),s.product.inputRepresentations.end(),
            [&](const auto& use){return use.sourceCandidate&&use.nativeSample==s.frame.nativeSample;});
    if(freshNativePublication)
    {
        // The private owner has sealed the current C01/C12 closure and mapper.
        // For After, that seal also verifies the private selected-SR output
        // provenance. Per-callback C12 keys are not the first sample's keys.
    }
    else for(const auto& plan:stable.structuralPlans)
        if(std::count(dependencies.structuralPlans.begin(),dependencies.structuralPlans.end(),plan)!=1)return false;
    if(!s.metadata||!Context::ValidValues(s.product)||s.anchor!=m.anchor||!Context::Established(m.plan.routeGeneration)||
       s.generation!=m.plan.routeGeneration.KnownPart()->value||recipe.committedPlan.recordType.View()!=C::PlanCommit::WireName||
       recipe.committedPlan.record!=m.plan.header.record||recipe.committedPlan.revision!=m.plan.header.revision||
       recipe.header.scope!=m.enrollment.scope||profile.header.scope!=m.enrollment.scope||
       recipe.strategy!=m.anchor.strategy||recipe.profile!=m.anchor.profile||recipe.placement!=m.anchor.placement||
       profile.header.contract!=C::ContractId::C04||profile.header.owner!=C::OwnerDomain::Strategy||
       profile.eligibility!=C::Eligibility::Eligible||profile.purpose.View()!="RENDER.NativeBindings"||
       profile.profile!=recipe.profile||profile.context!=recipe.context||
       !Context::Established(s.frame.sessionId)||s.frame.sessionId.KnownPart()->value!=m.enrollment.session||
       !Context::Established(s.frame.renderStreamId)||s.frame.renderStreamId.KnownPart()->value!=m.enrollment.stream||
       !Context::Established(s.frame.viewId)||s.frame.viewId.KnownPart()->value!=m.enrollment.view||
       // HostReturn binds the owner's NR evaluation to the exact sealed Native
       // sample. An absent upstream evaluation is not an original-game ID.
       // Source-bound Strict instead carries the Evaluation owner's association
       // sealed against this exact sample/transaction. Canonical Strict still
       // requires frame identity. A supplied conflicting identity always fails.
       ((s.frame.evaluationId.IsKnown()||(s.Delivery()!=C::NativeDeliveryKind::HostReturn&&
          !s.SourceEvaluationAssociated()))&&
        (!Context::Established(s.frame.evaluationId)||s.frame.evaluationId.KnownPart()->value!=recipe.evaluation))||
       s.consumer.Check()!=C::Error::None||s.recording.Check()!=C::Error::None||s.reservation.Check()!=C::Error::None||
       !s.leases.Size())return false;
    for(const auto& lease:s.leases)
    {
        if(!Lifecycle::ValidBinding(lease)||!lease.plan||*lease.plan!=m.plan.header.record||
           !lease.profile||*lease.profile!=profile.header.record||!lease.route||lease.route->identity!=s.generation.Describe()||
           lease.consumer!=s.consumer||lease.description.context!=recipe.context||!lease.sameRecording||
           lease.sameRecording->recording!=s.recording||lease.sameRecording->reservation!=s.reservation)return false;
    }
    return true;
}
NativeInvocationAdmission::NativeInvocationAdmission(std::shared_ptr<Lifecycle::NativeSessionLifetime> root,
    std::size_t slot,std::uint64_t serial):root_(std::move(root)),slot_(slot),serial_(serial){}
NativeInvocationAdmission::~NativeInvocationAdmission(){Drop();}
NativeInvocationAdmission::NativeInvocationAdmission(NativeInvocationAdmission&& other)noexcept
    :root_(std::move(other.root_)),slot_(other.slot_),serial_(other.serial_){other.serial_=0;}
NativeInvocationAdmission& NativeInvocationAdmission::operator=(NativeInvocationAdmission&& other)noexcept
{if(this!=&other){Drop();root_=std::move(other.root_);slot_=other.slot_;serial_=other.serial_;other.serial_=0;}return *this;}
void NativeInvocationAdmission::Drop()noexcept
{if(root_)root_->kernel_->DropInvocation(slot_,serial_);root_.reset();serial_=0;}
bool NativeInvocationAdmission::AcquirePrimary(){return root_&&root_->kernel_->ClaimInvocation(slot_,serial_);}
bool NativeInvocationAdmission::SubmitEvaluation(const C::EvaluationResult& result)
{return root_&&root_->kernel_->SubmitInvocation(slot_,serial_,result);}
std::optional<Lifecycle::PrimaryNrClaimHandle> NativeInvocationAdmission::Primary()const
{return root_?root_->kernel_->InvocationClaim(slot_,serial_):std::nullopt;}
std::optional<Lifecycle::SourceBoundClaimHandle> NativeInvocationAdmission::SourceBoundPrimary()const
{return root_?root_->kernel_->InvocationSourceBoundClaim(slot_,serial_):std::nullopt;}
C::NativeDeliveryKind NativeInvocationAdmission::Delivery()const noexcept
{return root_?root_->kernel_->InvocationDelivery(slot_,serial_):C::NativeDeliveryKind::Invalid;}
std::optional<C::NativeSampleIdentityV1> NativeInvocationAdmission::Sample()const
{return root_?root_->kernel_->InvocationSample(slot_,serial_):std::nullopt;}
const RecipeProduct* NativeInvocationAdmission::Product()const noexcept
{return root_?root_->kernel_->InvocationProduct(slot_,serial_):nullptr;}
std::unique_ptr<NativeOwnerActionGuard> NativeInvocationAdmission::Action(NativeCheckPoint point)
{return root_?root_->kernel_->InvocationAction(slot_,serial_,point):nullptr;}
bool NativeInvocationAdmission::StartModel(const NativeOwnerActionGuard& guard)
{return root_&&root_->kernel_->StartInvocationModel(slot_,serial_,guard);}
bool NativeInvocationAdmission::ModelCalled(){return root_&&root_->kernel_->InvocationModelCalled(slot_,serial_);}
void NativeInvocationAdmission::ModelReturned(){if(root_)root_->kernel_->InvocationModelReturned(slot_,serial_);}
std::optional<NativeInvocationAdmission> NativeInvocationIngress::Admit(
    const std::shared_ptr<Lifecycle::NativeSessionLifetime>& root,std::shared_ptr<NativeInvocationOwnerPort> port)
{return root?root->kernel_->AdmitInvocation(std::move(port)):std::nullopt;}
}
