#include "../OptiScaler/dlssnr/NativeFgVulkan.h"
#include "../OptiScaler/dlssnr/NativeGuideRoute.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const int source = std::atoi(argv[1]);
    const bool requested = std::atoi(argv[2]) != 0;
    const bool expected = std::atoi(argv[3]) != 0;
    // Explicit input selection must override both legacy and environment requests.
    DlssNr::NativeGuides::Configure(true);
    DlssNr::NativeGuides::ConfigureInput(source, 0, true);
    DlssNr::NativeFg::Configure(requested);
    SetEnvironmentVariableW(L"NEUROTIC_NATIVE_GUIDES", L"1");
    SetEnvironmentVariableW(L"NEUROTIC_NATIVE_FG", requested ? L"1" : L"0");
    if (DlssNr::NativeFg::Selected() != expected) {
        std::fprintf(stderr, "FG selection incorrect for source=%d requested=%d\n", source, requested);
        return 1;
    }
    // Changing settings during a session cannot transfer ownership.
    DlssNr::NativeGuides::ConfigureInput(source == 2 ? 1 : 2, 0, true);
    DlssNr::NativeFg::Configure(!requested);
    if (DlssNr::NativeFg::Selected() != expected) return 1;
    std::printf("native_fg_selection: source=%d requested=%d selected=%d passed\n", source, requested, expected);
}
