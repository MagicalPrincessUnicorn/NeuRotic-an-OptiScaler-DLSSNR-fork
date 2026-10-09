#pragma once
#include "CapabilityStore.h"
namespace DlssNr::Capability {
using PendingEnvelope=Batch;
StageResult TryStageEnvelope(const WriterPort&,uint64_t,const PendingEnvelope&) noexcept;
}
