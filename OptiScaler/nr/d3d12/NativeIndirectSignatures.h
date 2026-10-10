#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <dlssnr/NativeIdentity.h>
#include <cstdint>
#include <mutex>
#include <memory>
#include <vector>

#define NR_NATIVE_INDIRECT_SIGNATURES 1

namespace Neurotic::D3D12
{
enum class NativeIndirectRootKind { Constants, CBV, SRV, UAV };
struct NativeIndirectRootReset
{
    NativeIndirectRootKind kind;
    UINT parameter = 0, offset = 0, count = 0;
};
struct NativeIndirectEffects
{
    bool compute = false;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    std::vector<NativeIndirectRootReset> resets;
};

// Bounded process-retained ownership prevents address reuse. No private-data
// bytes are read as COM pointers. Entries live until explicit hook retirement;
// this does not promise device-destruction notification or unlimited enrollment.
class NativeIndirectSignatures
{
    struct Entry
    {
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> signature;
        Microsoft::WRL::ComPtr<IUnknown> device;
        NativeIndirectEffects effects;
    };
    struct Registry
    {
        std::mutex mutex;
        bool active = true;
        std::uint64_t epoch = 1;
        std::vector<std::shared_ptr<const Entry>> entries;
    };
    static Registry& Store() { static auto* registry = new Registry; return *registry; }
    static std::uint64_t EnrollmentEpoch()
    {
        auto& registry = Store(); std::lock_guard lock(registry.mutex);
        return registry.active ? registry.epoch : 0;
    }
  public:
    static constexpr size_t Capacity = 1024;
    // Close admission before dropping owners. No automatic reopening: a late
    // creation callback cannot publish into a retired or later hook generation.
    static void Retire() noexcept
    {
        try
        {
            std::vector<std::shared_ptr<const Entry>> retired;
            auto& registry = Store();
            {
                std::lock_guard lock(registry.mutex);
                registry.active = false;
                ++registry.epoch;
                retired.swap(registry.entries);
            } // COM Release may reenter; never perform it under the registry lock.
        }
        catch (...) {}
    }

