#pragma once
#include <unknwn.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace DlssNr::PresentGuideAllocation
{
struct Ledger
{
    std::atomic<uint64_t> committed {0}, reserved {0};
};
// The resource owns this charge through public private-data COM ownership.
// No charge owns a resource; delayed external COM release cannot create a cycle
// or make clearing Bridge slot metadata undercount a still-live allocation.
class Charge final : public IUnknown
{
    std::atomic<ULONG> references {1};
    std::shared_ptr<Ledger> ledger;
    uint64_t bytes;
    ~Charge() { ledger->committed.fetch_sub(bytes, std::memory_order_relaxed); }
  public:
    Charge(std::shared_ptr<Ledger> value, uint64_t cost) : ledger(std::move(value)), bytes(cost)
    { ledger->committed.fetch_add(bytes, std::memory_order_relaxed); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override
    {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (id != __uuidof(IUnknown)) return E_NOINTERFACE;
        *output = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override
    { const ULONG left = --references; if (!left) delete this; return left; }
};
class Reservation
{
    std::shared_ptr<Ledger> ledger;
    uint64_t remaining;
  public:
    Reservation(std::shared_ptr<Ledger> value, uint64_t bytes) : ledger(std::move(value)), remaining(bytes)
    { if (ledger) ledger->reserved.fetch_add(remaining, std::memory_order_relaxed); }
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    ~Reservation()
    { if (ledger) ledger->reserved.fetch_sub(remaining, std::memory_order_relaxed); }
    template<class Resource> bool Attach(Resource* resource, uint64_t bytes) noexcept
    {
        if (!resource || !ledger || !bytes || bytes > remaining) return false;
        auto* charge = new(std::nothrow) Charge(ledger, bytes);
        if (!charge) return false;
        constexpr GUID key = {0x45b3f8a1,0x412b,0x4f36,{0xb1,0x86,0x82,0x90,0x96,0xc3,0xe0,0x19}};
        const auto result = resource->SetPrivateDataInterface(key, charge);
        charge->Release();
        if (FAILED(result)) return false;
        remaining -= bytes;
        ledger->reserved.fetch_sub(bytes, std::memory_order_relaxed);
        return true;
    }
};
}
