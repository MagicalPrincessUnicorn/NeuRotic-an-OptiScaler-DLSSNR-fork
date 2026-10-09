#pragma once
#include "StreamCoordinatorKernel.h"
namespace Neurotic::Orchestration
{
class InitialFixedNativeCoordinator
{
    friend class Lifecycle::NativeSessionLifetime;
    std::shared_ptr<Lifecycle::NativeSessionLifetime> root_;
    explicit InitialFixedNativeCoordinator(std::shared_ptr<Lifecycle::NativeSessionLifetime> root):root_(std::move(root)){}
  public:
    InitResult TryActivate();
    InitResult TryReadCurrent()const;
    void Revoke(RevocationCause);
    CoordinatorSnapshot Inspect()const;
};
}
