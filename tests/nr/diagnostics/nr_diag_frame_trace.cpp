#include <dlssnr/FrameTrace.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace M0 = Neurotic::Diagnostics::M0;
namespace C = Neurotic::Contracts;

int main(int argc, char** argv)
{
    assert(argc == 2);
    const bool shadow = std::strcmp(argv[1], "shadow") == 0;
    assert(shadow || std::strcmp(argv[1], "off") == 0);

    _putenv_s("NEUROTIC_FRAME_TRACE_SESSION", "");
    _putenv_s("NEUROTIC_FRAME_TRACE_TRIGGER", "");
    _putenv_s("NEUROTIC_FRAME_TRACE_PROFILE", "");
    _putenv_s("NEUROTIC_CONTRACT_OBSERVATION", shadow ? "Shadow" : "Off");

    int builderCalls = 0;
    const auto publisher = DlssNr::FrameTrace::WithM0Publisher([&]() noexcept {
        ++builderCalls;
        auto source = M0::SourceSnapshot::OwnerPublication(
            C::OwnerDomain::Presentation, "Test.FrameTrace", "nr_diag_frame_trace", 1, "OriginalPresent");
        source.Add("alpha.presentFlags", std::uint64_t {0});
        return source;
    });
    (void) publisher;

    int formattedArgument = 0;
    NR_FRAME_TRACE("original-present-enter", "argument={}", ++formattedArgument);
    assert(formattedArgument == 0); // FrameTrace remains off, so its variadic payload is not evaluated.

    M0::EventClosure event;
    if (shadow)
    {
        assert(builderCalls == 1);
        assert(M0::TryPop(event));
        assert(event.event.eventType.View() == "original-present-enter");
        assert(event.mirroredFacts.Size() == 1);
        assert(!M0::TryPop(event));
    }
    else
    {
        assert(builderCalls == 0);
        assert(!M0::TryPop(event));
    }

    std::cout << "PASS: FrameTrace/M0 seam " << argv[1] << " candidate case\n";
}
