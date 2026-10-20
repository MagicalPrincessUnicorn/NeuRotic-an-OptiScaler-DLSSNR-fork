// Link into the real WARP flow test to observe its actual STL mutex imports.
// The installed runtime still executes every operation. This detects missing
// runtime initialization without installing an old CRT or fabricating GPU work.
#include <windows.h>
#include <cstdio>
#include <cstdlib>

namespace
{
SRWLOCK registryLock = SRWLOCK_INIT;
void* initialized[256]{};
unsigned initializedCount = 0, lockCount = 0, missingCount = 0;

template<class T> T Runtime(const char* name)
{
    const auto module = GetModuleHandleW(L"msvcp140.dll");
    const auto proc = module ? GetProcAddress(module, name) : nullptr;
    if (!proc) { std::fprintf(stderr, "Missing runtime export: %s\n", name); ExitProcess(2); }
    return reinterpret_cast<T>(proc);
}

void __cdecl Initialize(void* mutex, int flags)
{
    Runtime<void(__cdecl*)(void*, int)>("_Mtx_init_in_situ")(mutex, flags);
    AcquireSRWLockExclusive(&registryLock);
    bool found = false;
    for (unsigned i = 0; i < initializedCount; ++i) found |= initialized[i] == mutex;
    if (!found) {
        if (initializedCount == 256) ExitProcess(3);
        initialized[initializedCount++] = mutex;
    }
    ReleaseSRWLockExclusive(&registryLock);
}

int __cdecl Lock(void* mutex)
{
    AcquireSRWLockExclusive(&registryLock);
    ++lockCount;
    bool found = false;
    for (unsigned i = 0; i < initializedCount; ++i) found |= initialized[i] == mutex;
    if (!found) ++missingCount;
    ReleaseSRWLockExclusive(&registryLock);
    return Runtime<int(__cdecl*)(void*)>("_Mtx_lock")(mutex);
}

void Finish()
{
    if (!lockCount || !initializedCount || missingCount) {
        std::fprintf(stderr, "FAIL: backend runtime mutex initialization: initialized=%u locks=%u missing=%u\n",
                     initializedCount, lockCount, missingCount);
        ExitProcess(1);
    }
    std::printf("PASS: actual flow backend runtime mutex initialization: initialized=%u locks=%u missing=0\n",
                initializedCount, lockCount);
}
struct Register { Register() { std::atexit(Finish); } } registration;
}

// Local import pointers intercept only this test executable, forwarding to
// the real runtime exports. Production binaries never link this probe.
extern "C" {
void __cdecl _Mtx_init_in_situ(void* mutex, int flags) { Initialize(mutex, flags); }
int __cdecl _Mtx_lock(void* mutex) { return Lock(mutex); }
void (__cdecl* __imp__Mtx_init_in_situ)(void*, int) = Initialize;
int (__cdecl* __imp__Mtx_lock)(void*) = Lock;
}
