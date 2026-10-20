#pragma once
#include "NativeIdentity.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

namespace DlssNr::PreFg::OutputIdentity
{
// A value token never owns a native resource, device, chain or foreign interface.
// Its destruction under the provider mutex performs local atomic/free work only.
struct Metadata
{
    const uint64_t serial;
    const uintptr_t canonical;
    std::atomic<bool> alive {true};
    uintptr_t cookie = 0; // identity only; never dereferenced
    Metadata(uint64_t value, uintptr_t identity) : serial(value), canonical(identity) {}
};
struct Binding
{
    std::shared_ptr<Metadata> token;
    bool HasToken() const noexcept { return bool(token); }
    bool operator==(const Binding&) const = default;
};

inline std::atomic<uint64_t> epoch {1};
inline std::atomic<unsigned> closing {0};
inline uint64_t Current() noexcept
{
    const auto value = epoch.load();
    return closing.load() ? 0 : value;
}
inline bool IsCurrent(uint64_t value) noexcept { return value && value == Current(); }
inline void Invalidate() noexcept { ++epoch; }
class ResizeScope
{
  public:
    ResizeScope() noexcept { ++closing; Invalidate(); }
    ~ResizeScope() { Invalidate(); --closing; }
    ResizeScope(const ResizeScope&) = delete;
    ResizeScope& operator=(const ResizeScope&) = delete;
};

// Stable COM/POD boundary: no C++ library/atomic layout crosses QueryInterface.
struct CookieSnapshot
{
    uint32_t version = 1, alive = 0;
    uint64_t serial = 0;
    uintptr_t canonical = 0;
};
struct __declspec(uuid("847A73E1-6D46-48C8-87C8-39F448825C01")) ICookie : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Snapshot(UINT bytes, CookieSnapshot* output) = 0;
};
// Normal DLL attach pins our code. A distinct pinned runtime instance uses its
// own key, rather than trusting another runtime's private C++ implementation.
inline unsigned char runtimeAnchor = 0;
inline GUID Key() noexcept
{
    const auto address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&runtimeAnchor));
    GUID key {0x643bd7d1, 0x4eef, 0x4ab3, {0xa9,0x1f,0x36,0x9a,0x12,0xd8,0x0e,0x51}};
    for (unsigned i = 0; i != 8; ++i) key.Data4[i] ^= static_cast<unsigned char>(address >> (i * 8));
    return key;
}
struct Registry
{
    std::recursive_mutex mutex;
    uint64_t next = 0;
    std::map<uint64_t, std::weak_ptr<Metadata>> tokens;
};
inline Registry& Instances() { static Registry value; return value; }
class Cookie final : public ICookie
{
    std::atomic<ULONG> refs {1};
    std::shared_ptr<Metadata> token;
  public:
    explicit Cookie(std::shared_ptr<Metadata> value) : token(std::move(value)) {}
    ~Cookie() { token->alive.store(false); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override
    {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(ICookie)) return E_NOINTERFACE;
        *output = static_cast<ICookie*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto left = --refs;
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Snapshot(UINT bytes, CookieSnapshot* output) override
    {
        if (!output) return E_POINTER;
        if (bytes != sizeof(CookieSnapshot)) return E_INVALIDARG;
        *output = {1, token->alive.load() ? 1u : 0u, token->serial, token->canonical};
        return S_OK;
    }
};
struct Observation
{
    Binding identity;
    uintptr_t canonical = 0;
    HRESULT result = E_POINTER;
    const char* refusal = "native-interface";
    bool optionalUnavailable = false;
};
inline HRESULT Canonical(ID3D12Resource* resource, uintptr_t& address)
{
    Microsoft::WRL::ComPtr<IUnknown> identity;
    const auto result = resource ? resource->QueryInterface(IID_PPV_ARGS(&identity)) : E_POINTER;
    address = SUCCEEDED(result) ? reinterpret_cast<uintptr_t>(identity.Get()) : 0;
    return SUCCEEDED(result) && !address ? E_UNEXPECTED : result;
}
inline Observation Observe(ID3D12Resource* resource, bool create)
{
    Observation observed;
    observed.result = Canonical(resource, observed.canonical);
    if (FAILED(observed.result)) return observed;
    // All QI/private-data/native references remain outside the provider mutex.
    Microsoft::WRL::ComPtr<IUnknown> cookie, cookieIdentity;
    Microsoft::WRL::ComPtr<ICookie> contract;
    try
    {
        auto& registry = Instances();
        std::lock_guard lock(registry.mutex);
        for (auto it = registry.tokens.begin(); it != registry.tokens.end();)
            if (it->second.expired()) it = registry.tokens.erase(it); else ++it;
        auto read = [&]() {
            UINT bytes = sizeof(IUnknown*);
            cookie.Reset();
            const auto result = resource->GetPrivateData(Key(), &bytes, cookie.GetAddressOf());
            if ((SUCCEEDED(result) && (bytes != sizeof(IUnknown*) || !cookie)) ||
                (FAILED(result) && cookie)) return E_UNEXPECTED;
            return result;
        };
        observed.result = read();
        if (observed.result == DXGI_ERROR_NOT_FOUND && create)
        {
            std::shared_ptr<Metadata> metadata;
            try { metadata = std::make_shared<Metadata>(++registry.next, observed.canonical); }
            catch (const std::bad_alloc&)
            { observed.result = E_OUTOFMEMORY; observed.refusal = "identity-allocation";
                observed.optionalUnavailable = true; return observed; }
            Microsoft::WRL::ComPtr<ICookie> fresh;
            fresh.Attach(new (std::nothrow) Cookie(metadata));
            if (!fresh) { observed.result = E_OUTOFMEMORY; observed.refusal = "identity-allocation";
                observed.optionalUnavailable = true; return observed; }
            Microsoft::WRL::ComPtr<IUnknown> freshIdentity;
            observed.result = fresh.As(&freshIdentity);
            if (FAILED(observed.result)) { observed.refusal = "cookie-interface"; return observed; }
            metadata->cookie = reinterpret_cast<uintptr_t>(freshIdentity.Get());
            try { registry.tokens[metadata->serial] = metadata; }
            catch (const std::bad_alloc&)
            { observed.result = E_OUTOFMEMORY; observed.refusal = "identity-allocation";
                observed.optionalUnavailable = true; return observed; }
            observed.result = resource->SetPrivateDataInterface(Key(), fresh.Get());
            if (FAILED(observed.result)) { observed.refusal = "private-data-set";
                observed.optionalUnavailable = true; return observed; }
            // Re-read what is actually installed; never assume our Set won.
            observed.result = read();
        }
        if (FAILED(observed.result))
        {
            observed.refusal = "private-data-get";
            observed.optionalUnavailable = observed.result != DXGI_ERROR_NOT_FOUND &&
                observed.result != DXGI_ERROR_MORE_DATA && observed.result != E_UNEXPECTED;
            return observed;
        }
        observed.refusal = "cookie-interface";
        observed.result = cookie.As(&contract);
        if (FAILED(observed.result) || !contract)
        { if (SUCCEEDED(observed.result)) observed.result = E_UNEXPECTED; return observed; }
        observed.result = cookie.As(&cookieIdentity);
        if (FAILED(observed.result) || !cookieIdentity)
        { if (SUCCEEDED(observed.result)) observed.result = E_UNEXPECTED; return observed; }
        CookieSnapshot snapshot;
        observed.refusal = "cookie-metadata";
        observed.result = contract->Snapshot(sizeof(snapshot), &snapshot);
        if (FAILED(observed.result)) return observed;
        const auto found = registry.tokens.find(snapshot.serial);
        auto metadata = found == registry.tokens.end() ? std::shared_ptr<Metadata>{} : found->second.lock();
        if (snapshot.version != 1 || !snapshot.alive || !metadata || !metadata->alive.load() ||
            snapshot.canonical != observed.canonical || metadata->canonical != observed.canonical ||
            metadata->cookie != reinterpret_cast<uintptr_t>(cookieIdentity.Get()))
        { observed.result = E_UNEXPECTED; return observed; }
        observed.identity.token = std::move(metadata);
        observed.result = S_OK; observed.refusal = nullptr;
        return observed;
    }
    catch (...)
    {
        observed.result = E_UNEXPECTED; observed.refusal = "cookie-metadata-exception";
        observed.optionalUnavailable = false; return observed;
    }
}
inline bool Matches(const Binding& binding, ID3D12Resource* resource)
{
    if (!binding.token || !binding.token->alive.load()) return false;
    const auto current = Observe(resource, false);
    return SUCCEEDED(current.result) && current.identity == binding;
}
}
