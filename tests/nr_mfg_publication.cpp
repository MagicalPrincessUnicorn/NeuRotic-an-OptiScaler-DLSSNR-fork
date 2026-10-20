#include "../OptiScaler/mfg/MfgPublication.h"

#include <array>
#include <cassert>
#include <cstring>

using namespace Neurotic::Mfg;

struct FakeMemory : MfgMutationOps
{
    std::array<uint8_t, 8> bytes {0xB0, 0xB0, 0xD1, 0, 0, 0, 0, 0};
    int writes = 0;
    int failWriteAt = 0;
    int failRestoreAt = 0;
    int restores = 0;
    bool Read(uintptr_t address, uint8_t* out, size_t size) noexcept override
    {
        if (address + size > bytes.size()) return false;
        std::memcpy(out, bytes.data() + address, size);
        return true;
    }
    bool MakeWritable(uintptr_t, size_t, uintptr_t& prior) noexcept override
    { prior = 1; return true; }
    bool Write(uintptr_t address, const uint8_t* src, size_t size) noexcept override
    {
        ++writes;
        std::memcpy(bytes.data() + address, src, size);
        return writes != failWriteAt; // A failed call may already have changed memory.
    }
    bool Flush(uintptr_t, size_t) noexcept override { return true; }
    bool RestoreProtection(uintptr_t, size_t, uintptr_t) noexcept override
    { ++restores; return restores != failRestoreAt; }
};

static MfgPatchPlan GoodPlan()
{
    MfgPatchPlan plan;
    plan.generation = 12;
    plan.count = 3;
    plan.sites[0] = {0, 1, {0xB0}, {0x90}};
    plan.sites[1] = {1, 1, {0xB0}, {0x90}};
    plan.sites[2] = {2, 1, {0xD1}, {0xD2}};
    return plan;
}

static void TestPreflightAndSuccess()
{
    FakeMemory mem;
    auto plan = GoodPlan();
    MfgPublicationLatch latch;
    auto result = PublishMfgPatches(mem, plan, 12, latch);
    assert(result.status == MfgPublicationStatus::OwnedPublication);
    assert(mem.bytes[0] == 0x90 && mem.bytes[1] == 0x90 && mem.bytes[2] == 0xD2);
    assert(RestoreMfgPatches(mem, result.receipt, 12) == MfgPublicationStatus::RollbackVerified);
    assert(mem.bytes[0] == 0xB0 && mem.bytes[1] == 0xB0 && mem.bytes[2] == 0xD1);
    assert(PublishMfgPatches(mem, plan, 13, latch).status == MfgPublicationStatus::Stale);
    plan.sites[1].address = 0;
    assert(PublishMfgPatches(mem, plan, 12, latch).status == MfgPublicationStatus::NoMatch);
    assert(mem.writes == 6);
}

static void TestPrepatchedAndPartialWrite()
{
    FakeMemory mem;
    MfgPublicationLatch latch;
    mem.bytes[1] = 0x90;
    assert(PublishMfgPatches(mem, GoodPlan(), 12, latch).status == MfgPublicationStatus::ForeignPrepatched);
    assert(mem.writes == 0);
    mem.bytes[1] = 0xB0;
    mem.failWriteAt = 2;
    assert(PublishMfgPatches(mem, GoodPlan(), 12, latch).status == MfgPublicationStatus::RollbackVerified);
    assert(mem.bytes[0] == 0xB0 && mem.bytes[1] == 0xB0);
    mem.failWriteAt = mem.writes + 3;
    assert(PublishMfgPatches(mem, GoodPlan(), 12, latch).status == MfgPublicationStatus::RollbackVerified);
    assert(mem.bytes[2] == 0xD1);
}

static void TestIndeterminateStopsRetry()
{
    FakeMemory mem;
    MfgPublicationLatch latch;
    mem.failWriteAt = 2;
    mem.failRestoreAt = 2;
    assert(PublishMfgPatches(mem, GoodPlan(), 12, latch).status == MfgPublicationStatus::Indeterminate);
    assert(PublishMfgPatches(mem, GoodPlan(), 12, latch).status == MfgPublicationStatus::Indeterminate);
}

int main()
{
    TestPreflightAndSuccess();
    TestPrepatchedAndPartialWrite();
    TestIndeterminateStopsRetry();
}
