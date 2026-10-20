#pragma once
// Byte-only root-layout observations follow the actual object lifetime.
#include <d3d12.h>
#include <dxgi.h>
#include <array>
#include <cstdint>
#include <optional>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Neurotic::D3D12
{
enum class RootKind { Table, Constants, CBV, SRV, UAV };
struct RootParameter
{
    RootKind kind;
    UINT constants = 0;
};

// Immutable shape bytes follow the actual root object's lifetime. No interface
// payload, object owner, recording epoch, or global pointer catalogue exists.
class NativeRootLayouts
{
    inline static constexpr GUID key_{0x6f0cdf6a, 0x56ad, 0x4baf,
        {0xa6, 0x31, 0x21, 0x06, 0x8e, 0xa4, 0x8c, 0x79}};
    struct Parameter { std::uint32_t kind = 0, constants = 0; };
    struct Blob
    {
        std::uint32_t magic = 0x4e52524c, version = 1, count = 0;
        std::array<Parameter, 64> parameters{};
    };
    static_assert(sizeof(Parameter) == 8 && sizeof(Blob) == 524);
    inline static std::mutex publicationMutex_;
    inline static std::unordered_set<ID3D12RootSignature*> publishing_;
    class Publication
    {
        ID3D12RootSignature* signature_;
        bool reserved_;
      public:
        explicit Publication(ID3D12RootSignature* signature) : signature_(signature)
        {
            std::lock_guard lock(publicationMutex_);
            reserved_ = publishing_.insert(signature).second;
        }
        ~Publication()
        {
            if (reserved_)
            {
                std::lock_guard lock(publicationMutex_);
                publishing_.erase(signature_);
            }
        }
        explicit operator bool() const { return reserved_; }
        Publication(const Publication&) = delete;
        Publication& operator=(const Publication&) = delete;
    };
    static bool Valid(RootParameter parameter)
    {
        if (static_cast<unsigned>(parameter.kind) > static_cast<unsigned>(RootKind::UAV)) return false;
        return parameter.kind == RootKind::Constants ?
            parameter.constants > 0 && parameter.constants <= 64 : parameter.constants == 0;
    }
  public:
    struct ReadResult
    {
        HRESULT result = E_FAIL;
        std::optional<std::vector<RootParameter>> layout;
    };
    // Foreign calls: caller must hold neither observer nor recording-state lock.
    static ReadResult Read(ID3D12RootSignature* signature)
    {
        if (!signature) return {E_INVALIDARG, {}};
        Blob blob{};
        UINT size = sizeof(blob);
        const auto result = signature->GetPrivateData(key_, &size, &blob);
        if (FAILED(result)) return {result, {}};
        if (size != sizeof(blob) || blob.magic != 0x4e52524c || blob.version != 1 || blob.count > 64)
            return {E_INVALIDARG, {}};
        std::vector<RootParameter> layout;
        layout.reserve(blob.count);
        for (UINT i = 0; i < blob.count; ++i)
        {
            const auto value = blob.parameters[i];
            if (value.kind > static_cast<unsigned>(RootKind::UAV)) return {E_INVALIDARG, {}};
            RootParameter parameter{static_cast<RootKind>(value.kind), value.constants};
            if (!Valid(parameter)) return {E_INVALIDARG, {}};
            layout.push_back(parameter);
        }
        for (UINT i = blob.count; i < 64; ++i)
            if (blob.parameters[i].kind || blob.parameters[i].constants) return {E_INVALIDARG, {}};
        return {S_OK, std::move(layout)};
    }
    static bool Store(ID3D12RootSignature* signature, const std::vector<RootParameter>& layout)
    {
        if (!signature || layout.size() > 64) return false;
        Blob blob{};
        blob.count = static_cast<UINT>(layout.size());
        for (UINT i = 0; i < blob.count; ++i)
        {
            if (!Valid(layout[i])) return false;
            blob.parameters[i] = {static_cast<std::uint32_t>(layout[i].kind), layout[i].constants};
        }
        // A reservation exists only during this call; it owns no COM object.
        // Overlapping/reentrant publication declines observation, never waits
        // on foreign code or claims an unfinished write succeeded. Completed
        // equal observations remain idempotent; conflicts cannot overwrite.
        Publication publication(signature);
        if (!publication) return false;
        const auto existing = Read(signature);
        if (existing.layout)
        {
            if (existing.layout->size() != layout.size()) return false;
            for (UINT i = 0; i < blob.count; ++i)
                if ((*existing.layout)[i].kind != layout[i].kind ||
                    (*existing.layout)[i].constants != layout[i].constants) return false;
            return true;
        }
        // Missing data can be created. Failed/malformed/conflicting existing
        // data cannot be replaced with an invented immutable observation.
        if (existing.result != DXGI_ERROR_NOT_FOUND) return false;
        return SUCCEEDED(signature->SetPrivateData(key_, sizeof(blob), &blob));
    }
};
}
