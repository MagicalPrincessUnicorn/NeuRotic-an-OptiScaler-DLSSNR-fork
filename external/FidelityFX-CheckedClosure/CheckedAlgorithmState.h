// Creating-module state; applications consume CheckedAlgorithm.h only.
#pragma once
#include "CheckedAlgorithm.h"
#include "CheckedAlgorithmCleanup.h"
#include "CheckedSdkRuntime.h"
#include <functional>
#include <vector>
namespace ffx::nr {
class AlgorithmState;
inline thread_local AlgorithmState* currentAlgorithm=nullptr;
inline thread_local AlgorithmState* algorithmDestroyPermit=nullptr;
class AlgorithmState final : public AlgorithmLease, public std::enable_shared_from_this<AlgorithmState> {
    inline static std::atomic<uint64_t> nextGeneration_{1};
    const uint64_t generation_=nextGeneration_.fetch_add(1);
    const bool selectable_;
    void* const device_;
    void* const original_;
    std::function<int32_t()> destroy_;
    std::shared_ptr<CreatedContext> owner_;
    FfxNrContextTicketV1 boundIdentity_{};
    std::shared_ptr<OwnedOutputLease> output_;
    std::weak_ptr<OwnedOutputLease> outputTombstone_;
    std::atomic<OwnedOutputLease*> outputIdentity_{nullptr};
    std::atomic<ID3D12Resource*> input_{nullptr};
    std::atomic<uint64_t> allocation_{0};
    std::vector<ID3D12Resource*> resources_;
    std::atomic<bool> selected_{false},closed_{false},active_{false},failed_{false},attempted_{false},released_{false},registered_{false};
    bool touched_=false,configured_=false,prepared_=false,dispatched_=false;
    uint64_t frame_=0;
    AlgorithmReleaseReceipt receipt_{};
    AlgorithmConsumerObserver observer_{};
    bool bindingPin_=false;
public:
    std::mutex mutex;
    class Scope {
        friend class AlgorithmState;
        AlgorithmState* state_=nullptr;
        std::unique_lock<std::mutex> lock_;
        std::unique_ptr<WorkScope> work_;
        CheckedClosure::Token child_;
        AlgorithmState* previous_=nullptr;
        Scope(AlgorithmState* state,std::unique_lock<std::mutex>&& lock,std::unique_ptr<WorkScope>&& work,CheckedClosure::Token child):state_(state),lock_(std::move(lock)),work_(std::move(work)),child_(std::move(child)),previous_(currentAlgorithm){state_->active_=true;currentAlgorithm=state_;}
    public:
        Scope()=default;
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
        Scope(Scope&& other)noexcept:state_(std::exchange(other.state_,nullptr)),lock_(std::move(other.lock_)),work_(std::move(other.work_)),child_(std::move(other.child_)),previous_(other.previous_){}
        Scope& operator=(Scope&& other)noexcept {this->~Scope();new(this)Scope(std::move(other));return *this;}
        ~Scope(){if(state_){currentAlgorithm=previous_;state_->active_=false;}}
        explicit operator bool()const{return state_!=nullptr;}
    };
    AlgorithmState(void* original,std::function<int32_t()> destroy,bool selectable=true,void* device=nullptr):selectable_(selectable),device_(device),original_(original),destroy_(std::move(destroy)){}
    bool selected()const noexcept{return selected_.load();}
    bool dead()const noexcept{return released_.load();}
    void touchLegacy(){std::lock_guard lock(mutex);touched_=true;}
    void* original()const noexcept{return original_;}
    std::shared_ptr<CreatedContext> owner()const{return owner_;}
    bool select(const std::shared_ptr<CreatedContext>& owner,const AlgorithmConsumerObserver& observer={}) {
        std::unique_lock lock(mutex,std::try_to_lock);
        if(!lock || !selectable_ || selected_ || touched_ || closed_ || !owner || (device_ && device_!=owner->deviceIdentity)) return false;
        owner_=owner;boundIdentity_=owner->identity;observer_=observer;
        if(owner_->retainPin){owner_->retainPin(owner_->object);bindingPin_=true;}
        selected_=true;return true;
    }
    Scope enter(bool destroyCall=false) {
        std::unique_lock lock(mutex,std::try_to_lock);
        if(!lock || closed_ || attempted_ || released_ || (selected_ && destroyCall))return {};
        std::unique_ptr<WorkScope> work;
        CheckedClosure::Token child;
        if(selected_) {
            if(activeContext==owner_.get() && activeToken){child=activeToken->child();if(!child)return {};}
            else {work=std::make_unique<WorkScope>(owner_->object);if(!*work)return {};}
        }
        else touched_=true;
        return Scope(this,std::move(lock),std::move(work),std::move(child));
    }
    AlgorithmSnapshot inspect()const noexcept override {
        AlgorithmSnapshot s{};if(selected_.load())s.swapchain=boundIdentity_;
        s.algorithmGeneration=generation_;s.allocationGeneration=allocation_.load();s.input=input_.load();
        s.registrationCreated=outputIdentity_.load()!=nullptr;s.registrationComplete=registered_.load();s.active=active_.load();s.failed=failed_.load();s.admissionClosed=closed_.load();s.destroyAttempted=attempted_.load();s.released=released_.load();return s;
    }
    bool Owns(const std::shared_ptr<OwnedOutputLease>& output)const noexcept override {return output && outputIdentity_.load()==output.get() && !outputTombstone_.owner_before(output) && !output.owner_before(outputTombstone_);}
    void closeAdmission()noexcept override {closed_=true;}
    void fail()noexcept {failed_=true;if(owner_)owner_->closure.recordFailure(E_FAIL);}
    bool bindOutput(const std::shared_ptr<OwnedOutputLease>& output) {
        std::unique_lock lock(mutex,std::try_to_lock);
        if(!lock || !selected_ || closed_ || output_ || !output || activeContext!=owner_.get() || owner_->ownedOutput!=output) return false;
        auto view=output->inspect();if(!view.resource || !view.ordinaryEscapeExcluded || !view.recyclingExcluded)return false;
        output_=output;outputTombstone_=output;input_=view.resource;allocation_=view.allocationGeneration;
        view.resource->AddRef();resources_.push_back(view.resource);
        registered_=true;outputIdentity_=output.get();return true;
    }
    bool configure(uint64_t frame) {
        if(currentAlgorithm!=this || configured_ || output_ || !selected_)return false;
        configured_=true;frame_=frame;return true;
    }
    bool prepare(void* list,uint64_t frame,ID3D12Resource* depth,ID3D12Resource* motion) {
        if(currentAlgorithm!=this || !configured_ || prepared_ || !registered_ || frame!=frame_ || !output_ || !depth || !motion) return false;
        auto view=output_->inspect();
        if(!view.writerActive || view.writerList!=list || !view.writerRecording || activeContext!=owner_.get() || currentOwnedWriter!=output_.get())return false;
        depth->AddRef();motion->AddRef();resources_.push_back(depth);resources_.push_back(motion);prepared_=true;return true;
    }
    bool dispatch(void* list,uint64_t frame,ID3D12Resource* input,ID3D12Resource* output) {
        if(currentAlgorithm!=this || !prepared_ || dispatched_ || frame!=frame_ || !currentDispatch || activeContext!=owner_.get() || input!=input_.load() || !output)return false;
        if(currentDispatch->command_list!=reinterpret_cast<uintptr_t>(list) || currentDispatch->present_color!=reinterpret_cast<uintptr_t>(input))return false;
        output->AddRef();resources_.push_back(output);dispatched_=true;return true;
    }
    class ConsumerScope {
        AlgorithmState* state_=nullptr;void* guard_=nullptr;int32_t result_=-1;
    public:
        explicit ConsumerScope(AlgorithmState* state):state_(state) {
            if(!state_ || !state_->observer_.begin_consumer || !state_->observer_.end_consumer || !currentDispatch){state_=nullptr;return;}
            try {if(state_->observer_.begin_consumer(state_->observer_.user,state_->output_,state_->shared_from_this(),currentDispatch,&guard_)!=FFX_NR_COMPLETE)state_=nullptr;}
            catch(...){state_->fail();state_=nullptr;}
        }
        ~ConsumerScope(){if(state_){if(result_!=0)state_->fail();try{state_->observer_.end_consumer(state_->observer_.user,guard_,result_);}catch(...){state_->fail();}}}
        explicit operator bool()const{return state_!=nullptr;}
        void succeeded(){result_=0;}
    };
    FfxNrStatusV1 releaseAfterDrain(const FfxNrDrainHandleV1& drain,uint64_t revision,AlgorithmReleaseReceipt& out)noexcept override {
        std::unique_lock lock(mutex,std::try_to_lock);
        if(!lock)return FFX_NR_BUSY;
        if(released_){out=receipt_;return receipt_.evidenceRevision==revision && std::memcmp(&receipt_.drain,&drain,sizeof(drain))==0?FFX_NR_COMPLETE:FFX_NR_STALE_CONTEXT;}
        if(attempted_){out=receipt_;return FFX_NR_API_FAILURE_RETAINED;}
        if(!selected_ || !closed_ || active_ || !owner_)return FFX_NR_PENDING;
        const auto valid=owner_->closure.validateComplete(drain,revision);if(valid!=FFX_NR_COMPLETE)return valid;
        attempted_=true;receipt_.algorithmGeneration=generation_;receipt_.drain=drain;receipt_.evidenceRevision=revision;
        AlgorithmCleanup cleanup;
        auto* previous=algorithmDestroyPermit;algorithmDestroyPermit=this;
        try {receipt_.actualResult=destroy_();}catch(...){receipt_.actualResult=-1;}
        algorithmDestroyPermit=previous;
        receipt_.cleanupOperations=cleanup.Operations();if(cleanup.Result()!=0)receipt_.actualResult=cleanup.Result();
        if(receipt_.actualResult!=0){failed_=true;out=receipt_;return FFX_NR_API_FAILURE_RETAINED;}
        for(auto* resource:resources_)resource->Release();resources_.clear();
        receipt_.wholeContextDestroyed=true;released_=true;out=receipt_;
        if(bindingPin_){bindingPin_=false;owner_->releasePin(owner_->object);}
        output_.reset();owner_.reset(); // Keep only immutable identity/weak control-block tombstones.
        return FFX_NR_COMPLETE;
    }
};
class AlgorithmRegistry {
    std::mutex mutex_;
    std::unordered_map<void*,std::shared_ptr<AlgorithmState>> contexts_;
public:
    bool add(void* p,std::function<int32_t()> destroy,bool selectable=true,void* device=nullptr){std::lock_guard lock(mutex_);auto it=contexts_.find(p);if(it!=contexts_.end() && !it->second->dead())return false;contexts_[p]=std::make_shared<AlgorithmState>(p,std::move(destroy),selectable,device);return true;}
    std::shared_ptr<AlgorithmState> find(void* p){std::lock_guard lock(mutex_);auto it=contexts_.find(p);return it==contexts_.end()?nullptr:it->second;}
    void removeLegacy(void* p){std::lock_guard lock(mutex_);auto it=contexts_.find(p);if(it!=contexts_.end() && !it->second->selected())contexts_.erase(it);}
};
inline AlgorithmRegistry& algorithmRegistry(){static AlgorithmRegistry registry;return registry;}
class AlgorithmApiScope {
    std::shared_ptr<AlgorithmState> state_;
    AlgorithmState::Scope scope_;
    bool allowed_=true;
public:
    explicit AlgorithmApiScope(void* exact,bool destroy=false):state_(algorithmRegistry().find(exact)) {
        if(!state_)return;
        if(!state_->selected()){state_->touchLegacy();return;}
        scope_=state_->enter(destroy);allowed_=bool(scope_);
    }
    explicit operator bool()const{return allowed_;}
};
}
