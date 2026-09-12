#include "dlssnr/FrameTrace.h"
#include <spdlog/sinks/base_sink.h>
#include <cassert>
#include <iostream>
#include <mutex>

class Sink : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    size_t count = 0;
    std::string first, last;
  protected:
    void sink_it_(const spdlog::details::log_msg& message) override
    {
        last.assign(message.payload.data(), message.payload.size());
        if (!count++) first = last;
    }
    void flush_() override {}
};
int main(int argc, char** argv)
{
    assert(argc == 2);
    const bool enabled = std::string_view(argv[1]) == "on";
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION", enabled ? "0123456789abcdef0123456789abcdef" :
        std::string_view(argv[1]) == "invalid" ? "bad-session" : nullptr);
    auto sink = std::make_shared<Sink>();
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("frame-trace-test", sink));
    assert(DlssNr::FrameTrace::Armed() == enabled);
    int evaluated = 0;
    NR_FRAME_TRACE("probe", "value={}", ++evaluated);
    if (!enabled)
    {
        assert(evaluated == 0 && sink->count == 0);
        assert(DlssNr::FrameTrace::Event("probe", "value={}", 42) == 0);
    }
    else
    {
        assert(evaluated == 1);
        assert(sink->first.find("NR_FRAME_TRACE v=1 session=0123456789abcdef0123456789abcdef") == 0);
        assert(sink->first.find(" seq=1 native=0 present=0 kind=probe classification=unknown value=1") != std::string::npos);
        for (uint64_t i = 0; i < DlssNr::FrameTrace::Budget::Limit + 100; ++i)
            NR_FRAME_TRACE("probe", "value={}", i);
        assert(sink->count == DlssNr::FrameTrace::Budget::Limit);
        assert(sink->last.find("kind=trace-ended classification=unknown reason=event-budget-exhausted") != std::string::npos);
    }
    std::cout << "PASS: trace logger " << argv[1] << "\n";
}
