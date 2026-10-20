// Module-local observation of actual SDK cleanup return values. No release
// authority is created: the enclosing owner still needs exclusion and GPU drain.
#pragma once
#include <cstdint>
namespace ffx::nr {
class AlgorithmCleanup final {
    inline static thread_local AlgorithmCleanup* current_=nullptr;
    AlgorithmCleanup* previous_;
    std::int32_t firstError_=0;
    std::uint64_t operations_=0;
public:
    AlgorithmCleanup()noexcept:previous_(current_){current_=this;}
    ~AlgorithmCleanup(){current_=previous_;}
    AlgorithmCleanup(const AlgorithmCleanup&)=delete;
    AlgorithmCleanup& operator=(const AlgorithmCleanup&)=delete;
    static void Observe(std::int32_t actualResult)noexcept {
        for(auto* scope=current_;scope;scope=scope->previous_){
            ++scope->operations_;
            if(actualResult!=0 && scope->firstError_==0)scope->firstError_=actualResult;
        }
    }
    std::int32_t Result()const noexcept{return firstError_;}
    std::uint64_t Operations()const noexcept{return operations_;}
};
}
