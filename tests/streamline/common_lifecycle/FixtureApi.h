#pragma once
#include <windows.h>

// Authored public fixture only. This is not a private Streamline provider.
struct CommonFixtureLedger
{
    volatile LONG detached = 0;
    volatile LONG entered = 0;
    volatile LONG block = 0;
    HANDLE enteredEvent = nullptr;
    HANDLE releaseEvent = nullptr;
};
using FixtureConfigure = void (*)(CommonFixtureLedger*);
using FixtureGetter = void* (*)(const char*);
using FixtureCallback = unsigned (*)(unsigned);
using FixtureState = unsigned (*)();
using FixtureMutate = void (*)(unsigned);
using FixtureLoad = bool (*)(void*,const char*,const char**);
using FixtureStartup = bool (*)(const char*,void*);
using FixtureStartupHistorical = bool (*)(const char*,void*,void*);
using FixtureStartupParameters = void* (*)();
using FixtureShutdown = void (*)();
