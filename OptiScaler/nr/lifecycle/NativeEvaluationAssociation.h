#pragma once
#include <nr/contracts/InterceptedSourceTransaction.h>

namespace Neurotic::Lifecycle
{
// An immutable association issued with the NR evaluation by the existing
// Evaluation owner. This does not change FrameIdentity or grant execution,
// Resource rights, original-game identity, or a canonical final seal.
class NativeEvaluationAssociation
{
    friend class EvaluationIssuer;
    Contracts::NativeSampleIdentityV1 sample_;
    Contracts::EpisodeId episode_;
    Contracts::EvaluationId evaluation_;
    NativeEvaluationAssociation(const Contracts::NativeSampleIdentityV1& sample,
        Contracts::EpisodeId episode,Contracts::EvaluationId evaluation)
        :sample_(sample),episode_(episode),evaluation_(evaluation){}
  public:
    const Contracts::EvaluationId& Evaluation()const noexcept{return evaluation_;}
    bool Matches(const Contracts::NativeSampleIdentityV1& sample,Contracts::EpisodeId episode,
        Contracts::EvaluationId evaluation)const noexcept
    {return sample_==sample&&episode_==episode&&evaluation_==evaluation;}
    bool Matches(const Contracts::InterceptedSourceTransactionV1& transaction,
        Contracts::EvaluationId evaluation)const noexcept
    {return Matches(transaction.sample,transaction.episode,evaluation);}
    bool operator==(const NativeEvaluationAssociation&)const=default;
};
}