    template<class Original>
    static HRESULT CreateObserved(Original original, ID3D12Device* device,
        const D3D12_COMMAND_SIGNATURE_DESC* descriptor, ID3D12RootSignature* root,
        REFIID iid, void** output)
    {
        std::uint64_t epoch = 0;
        try { epoch = EnrollmentEpoch(); } catch (...) {}
        const auto result = original(device,descriptor,root,iid,output);
        if (epoch && SUCCEEDED(result) && output && *output)
        {
            try
            {
                Microsoft::WRL::ComPtr<ID3D12CommandSignature> signature;
                if (SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&signature))))
                    RegisterEpoch(device,signature.Get(),descriptor,root,epoch);
            }
            catch (...) {} // Observation preserves original HRESULT/output.
        }
        return result;
    }

    static bool Register(ID3D12Device* device, ID3D12CommandSignature* signature,
                         const D3D12_COMMAND_SIGNATURE_DESC* descriptor, ID3D12RootSignature* root) noexcept
    {
        try { return RegisterEpoch(device,signature,descriptor,root,EnrollmentEpoch()); }
        catch (...) { return false; }
    }
  private:
    // Call only for a successful original CreateCommandSignature with a real
    // returned object. Copy descriptors while valid, independently of NR enable.
    // Unsupported/missed creation cannot be reconstructed from the signature.
    static bool RegisterEpoch(ID3D12Device* device, ID3D12CommandSignature* signature,
                         const D3D12_COMMAND_SIGNATURE_DESC* descriptor, ID3D12RootSignature* root,
                         std::uint64_t epoch) noexcept
    {
        try
        {
            if (!epoch) return false;
            if (!device || !signature || !descriptor || !descriptor->pArgumentDescs ||
                !descriptor->NumArgumentDescs || descriptor->NumArgumentDescs > 128 ||
                !descriptor->ByteStride || descriptor->ByteStride % 4) return false;
            const auto native = DlssNr::NativeIdentity::Resolve<ID3D12CommandSignature>(signature);
            if (!native.object) return false;
            Microsoft::WRL::ComPtr<ID3D12Device> signatureDevice;
            if (FAILED(native.object->GetDevice(IID_PPV_ARGS(&signatureDevice)))) return false;
            const auto identity = DlssNr::NativeIdentity::ResolveDeviceIdentity(device);
            const auto actual = DlssNr::NativeIdentity::ResolveDeviceIdentity(signatureDevice.Get());
            if (!identity.object || !actual.object || identity.object.Get() != actual.object.Get()) return false;
            if (root)
            {
                Microsoft::WRL::ComPtr<ID3D12Device> rootDevice;
                if (FAILED(root->GetDevice(IID_PPV_ARGS(&rootDevice))) ||
                    !DlssNr::NativeIdentity::CompareDevices(device,rootDevice.Get()).equal) return false;
            }
            auto entry = std::make_shared<Entry>();
            entry->signature = native.object;
            entry->device = identity.object;
            auto* effects = &entry->effects;
            effects->root = root;
            UINT bytes = 0; bool terminal = false, graphicsBuffers = false;
            for (UINT i = 0; i < descriptor->NumArgumentDescs; ++i)
            {
                const auto& argument = descriptor->pArgumentDescs[i];
                UINT size = 0;
                switch (argument.Type)
                {
                case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
                case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
                case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
                    if (terminal || i + 1 != descriptor->NumArgumentDescs) return false;
                    terminal = true; effects->compute = argument.Type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
                    size = effects->compute ? 12 : argument.Type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW ? 16 : 20;
                    break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
                    if (argument.VertexBuffer.Slot >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) return false;
                    graphicsBuffers = true; size = sizeof(D3D12_VERTEX_BUFFER_VIEW); break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
                    graphicsBuffers = true; size = sizeof(D3D12_INDEX_BUFFER_VIEW); break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
                    if (!argument.Constant.Num32BitValuesToSet || argument.Constant.RootParameterIndex >= 64 ||
                        argument.Constant.DestOffsetIn32BitValues >= 64 ||
                        argument.Constant.Num32BitValuesToSet > 64 - argument.Constant.DestOffsetIn32BitValues) return false;
                    effects->resets.push_back({NativeIndirectRootKind::Constants, argument.Constant.RootParameterIndex,
                        argument.Constant.DestOffsetIn32BitValues, argument.Constant.Num32BitValuesToSet});
                    size = 4 * argument.Constant.Num32BitValuesToSet; break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
                    effects->resets.push_back({NativeIndirectRootKind::CBV,argument.ConstantBufferView.RootParameterIndex}); size=8; break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
                    effects->resets.push_back({NativeIndirectRootKind::SRV,argument.ShaderResourceView.RootParameterIndex}); size=8; break;
                case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
                    effects->resets.push_back({NativeIndirectRootKind::UAV,argument.UnorderedAccessView.RootParameterIndex}); size=8; break;
                default: return false; // Rays, mesh, incrementing constants and future effects stay conservative.
                }
                if (size > descriptor->ByteStride - bytes) return false;
                bytes += size;
            }
            if (!terminal || (effects->compute && graphicsBuffers) ||
                (effects->resets.empty() != (root == nullptr))) return false;
            for (const auto& reset : effects->resets) if (reset.parameter >= 64) return false;
            std::shared_ptr<const Entry> immutable = std::move(entry);
            auto& registry = Store();
            // immutable owns all COM references before the lock. It is declared
            // before this guard so every reject/exception releases outside it.
            std::lock_guard lock(registry.mutex);
            if (!registry.active || registry.epoch != epoch || registry.entries.size() >= Capacity) return false;
            for (const auto& existing : registry.entries)
                if (existing->signature.Get() == native.object.Get()) return false;
            registry.entries.push_back(immutable);
            return true;
        }
        catch (...) { return false; } // Observation cannot change the game's creation result.
    }

  public:
    static std::shared_ptr<const NativeIndirectEffects> Read(
        ID3D12CommandSignature* signature, ID3D12GraphicsCommandList* list) noexcept
    {
        try
        {
            if (!signature || !list) return {};
            const auto native = DlssNr::NativeIdentity::Resolve<ID3D12CommandSignature>(signature);
            if (!native.object) return {};
            Microsoft::WRL::ComPtr<ID3D12Device> device;
            if (FAILED(list->GetDevice(IID_PPV_ARGS(&device)))) return {};
            const auto identity = DlssNr::NativeIdentity::ResolveDeviceIdentity(device.Get());
            if (!identity.object) return {};
            auto& registry = Store(); std::lock_guard lock(registry.mutex);
            if (!registry.active) return {};
            for (const auto& entry : registry.entries)
                if (entry->signature.Get() == native.object.Get() && entry->device.Get() == identity.object.Get())
                    return {entry,&entry->effects}; // Alias owns signature, root AND device provenance.
        }
        catch (...) {}
        return {};
    }
};
}
