#include "dlssnr/FrameTrace.h"
#include <spdlog/sinks/base_sink.h>
#include <cassert>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

class Sink : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    size_t count = 0;
    size_t controls = 0, starts = 0;
    std::string first, last;
  protected:
    void sink_it_(const spdlog::details::log_msg& message) override
    {
        last.assign(message.payload.data(), message.payload.size());
        if (last.find("NR_FRAME_TRACE_CONTROL ") == 0) ++controls;
        if (last.find("kind=trace-started ") != std::string::npos) ++starts;
        if (!count++) first = last;
    }
    void flush_() override {}
};
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string_view mode(argv[1]);
    const bool delayed = mode == "delayed";
    const bool enabled = mode == "on" || delayed;
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION", enabled || mode == "invalid-trigger" ?
        "0123456789abcdef0123456789abcdef" : mode == "invalid" ? "bad-session" : nullptr);
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_TRIGGER", delayed ? "nr-enable" :
        mode == "invalid-trigger" ? "unknown-trigger" : nullptr);
    auto sink = std::make_shared<Sink>();
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("frame-trace-test", sink));
    assert(DlssNr::FrameTrace::Armed() == (enabled && !delayed));
    int evaluated = 0;
    if (delayed)
    {
        assert(sink->count == 1 && sink->controls == 1);
        assert(sink->first.find("state=waiting-for-nr-enable") != std::string::npos);
        for (uint64_t i = 0; i < DlssNr::FrameTrace::Budget::Limit + 100; ++i)
        {
            NR_FRAME_TRACE("startup", "value={}", ++evaluated);
            assert(DlssNr::FrameTrace::Event("direct-startup", "value={}", i) == 0);
        }
        assert(evaluated == 0 && sink->count == 1);
        DlssNr::FrameTrace::OnNrEnable(false, false);
        DlssNr::FrameTrace::OnNrEnable(true, false);
        DlssNr::FrameTrace::OnNrEnable(true, true);
        assert(!DlssNr::FrameTrace::Armed());
        std::vector<std::thread> workers;
        for (unsigned i = 0; i < 8; ++i)
            workers.emplace_back([] { DlssNr::FrameTrace::OnNrEnable(false, true); });
        for (auto& worker : workers) worker.join();
        assert(DlssNr::FrameTrace::Armed() && sink->starts == 1 && sink->count == 2);
        assert(sink->last.find("seq=1 native=0 present=0 kind=trace-started") != std::string::npos);
        DlssNr::FrameTrace::OnNrEnable(true, false);
        DlssNr::FrameTrace::OnNrEnable(false, true);
        assert(DlssNr::FrameTrace::Armed() && sink->starts == 1);
    }
    NR_FRAME_TRACE("probe", "value={}", ++evaluated);
    if (!enabled)
    {
        assert(evaluated == 0 && sink->count == 0);
        assert(DlssNr::FrameTrace::Event("probe", "value={}", 42) == 0);
        DlssNr::FrameTrace::OnNrEnable(false, true);
        assert(!DlssNr::FrameTrace::Armed() && sink->count == 0);
    }
    else
    {
        assert(evaluated == 1);
        if (!delayed)
        {
            assert(sink->first.find("NR_FRAME_TRACE v=1 session=0123456789abcdef0123456789abcdef") == 0);
            assert(sink->first.find(" seq=1 native=0 present=0 kind=probe classification=unknown value=1") != std::string::npos);
        }
        for (uint64_t i = 0; i < DlssNr::FrameTrace::Budget::Limit + 100; ++i)
            NR_FRAME_TRACE("probe", "value={}", i);
        assert(sink->count == DlssNr::FrameTrace::Budget::Limit + sink->controls);
        assert(sink->last.find("kind=trace-ended classification=unknown reason=event-budget-exhausted") != std::string::npos);
        DlssNr::FrameTrace::OnNrEnable(false, true);
        assert(DlssNr::FrameTrace::Event("after-budget", "value={}", 42) == 0);
        assert(sink->count == DlssNr::FrameTrace::Budget::Limit + sink->controls);
    }
    std::cout << "PASS: trace logger " << argv[1] << "\n";
}
