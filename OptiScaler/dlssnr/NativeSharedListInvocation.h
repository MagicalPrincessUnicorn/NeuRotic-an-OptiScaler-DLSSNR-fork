#pragma once
#include <nr/protocol/NativeCommandStateScope.h>

namespace DlssNr
{
enum class NativeSharedListResult { Skipped, Restored, Failed };

// The actual command-state owner supplies an authenticated snapshot and replay.
// Capture refusal performs no custom work. Once work starts, any exception or
// replay failure forbids continuation against uncertain caller bindings.
template<class Capture, class Invoke, class Restore>
NativeSharedListResult InvokeNativeSharedList(Capture capture, Invoke invoke, Restore restore)
{
    decltype(capture()) saved;
    try { saved = capture(); }
    catch (...) { return NativeSharedListResult::Skipped; }
    if (!saved) return NativeSharedListResult::Skipped;

    Neurotic::Protocol::NativeCommandStateScope scope(
        [&] { return restore(*saved); }, []() noexcept {});
    bool completed = false;
    try { invoke(); completed = true; }
    catch (...) {}
    return scope.Restore() && completed
        ? NativeSharedListResult::Restored : NativeSharedListResult::Failed;
}
}
