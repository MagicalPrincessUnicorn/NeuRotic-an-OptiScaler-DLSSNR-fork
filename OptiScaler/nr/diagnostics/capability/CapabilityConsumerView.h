#pragma once
#include "CapabilityQuery.h"
namespace DlssNr::Capability {
// A consumer can capture retained values and run pure queries, but cannot register
// writers, collect owners, drain events, change settings or obtain execution rights.
class ConsumerView {
    CaptureResult (*capture)(const Selection&) noexcept;
    explicit ConsumerView(CaptureResult (*fn)(const Selection&) noexcept):capture(fn){}
    friend ConsumerView CapabilityView() noexcept;
  public:
    CaptureResult Capture(const Selection& selection={}) const noexcept { return capture(selection); }
    QueryResult Evaluate(const Snapshot& snapshot,const QueryRequest& request) const noexcept { return Query(snapshot,request); }
};
ConsumerView CapabilityView() noexcept;
}
