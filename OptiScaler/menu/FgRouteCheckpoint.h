#pragma once
#include <optional>
#include <cstdint>
namespace Neurotic {
// Cancellation checkpoint over the existing config owner, not a second draft.
// A successful save anywhere in the menu advances the checkpoint.
template<class Values> class FgRouteCheckpoint {
    std::optional<Values> saved_;uint64_t sequence_=0;
public:
    void Observe(const Values& current,uint64_t sequence,bool succeeded){
        if(!saved_||(sequence!=sequence_&&succeeded))saved_=current;
        sequence_=sequence;
    }
    bool Dirty(const Values& current)const{return saved_&&*saved_!=current;}
    const Values& Saved()const{return *saved_;}
};
}
