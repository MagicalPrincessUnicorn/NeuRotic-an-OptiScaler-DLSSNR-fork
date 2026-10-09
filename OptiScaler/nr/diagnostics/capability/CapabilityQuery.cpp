#include "CapabilityQuery.h"
#include "CapabilityRegistry.h"
#include <algorithm>
namespace DlssNr::Capability {
namespace {
const Scope* ScopeFor(const Batch& b,TextRef ref) noexcept { for(size_t i=0;i<b.scopeCount;++i) { const auto& s=b.At<Scope>(b.scopes[i]); if(b.Text(s.id)==b.Text(ref)) return &s; } return nullptr; }
bool SameReceipt(const OwnerReceipt& a,const Batch& x,const OwnerReceipt& b,const Batch& y) noexcept { return x.Text(a.contract)==y.Text(b.contract)&&x.Text(a.version)==y.Text(b.version)&&x.Text(a.id)==y.Text(b.id)&&x.Text(a.verdict)==y.Text(b.verdict)&&x.Text(a.lineage)==y.Text(b.lineage)&&x.Text(a.sha256)==y.Text(b.sha256); }
struct FactRow { const Fact* fact=nullptr; const StreamImage* image=nullptr; };
struct EvidenceRow { const Evidence* evidence=nullptr; const StreamImage* image=nullptr; };
struct Validator {
    const Snapshot& snapshot; const QueryRequest& request; QueryResult& result;
    std::array<std::string_view,128> visited{},trail{}; size_t visits=0; bool capacity=false;
    struct EvidenceMemo { const Evidence* evidence; const RegistryEntry* registry; unsigned depth; bool ancestor,inferred; };
    struct FactMemo { const Fact* fact; unsigned depth; bool dependency,inferred; };
    std::vector<EvidenceMemo> evidenceMemo;
    std::vector<FactMemo> factMemo;
    void Reason(std::string_view r) { if(std::find(result.reasons.begin(),result.reasons.end(),r)==result.reasons.end()) result.reasons.emplace_back(r); }
    bool Node(std::string_view id,unsigned depth) {
        if(depth>=16) { capacity=true; Reason("CAP_QUERY_CAPACITY"); return false; }
        for(unsigned i=0;i<depth;++i) if(trail[i]==id) { Reason("CAP_DEPENDENCY_CYCLE"); return false; }
        trail[depth]=id;
        if(std::find(visited.begin(),visited.begin()+visits,id)==visited.begin()+visits) {
            if(visits>=128) { capacity=true; Reason("CAP_QUERY_CAPACITY"); return false; }
            visited[visits++]=id;
        }
        return true;
    }
    bool Stream(const StreamImage& image) {
        if(image.closed) { Reason("CAP_CLOSED_WRITER"); return false; }
        if(image.raced) { Reason("CAP_CAPTURE_RACED"); return false; }
        if(image.offered!=image.committed) { Reason("CAP_PUBLICATION_GAP"); return false; }
        return true;
    }
    bool ScopeCurrent(const Scope* scope,const Batch& b) {
        if(!scope||b.Text(scope->session)!=snapshot.session||request.session!=snapshot.session||scope->role!=ScopeRole::OwnerIdentity) { Reason("CAP_SCOPE_UNQUALIFIED"); return false; }
        for(auto stamp:b.List<OwnerStamp>(scope->bindings)) {
            if(stamp.status!=StampStatus::Known) { Reason("CAP_SCOPE_UNKNOWN"); return false; }
            auto context=std::find_if(request.bindings.begin(),request.bindings.end(),[&](const auto& c){return c.dimension==stamp.dimension;});
            if(context==request.bindings.end()||context->status!=StampStatus::Known||context->owner!=b.Text(stamp.owner)||context->instance!=b.Text(stamp.instance)||context->generation!=stamp.generation) { Reason("CAP_SCOPE_STALE"); return false; }
        }
        return true;
    }
    bool RetainsScope(const Scope* claim,const Batch& a,const Scope* source,const Batch& b) {
        if(!claim||!source||a.Text(claim->session)!=b.Text(source->session)) return false;
        for(auto dependency:b.List<OwnerStamp>(source->bindings)) {
            auto bindings=a.List<OwnerStamp>(claim->bindings); auto own=std::find_if(bindings.begin(),bindings.end(),[&](const auto& s){return s.dimension==dependency.dimension;});
            if(own==bindings.end()||own->status!=dependency.status||own->generation!=dependency.generation||a.Text(own->owner)!=b.Text(dependency.owner)||a.Text(own->instance)!=b.Text(dependency.instance)) return false;
        } return true;
    }
    FactRow FindFact(std::string_view id) {
        for(const auto& image:snapshot.streams) for(size_t i=0;i<image.batch.factCount;++i) { auto& f=image.batch.At<Fact>(image.batch.facts[i]); if(image.batch.Text(f.id)==id) return {&f,&image}; } return {};
    }
    EvidenceRow FindEvidence(std::string_view id) {
        for(const auto& image:snapshot.streams) for(size_t i=0;i<image.batch.evidenceCount;++i) { auto& e=image.batch.At<Evidence>(image.batch.evidence[i]); if(image.batch.Text(e.id)==id) return {&e,&image}; } return {};
    }
    bool EvidenceCurrent(EvidenceRow row,const Scope* claim,const Batch& claimBatch,const RegistryEntry& reg,unsigned depth,bool& inferred,bool ancestor=false) {
        if(!row.evidence) { Reason("CAP_MISSING_EVIDENCE"); return false; } const auto& e=*row.evidence; const auto& b=row.image->batch;
        if(!Node(b.Text(e.id),depth)||!Stream(*row.image)||e.writer!=row.image->writer||e.sequence!=row.image->committed) return false;
        auto scope=ScopeFor(b,e.scope); if(!ScopeCurrent(scope,b)||!RetainsScope(claim,claimBatch,scope,b)) { Reason("CAP_SCOPE_DROPPED"); return false; }
        for(const auto& memo:evidenceMemo) if(memo.evidence==&e&&memo.registry==&reg&&memo.ancestor==ancestor&&depth<=memo.depth) { inferred|=memo.inferred; return true; }
        bool localInference=false;
        if(e.source==SourceKind::HistoricalImport||e.source==SourceKind::Fixture||e.source==SourceKind::UserReport) { Reason("CAP_NON_LIVE_ORIGIN"); return false; }
        if((reg.requiresNoMutation||ancestor)&&e.mutation!=Mutation::NoneObserved) { Reason("CAP_MUTATED_EVIDENCE"); return false; }
        if(ancestor&&(e.source==SourceKind::EffectivePolicy||e.source==SourceKind::SyntheticDefault)) { Reason("CAP_TAINTED_PARENT"); return false; }
        if(e.source==SourceKind::Inference) { localInference=true; if(!e.parents.count) { Reason("CAP_MISSING_PARENTS"); return false; } }
        else if(!ancestor) {
            bool profile=false; for(size_t j=0;j<reg.profileCount;++j) if(reg.profiles[j].source==e.source&&reg.profiles[j].basis==e.basis) profile=true;
            if(!profile) { Reason("CAP_PROVENANCE_PROFILE"); return false; }
        }
        for(auto parent:b.List<TextRef>(e.parents)) if(!EvidenceCurrent(FindEvidence(b.Text(parent)),scope,b,reg,depth+1,localInference,true)) return false;
        inferred|=localInference; evidenceMemo.push_back({&e,&reg,depth,ancestor,localInference});
        result.evidenceIds.emplace_back(b.Text(e.id)); for(auto lineage:b.List<TextRef>(e.lineages)) result.lineageIds.emplace_back(b.Text(lineage)); return true;
    }
    bool FactCurrent(FactRow row,unsigned depth,bool& inferred,bool dependency=false) {
        if(!row.fact) { Reason("CAP_MISSING_DEPENDENCY"); return false; } const auto& f=*row.fact; const auto& b=row.image->batch; const auto* reg=Lookup(f.capability);
        if(!reg||!Node(b.Text(f.id),depth)||!Stream(*row.image)||f.writer!=row.image->writer||f.sequence!=row.image->committed||f.collection!=Collection::Known) { Reason("CAP_NO_CURRENT_ASSERTION"); return false; }
        auto scope=ScopeFor(b,f.scope); if(!ScopeCurrent(scope,b)) return false;
        for(const auto& memo:factMemo) if(memo.fact==&f&&memo.dependency==dependency&&depth<=memo.depth) { inferred|=memo.inferred; return true; }
        bool localInference=false;
        if(!(reg->producers&(1u<<static_cast<unsigned>(row.image->producer)))||!f.evidence.count) return false;
        for(auto evidence:b.List<TextRef>(f.evidence)) if(!EvidenceCurrent(FindEvidence(b.Text(evidence)),scope,b,*reg,depth+1,localInference)) return false;
        for(auto dep:b.List<TextRef>(f.dependencies)) {
            auto target=FindFact(b.Text(dep)); bool depInference=false;
            if(!FactCurrent(target,depth+1,depInference,true)||!RetainsScope(scope,b,ScopeFor(target.image->batch,target.fact->scope),target.image->batch)) { Reason("CAP_INVALID_DEPENDENCY"); return false; }
            if(depInference) localInference=true;
        }
        inferred|=localInference; factMemo.push_back({&f,depth,dependency,localInference}); return true;
    }
    struct Closure {
        std::array<std::string_view,128> seen{}; size_t count=0;
        bool inference=false,vendor=true,qualification=true,receipt=false,tainted=false;
    };
    bool ClosureNode(Closure& closure,std::string_view id) {
        if(std::find(closure.seen.begin(),closure.seen.begin()+closure.count,id)!=closure.seen.begin()+closure.count) return false;
        if(closure.count==closure.seen.size()) { capacity=true; return false; }
        closure.seen[closure.count++]=id; return true;
    }
    void EvidenceClosure(EvidenceRow row,Closure& closure) {
        if(!row.evidence) return;
        const auto& e=*row.evidence; const auto& b=row.image->batch;
        if(!ClosureNode(closure,b.Text(e.id))) return;
        closure.inference|=e.source==SourceKind::Inference;
        closure.tainted|=e.mutation!=Mutation::NoneObserved||e.source==SourceKind::EffectivePolicy||e.source==SourceKind::SyntheticDefault;
        closure.vendor&=e.source==SourceKind::VendorQuery&&e.basis==Basis::T3&&e.upstream==UpstreamBinding::ExactOwnerChain;
        closure.qualification&=e.source==SourceKind::QualificationReceipt&&e.basis==Basis::T6;
        closure.receipt|=e.semanticReceipt.contract.size&&e.semanticReceipt.version.size&&e.semanticReceipt.id.size&&e.semanticReceipt.lineage.size;
        for(auto parent:b.List<TextRef>(e.parents)) EvidenceClosure(FindEvidence(b.Text(parent)),closure);
    }
    void FactClosure(FactRow row,Closure& closure) {
        if(!row.fact) return;
        const auto& f=*row.fact; const auto& b=row.image->batch;
        if(!ClosureNode(closure,b.Text(f.id))) return;
        for(auto evidence:b.List<TextRef>(f.evidence)) EvidenceClosure(FindEvidence(b.Text(evidence)),closure);
        for(auto dependency:b.List<TextRef>(f.dependencies)) FactClosure(FindFact(b.Text(dependency)),closure);
    }
    bool ClosureEligible(FactRow row,bool& inferred) {
        Closure closure; FactClosure(row,closure); const auto& reg=*Lookup(row.fact->capability);
        if(capacity) return false;
        if(reg.requiresNoMutation&&closure.tainted) { Reason("CAP_PROVENANCE_CEILING"); return false; }
        if(reg.id=="nr.cap.vendor.feature_support"&&!closure.inference&&!closure.vendor) { Reason("CAP_VENDOR_CHAIN_UNVERIFIED"); return false; }
        if(reg.id=="nr.cap.route.owner_health"&&!closure.receipt) { Reason("CAP_OWNER_CONTRACT_UNAVAILABLE"); return false; }
        if((reg.id.starts_with("nr.cap.qualification.")||reg.id=="nr.cap.parameter.causal_effect")&&!closure.qualification) { Reason("CAP_QUALIFICATION_UNPROVEN"); return false; }
        inferred|=closure.inference; return true;
    }
};
Assertion CopyAssertion(const Assertion& a,const Batch& b,BatchBuilder& out) {
    auto copy=a; copy.text=out.Text(b.Text(a.text)); copy.protocol=out.Text(b.Text(a.protocol)); copy.protocolVersion=out.Text(b.Text(a.protocolVersion)); copy.testedTupleDigest=out.Text(b.Text(a.testedTupleDigest));
    copy.receipt.contract=out.Text(b.Text(a.receipt.contract)); copy.receipt.version=out.Text(b.Text(a.receipt.version)); copy.receipt.id=out.Text(b.Text(a.receipt.id)); copy.receipt.verdict=out.Text(b.Text(a.receipt.verdict)); copy.receipt.lineage=out.Text(b.Text(a.receipt.lineage)); copy.receipt.sha256=out.Text(b.Text(a.receipt.sha256)); return copy;
}
}
bool EqualAssertion(const Assertion& a,const Batch& x,const Assertion& b,const Batch& y) noexcept {
    if(a.kind!=b.kind) return false;
    if(a.kind==AssertionKind::Scalar) {
        if(a.scalar!=b.scalar) return false;
        switch(a.scalar) {
            case ScalarType::Bool: case ScalarType::U64: return a.unsignedValue==b.unsignedValue;
            case ScalarType::I64: return a.signedValue==b.signedValue;
            case ScalarType::F64: return a.realValue==b.realValue;
            case ScalarType::Text: return x.Text(a.text)==y.Text(b.text);
        }
    }
    if(a.kind==AssertionKind::OwnerReceipt) return SameReceipt(a.receipt,x,b.receipt,y);
    if(a.code!=b.code) return false;
    if(a.kind==AssertionKind::Result) return a.sideEffects==b.sideEffects;
    if(a.kind==AssertionKind::Qualification) return x.Text(a.protocol)==y.Text(b.protocol)&&x.Text(a.protocolVersion)==y.Text(b.protocolVersion)&&x.Text(a.testedTupleDigest)==y.Text(b.testedTupleDigest);
    return true;
}
QueryResult Query(const Snapshot& snapshot,const QueryRequest& request) noexcept {
    QueryResult result; result.snapshotRevision=snapshot.revision;
    try {
        result.originSession=snapshot.session; Validator validator{snapshot,request,result}; BatchBuilder values;
        std::array<FactRow,256> matches{}; size_t matched=0; std::array<FactRow,256> direct{},inferences{}; size_t directs=0,inferredCount=0;
        for(const auto& image:snapshot.streams) for(size_t i=0;i<image.batch.factCount;++i) {
            const auto& f=image.batch.At<Fact>(image.batch.facts[i]); if(f.capability!=request.capability||image.batch.Text(f.subject)!=request.subject) continue;
            bool match=true; for(size_t j=0;j<f.qualifiers.size();++j) if(image.batch.Text(f.qualifiers[j])!=request.qualifiers[j]) match=false;
            if(!match) continue; if(matched==matches.size()) { result.reasons={"CAP_QUERY_CAPACITY"}; return result; } matches[matched++]={&f,&image};
        }
        for(size_t i=0;i<matched;++i) {
            auto row=matches[i]; bool inferred=false;
            if(validator.FactCurrent(row,0,inferred)&&validator.ClosureEligible(row,inferred)) {
                if(inferred) inferences[inferredCount++]=row;
                else direct[directs++]=row;
            } else result.ignoredFactIds.emplace_back(row.image->batch.Text(row.fact->id));
        }
        if(validator.capacity) { result.reasons={"CAP_QUERY_CAPACITY"}; result.acceptedFactIds.clear(); result.candidates.clear(); result.evidenceIds.clear(); result.lineageIds.clear(); return result; }
        const bool explain=request.mode==QueryMode::ExplainAsObserved;
        auto count=directs?directs:inferredCount; auto& candidates=directs?direct:inferences;
        if(explain) { count=matched; candidates=matches; }
        std::array<FactRow,16> unique{}; size_t distinct=0;
        for(size_t i=0;i<count;++i) {
            auto row=candidates[i]; if(explain&&row.fact->collection!=Collection::Known) continue;
            if(!explain) result.acceptedFactIds.emplace_back(row.image->batch.Text(row.fact->id));
            bool equal=false; for(size_t j=0;j<distinct;++j) equal|=EqualAssertion(unique[j].fact->assertion,unique[j].image->batch,row.fact->assertion,row.image->batch);
            if(equal) continue;
            if(distinct==16) { result.reasons={"CAP_QUERY_CAPACITY"}; result.acceptedFactIds.clear(); result.candidates.clear(); result.evidenceIds.clear(); result.lineageIds.clear(); return result; }
            unique[distinct++]=row; result.candidates.push_back(CopyAssertion(row.fact->assertion,row.image->batch,values));
        }
        result.values=values.Finish();
        if(explain) { result.reasons.emplace_back("CAP_EXPLAIN_ONLY"); return result; }
        if(directs) { result.resolution=distinct>1?Resolution::Conflict:Resolution::Resolved; if(distinct==1) result.assertion=result.candidates[0]; }
        else if(inferredCount) result.resolution=Resolution::Inferred;
        return result;
    } catch(...) { result.resolution=Resolution::Unknown; result.assertion.reset(); result.candidates.clear(); result.acceptedFactIds.clear(); return result; }
}
}
