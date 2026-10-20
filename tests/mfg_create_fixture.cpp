#include <windows.h>
#include <atomic>
static std::atomic<unsigned> calls {0};
static std::atomic<bool> valid {false};
extern "C" unsigned CreateBody(void* command, unsigned, void* params, void* output)
{
    ++calls;
    valid = command == reinterpret_cast<void*>(0x12340000) &&
        params == reinterpret_cast<void*>(0x23450000) && output == reinterpret_cast<void*>(0x34560000);
    return 1; // NVSDK_NGX_Result_Success
}
extern "C" __declspec(dllexport) unsigned Calls() { return calls.load(); }
extern "C" __declspec(dllexport) bool ValidArguments() { return valid.load(); }
