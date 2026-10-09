#include "CapabilityInternal.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <charconv>
namespace DlssNr::Capability {
namespace {
CaptureResult UnavailableCapture(std::string_view reason) noexcept {
    CaptureResult result;
    // Diagnostics must survive even when no allocation is available for the
    // explanatory string. Unavailable state and absent snapshot remain enough.
    try { result.reason=reason; } catch(...) {}
    return result;
}
bool Identifier(std::string_view s) noexcept {
    if(s.empty()||s.size()>128) return false;
    for(unsigned char c:s) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c==':'||c=='/'||c=='@'||c=='+'||c=='-')) return false;
    return true;
}
bool Utf8(std::string_view text) noexcept {
    for(size_t i=0;i<text.size();) {
        auto first=static_cast<unsigned char>(text[i++]); if(first<0x80) continue;
        unsigned count=0; uint32_t value=0,minimum=0;
        if(first>=0xc2&&first<=0xdf) { count=1; value=first&31; minimum=0x80; }
        else if(first>=0xe0&&first<=0xef) { count=2; value=first&15; minimum=0x800; }
        else if(first>=0xf0&&first<=0xf4) { count=3; value=first&7; minimum=0x10000; }
        else return false;
        if(count>text.size()-i) return false;
        while(count--) { auto next=static_cast<unsigned char>(text[i++]); if((next&0xc0)!=0x80) return false; value=(value<<6)|(next&63); }
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff)) return false;
    } return true;
}
bool TextValid(const Batch& b,TextRef r,size_t max=512) noexcept { return r.size<=max && r.offset<=b.used && r.size<=b.used-r.offset&&Utf8(b.Text(r)); }
template<class T> bool ListValid(const Batch& b,ListRef r,size_t max) noexcept { return r.count<=max && r.offset%alignof(T)==0 && r.offset<=b.used && r.count<=(b.used-r.offset)/sizeof(T); }
bool StringsValid(const Batch& b,ListRef r,size_t max,bool identifiers) noexcept {
    if(!ListValid<TextRef>(b,r,max)) return false;
    for(auto t:b.List<TextRef>(r)) if(!TextValid(b,t)|| (identifiers&&!Identifier(b.Text(t)))) return false;
    return true;
}
const Scope* ScopeFor(const Batch& b,TextRef id) noexcept { for(size_t i=0;i<b.scopeCount;++i) { const auto& s=b.At<Scope>(b.scopes[i]); if(b.Text(s.id)==b.Text(id)) return &s; } return nullptr; }
bool ReceiptValid(const Batch& b,const OwnerReceipt& r,bool required) noexcept {
    if(!required&&r.id.size==0) return true;
    return Identifier(b.Text(r.contract))&&Identifier(b.Text(r.version))&&Identifier(b.Text(r.id))&&Identifier(b.Text(r.lineage))&&TextValid(b,r.verdict)&&TextValid(b,r.sha256,64);
}
bool AssertionValid(const Batch& b,const Fact& f,const RegistryEntry& reg) noexcept {
    if(f.collection!=Collection::Known) return true;
    const auto& a=f.assertion; if(a.kind!=reg.kind) return false;
    if(a.kind==AssertionKind::Scalar) {
        if(a.scalar>ScalarType::Text||(reg.scalarRestricted&&a.scalar!=reg.scalar)) return false;
        return (a.scalar!=ScalarType::Bool||a.unsignedValue<=1)&&(a.scalar!=ScalarType::F64||std::isfinite(a.realValue))&&TextValid(b,a.text);
    }
    if(a.kind==AssertionKind::OwnerReceipt) return ReceiptValid(b,a.receipt,true);
    if(a.kind==AssertionKind::Qualification) return a.code<=2&&Identifier(b.Text(a.protocol))&&Identifier(b.Text(a.protocolVersion))&&b.Text(a.testedTupleDigest).size()==64;
    if(a.kind==AssertionKind::Result) return a.code<=2&&a.sideEffects<=5;
    return a.code<=(a.kind==AssertionKind::Health?5:1);
}
template<class T,size_t N> bool TableValid(const Batch& b,const std::array<uint32_t,N>& offsets,size_t count) noexcept {
    if(count>N) return false;
    for(size_t i=0;i<count;++i) if(offsets[i]<8||offsets[i]%alignof(T)||offsets[i]>b.used||sizeof(T)>b.used-offsets[i]) return false;
    return true;
}
bool Cycles(const Batch& b,bool facts,size_t index,std::array<uint8_t,64>& colors,unsigned depth) noexcept {
    if(depth>16||colors[index]==1) return true; if(colors[index]==2) return false; colors[index]=1;
    auto refs=facts?b.At<Fact>(b.facts[index]).dependencies:b.At<Evidence>(b.evidence[index]).parents;
    const size_t count=facts?b.factCount:b.evidenceCount;
    for(auto id:b.List<TextRef>(refs)) for(size_t j=0;j<count;++j) {
        auto target=facts?b.At<Fact>(b.facts[j]).id:b.At<Evidence>(b.evidence[j]).id;
        if(b.Text(id)==b.Text(target)&&Cycles(b,facts,j,colors,depth+1)) return true;
    }
    colors[index]=2; return false;
}
}
PublishResult ValidateBatch(const Batch& b,ProducerId producer,uint64_t writer,uint64_t sequence,std::string_view session) noexcept {
    if(b.overflow||b.used>Batch::Capacity) return PublishResult::CapacityExceeded;
    if(!sequence||!Identifier(session)||producer>=ProducerId::Count||!TableValid<Scope>(b,b.scopes,b.scopeCount)||!TableValid<Evidence>(b,b.evidence,b.evidenceCount)||!TableValid<Fact>(b,b.facts,b.factCount)) return PublishResult::InvalidBatch;
    std::array<std::string_view,128> ids{}; size_t count=0;
    std::array<char,192> prefix{}; size_t length=session.size(); std::memcpy(prefix.data(),session.data(),length);
    std::memcpy(prefix.data()+length,":w",2); length+=2;
    auto wr=std::to_chars(prefix.data()+length,prefix.data()+prefix.size(),writer); length=static_cast<size_t>(wr.ptr-prefix.data());
    std::memcpy(prefix.data()+length,":s",2); length+=2;
    auto sr=std::to_chars(prefix.data()+length,prefix.data()+prefix.size(),sequence); length=static_cast<size_t>(sr.ptr-prefix.data()); prefix[length++]=':';
    auto unique=[&](TextRef id) { if(!TextValid(b,id,128)||!Identifier(b.Text(id))||!b.Text(id).starts_with(std::string_view(prefix.data(),length))) return false; for(size_t j=0;j<count;++j) if(ids[j]==b.Text(id)) return false; ids[count++]=b.Text(id); return true; };
    for(size_t i=0;i<b.scopeCount;++i) {
        const auto& s=b.At<Scope>(b.scopes[i]); if(!unique(s.id)||b.Text(s.session)!=session||s.role>ScopeRole::Historical||!ListValid<OwnerStamp>(b,s.bindings,22)||!s.bindings.count) return PublishResult::InvalidBatch;
        uint32_t dimensions=0;
        for(auto stamp:b.List<OwnerStamp>(s.bindings)) {
            auto d=static_cast<unsigned>(stamp.dimension); if(d>=22||(dimensions&(1u<<d))||stamp.status>StampStatus::NotApplicable) return PublishResult::InvalidBatch; dimensions|=1u<<d;
            if(stamp.status==StampStatus::Known) { if(!Identifier(b.Text(stamp.owner))||!Identifier(b.Text(stamp.instance))) return PublishResult::InvalidBatch; }
            else if(stamp.owner.size||stamp.instance.size||stamp.generation) return PublishResult::InvalidBatch;
        }
    }
    for(size_t i=0;i<b.evidenceCount;++i) {
        const auto& e=b.At<Evidence>(b.evidence[i]);
        if(!unique(e.id)||!ScopeFor(b,e.scope)||e.writer!=writer||e.sequence!=sequence||e.source>=SourceKind::Count||e.basis>Basis::T6||e.mutation>Mutation::Unknown||e.upstream>UpstreamBinding::NotApplicable||!Identifier(b.Text(e.method))||!StringsValid(b,e.lineages,16,true)||!e.lineages.count||!StringsValid(b,e.parents,16,true)||!StringsValid(b,e.limitations,16,false)||!StringsValid(b,e.legacyIds,32,true)||!TextValid(b,e.locator)||!TextValid(b,e.sha256,64)||!ReceiptValid(b,e.semanticReceipt,false)) return PublishResult::InvalidBatch;
    }
    for(size_t i=0;i<b.factCount;++i) {
        const auto& f=b.At<Fact>(b.facts[i]); auto reg=Lookup(f.capability);
        if(!reg) return PublishResult::InvalidBatch;
        if(!(reg->producers&(1u<<static_cast<unsigned>(producer)))) return PublishResult::ProducerNotAllowed;
        auto scope=ScopeFor(b,f.scope);
        if(!unique(f.id)||!Identifier(b.Text(f.subject))||!scope||f.writer!=writer||f.sequence!=sequence||f.collection>Collection::Error||!StringsValid(b,f.evidence,16,true)||!StringsValid(b,f.dependencies,16,true)||!StringsValid(b,f.reasons,16,true)||!StringsValid(b,f.limitations,16,false)||!StringsValid(b,f.legacyFields,32,false)||!AssertionValid(b,f,*reg)) return PublishResult::InvalidBatch;
        if(f.collection==Collection::Known&&!f.evidence.count) return PublishResult::InvalidBatch;
        uint32_t qualifiers=0; for(size_t j=0;j<f.qualifiers.size();++j) { if(!TextValid(b,f.qualifiers[j],192)) return PublishResult::InvalidBatch; if(f.qualifiers[j].size) qualifiers|=1u<<j; }
        if((qualifiers&reg->qualifiers)!=reg->qualifiers) return PublishResult::InvalidBatch;
        auto field=b.Text(f.qualifiers[0]);
        if(reg->id=="nr.cap.fingerprint.field") {
            bool registeredPrefix=false; for(auto p:{"/collector/","/subject/","/adapter/","/driver/","/modules/","/feature_target/"}) registeredPrefix|=field.starts_with(p);
            if(!registeredPrefix) return PublishResult::InvalidBatch;
        }
        if(reg->id=="nr.cap.present.counter"&&b.Text(f.qualifiers[static_cast<size_t>(Qualifier::ScopeCategory)])!="process_summary") return PublishResult::InvalidBatch;
        auto feature=b.Text(f.qualifiers[static_cast<size_t>(Qualifier::FeatureId)]);
        if(!feature.empty()) { uint32_t value; auto parsed=std::from_chars(feature.data(),feature.data()+feature.size(),value); if(parsed.ec!=std::errc{}||parsed.ptr!=feature.data()+feature.size()||(feature.size()>1&&feature.front()=='0')) return PublishResult::InvalidBatch; }
        auto phase=b.Text(f.qualifiers[static_cast<size_t>(Qualifier::ParameterPhase)]);
        if(!phase.empty()&&phase!="declaration"&&phase!="creation"&&phase!="evaluation"&&phase!="readback") return PublishResult::InvalidBatch;
        if(!reg->fields.empty()) {
            bool found=false; auto fields=reg->fields; while(!fields.empty()) { auto end=fields.find('|'); if(fields.substr(0,end)==field) found=true; if(end==std::string_view::npos) break; fields.remove_prefix(end+1); } if(!found) return PublishResult::InvalidBatch;
        }
        uint32_t dimensions=0; for(auto stamp:b.List<OwnerStamp>(scope->bindings)) { dimensions|=1u<<static_cast<unsigned>(stamp.dimension); if(stamp.status==StampStatus::NotApplicable) return PublishResult::InvalidBatch; }
        auto required=RequiredDomains(*reg,field); if((dimensions&required)!=required) return PublishResult::InvalidBatch;
    }
    std::array<uint8_t,64> colors{};
    for(size_t i=0;i<b.factCount;++i) if(Cycles(b,true,i,colors,0)) return PublishResult::InvalidBatch;
    colors.fill(0); for(size_t i=0;i<b.evidenceCount;++i) if(Cycles(b,false,i,colors,0)) return PublishResult::InvalidBatch;
    return PublishResult::Published;
}
Store::Store(std::string session):state(std::make_shared<StoreState>(std::move(session))) { if(!Identifier(state->session)) state->closed=true; }
uint64_t WriterPort::Id() const noexcept { return cell->id; }
std::string_view WriterPort::Session() const noexcept { return cell->session; }
std::string WriterPort::RecordId(uint64_t sequence,std::string_view ordinal) const { return cell->session+":w"+std::to_string(cell->id)+":s"+std::to_string(sequence)+":"+std::string(ordinal); }
RegisterResult AdapterFactory::RegisterWriter(ProducerId producer,std::string_view group) noexcept {
    try {
        if(producer>=ProducerId::Ui||!Identifier(group)) return {RegisterState::InvalidProducer,{}};
        auto cell=std::make_shared<WriterCell>(); cell->active=std::make_unique<Batch>(); cell->pending=std::make_unique<Batch>();
        cell->session=state->session; cell->subjectGroup=group; cell->producer=producer; cell->store=state;
        std::lock_guard lock(state->mutex); if(state->closed||state->nextWriter==UINT64_MAX) return {RegisterState::Closed,{}};
        for(auto& slot:state->slots) if(!slot||slot->closed.load()) { cell->id=++state->nextWriter; slot=cell; return {RegisterState::Registered,WriterPort(cell)}; }
        return {RegisterState::Capacity,{}};
    } catch(...) { return {RegisterState::Unavailable,{}}; }
}
SequenceResult ReserveSample(const WriterPort& port) noexcept {
    auto& c=*port.cell; if(c.closed.load()) return {};
    auto offered=c.offered.load();
    for(;;) { if(offered==UINT64_MAX) { c.closed.store(true); return {SequenceState::Exhausted,0}; }
        if(c.offered.compare_exchange_weak(offered,offered+1)) return c.closed.load()?SequenceResult{}:SequenceResult{SequenceState::Reserved,offered+1}; }
}
void CloseWriter(const WriterPort& port) noexcept { port.cell->closed.store(true); }
PublishResult TryPublish(const WriterPort& port,uint64_t sequence,const Batch& batch) noexcept {
    const auto& c=port.cell; if(c->closed.load()) return PublishResult::Closed;
    auto valid=ValidateBatch(batch,c->producer,c->id,sequence,c->session); if(valid!=PublishResult::Published) return valid;
    auto state=c->store.lock(); if(!state) return PublishResult::Closed;
    std::unique_lock lock(state->mutex,std::try_to_lock); if(!lock.owns_lock()) return PublishResult::Contended;
    if(c->closed.load()||state->closed) return PublishResult::Closed;
    if(sequence!=c->offered.load()||sequence<=c->committed.load()) return PublishResult::StaleSequence;
    if(state->revision==UINT64_MAX) { c->closed.store(true); return PublishResult::Closed; }
    // IDs are session/writer/sample scoped; reject aliases with retained other streams.
    for(const auto& other:state->slots) if(other&&other!=c) {
        const auto& b=*other->active;
        for(size_t i=0;i<batch.factCount;++i) for(size_t j=0;j<b.factCount;++j) if(batch.Text(batch.At<Fact>(batch.facts[i]).id)==b.Text(b.At<Fact>(b.facts[j]).id)) return PublishResult::InvalidBatch;
        for(size_t i=0;i<batch.evidenceCount;++i) for(size_t j=0;j<b.evidenceCount;++j) if(batch.Text(batch.At<Evidence>(batch.evidence[i]).id)==b.Text(b.At<Evidence>(b.evidence[j]).id)) return PublishResult::InvalidBatch;
    }
    *c->active=batch; c->committed.store(sequence); ++state->revision; return PublishResult::Published;
}
CaptureResult Store::Capture(const Selection& selection) noexcept {
    (void)selection; // Bounded full closure; queries apply selection to immutable values.
    auto n=state->snapshots.load(); do { if(n>=4) return UnavailableCapture("CAP_SNAPSHOT_CAPACITY"); } while(!state->snapshots.compare_exchange_weak(n,n+1));
    bool owned=false;
    try {
        auto stateCopy=state;
        auto raw=new Snapshot;
        // shared_ptr calls its deleter if control-block allocation fails.
        owned=true;
        auto snapshot=std::shared_ptr<Snapshot>(raw,[stateCopy](Snapshot* p){delete p; --stateCopy->snapshots;});
        snapshot->session=state->session; snapshot->streams.resize(128); size_t used=0; CaptureState capture=CaptureState::Complete;
        { std::lock_guard lock(state->mutex); snapshot->revision=state->revision;
            for(auto& cell:state->slots) if(cell) {
                auto& image=snapshot->streams[used++]; image.writer=cell->id; image.producer=cell->producer;
                auto offered=cell->offered.load(); auto committed=cell->committed.load(); auto closed=cell->closed.load(); image.batch=*cell->active;
                image.offered=cell->offered.load(); image.committed=cell->committed.load(); image.closed=cell->closed.load();
                image.raced=offered!=image.offered||committed!=image.committed||closed!=image.closed;
                if(image.raced) capture=CaptureState::Raced; else if(image.offered!=image.committed&&capture==CaptureState::Complete) capture=CaptureState::Gap;
            }
        }
        snapshot->streams.resize(used); return {capture,std::move(snapshot),{}};
    } catch(...) { if(!owned) --state->snapshots; return UnavailableCapture("CAP_CAPTURE_UNAVAILABLE"); }
}
}
