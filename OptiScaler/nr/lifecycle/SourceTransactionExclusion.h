#pragma once
#include <nr/contracts/InterceptedSourceTransaction.h>
namespace Neurotic::Lifecycle
{
class SourceTransactionExclusion
{
    friend class NativeSourceTransactionObservation;
    Contracts::InterceptedSourceTransactionV1 transaction_;
    explicit SourceTransactionExclusion(Contracts::InterceptedSourceTransactionV1 transaction):transaction_(std::move(transaction)){}
  public:
    const Contracts::InterceptedSourceTransactionV1& Transaction()const noexcept{return transaction_;}
};
}
