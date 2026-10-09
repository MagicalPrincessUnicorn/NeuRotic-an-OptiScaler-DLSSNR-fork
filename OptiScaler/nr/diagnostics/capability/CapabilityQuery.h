#pragma once
#include "CapabilityContract.h"
namespace DlssNr::Capability {
QueryResult Query(const Snapshot&,const QueryRequest&) noexcept;
bool EqualAssertion(const Assertion&,const Batch&,const Assertion&,const Batch&) noexcept;
}
