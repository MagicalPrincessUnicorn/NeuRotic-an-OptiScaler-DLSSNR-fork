#include "../OptiScaler/mfg/MfgOptionsSnapshot.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

static unsigned failures;
static void Check(bool ok, const char* why)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; }
}

int main()
{
    // Use the actual pinned SDK layout on MSVC x64. Poison *all* bytes after
    // the last declared member, including old tail padding reused by new fields.
    for (size_t version = 1; version <= 5; ++version)
    {
        sl::DLSSGOptions input;
        input.structVersion = version;
        input.mode = sl::DLSSGMode::eAuto;
        input.numFramesToGenerate = 2;
        input.colorWidth = 2560;
        const unsigned char* last[] = {
            reinterpret_cast<unsigned char*>(&input.onErrorCallback) + sizeof(input.onErrorCallback),
            reinterpret_cast<unsigned char*>(&input.bReserved15) + sizeof(input.bReserved15),
            reinterpret_cast<unsigned char*>(&input.queueParallelismMode) + sizeof(input.queueParallelismMode),
            reinterpret_cast<unsigned char*>(&input.enableUserInterfaceRecomposition) + sizeof(input.enableUserInterfaceRecomposition),
            reinterpret_cast<unsigned char*>(&input.dynamicTargetFrameRate) + sizeof(input.dynamicTargetFrameRate)};
        const auto end = static_cast<size_t>(last[version - 1] - reinterpret_cast<unsigned char*>(&input));
        std::memset(reinterpret_cast<unsigned char*>(&input) + end, 0x7f, sizeof(input) - end);
        const auto captured = Neurotic::Mfg::CaptureOptions(input);
        Check(captured.supported && captured.replayable, "known pointer-free ABI is replayable");
        Check(captured.value.mode == sl::DLSSGMode::eAuto && captured.value.colorWidth == 2560,
            "known scalar values including Auto survive capture");
        if (version < 3) Check(captured.value.queueParallelismMode == sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue,
            "absent queue parallelism defaults instead of copying tail padding");
        if (version < 4) Check(captured.value.enableUserInterfaceRecomposition == sl::eFalse,
            "absent recomposition defaults instead of copying tail padding");
        if (version < 5) Check(captured.value.dynamicTargetFrameRate == 0.0f,
            "absent dynamic target defaults instead of copying tail padding");

        input.onErrorCallback = [](const sl::APIError&) {};
        sl::DLSSGOptions extension;
        input.next = &extension;
        const auto borrowed = Neurotic::Mfg::CaptureOptions(input);
        Check(borrowed.supported && !borrowed.replayable && borrowed.value.mode == sl::DLSSGMode::eAuto,
            "borrowed callbacks and chains remain scalar-observable but never replayable");
        Check(!borrowed.value.next && !borrowed.value.onErrorCallback,
            "owned observation retains no caller callback or extension pointer");

        // Put each real historical object extent against an inaccessible page.
        const size_t extents[] = {104, 112, 112, 120, 120};
        auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!pages) return 2;
        DWORD previous;
        if (!VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &previous)) return 2;
        auto* historical = pages + 4096 - extents[version - 1];
        std::memcpy(historical, &input, extents[version - 1]);
        const auto guarded = Neurotic::Mfg::CaptureOptions(*reinterpret_cast<sl::DLSSGOptions*>(historical));
        Check(guarded.supported && guarded.value.numFramesToGenerate == 2,
            "historical guarded object reads only its declared extent");
        VirtualFree(pages, 0, MEM_RELEASE);
    }
    sl::DLSSGOptions unknown;
    unknown.structVersion = 99;
    Check(!Neurotic::Mfg::CaptureOptions(unknown).supported, "unknown ABI remains opaque");
    std::printf("Pinned MSVC options ABI: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
