#pragma once
#include "CapabilityContract.h"
#include "CapabilityRegistry.h"
#include <atomic>
#include <mutex>
namespace DlssNr::Capability {
struct WriterCell;
struct StoreState;
struct TestAccess;
class WriterPort {
    std::shared_ptr<WriterCell> cell;
    explicit WriterPort(std::shared_ptr<WriterCell> c):cell(std::move(c)){}
    friend class AdapterFactory;
    friend SequenceResult ReserveSample(const WriterPort&) noexcept;
    friend PublishResult TryPublish(const WriterPort&,uint64_t,const Batch&) noexcept;
    friend StageResult TryStageEnvelope(const WriterPort&,uint64_t,const Batch&) noexcept;
    friend void CloseWriter(const WriterPort&) noexcept;
    friend struct TestAccess;
  public:
    uint64_t Id() const noexcept;
    std::string_view Session() const noexcept;
    std::string RecordId(uint64_t,std::string_view) const;
};
class Store {
    std::shared_ptr<StoreState> state;
    friend class AdapterFactory;
    friend struct TestAccess;
  public:
    explicit Store(std::string session);
    CaptureResult Capture(const Selection&) noexcept;
};
enum class RegisterState { Registered, InvalidProducer, Capacity, Unavailable, Closed };
struct RegisterResult { RegisterState state=RegisterState::Unavailable; std::optional<WriterPort> port; };
// Trusted wiring only. ConsumerView intentionally does not expose this header.
class AdapterFactory {
    std::shared_ptr<StoreState> state;
  public:
    explicit AdapterFactory(Store& store):state(store.state){}
    RegisterResult RegisterWriter(ProducerId,std::string_view subjectGroup) noexcept;
    std::vector<PublishResult> DrainStagedObservations(const Selection&) noexcept;
};
SequenceResult ReserveSample(const WriterPort&) noexcept;
PublishResult TryPublish(const WriterPort&,uint64_t,const Batch&) noexcept;
void CloseWriter(const WriterPort&) noexcept;
PublishResult ValidateBatch(const Batch&,ProducerId,uint64_t,uint64_t,std::string_view) noexcept;
}
