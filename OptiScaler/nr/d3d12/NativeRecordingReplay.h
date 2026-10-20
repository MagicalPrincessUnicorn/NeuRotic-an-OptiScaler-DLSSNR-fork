#pragma once
#include "NativeRecordingHooks.h"
#include "NativeRecordingInterfaces.h"
#include "NativeReplayEffects.h"
#include "NativeWrapperHeapBindings.h"

namespace Neurotic::D3D12
{
// Prepare every foreign ownership/interface query before selecting the journal
// epoch. These references are held by a restore point, not by a global COM map.
struct NativeRecordingReplayContext
{
    NativeRecordingInterfaces interfaces;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> version4;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList9> version9;
    struct Route
    {
        void* object;
        void** table;
        unsigned slot;
        void* target;
        std::array<unsigned char,16> bytes;
    };
    std::vector<Route> routes;
    bool prepared = false;
    static bool CopyBytes(void* destination, const void* source, size_t bytes) noexcept
    {
        __try { std::memcpy(destination, source, bytes); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Valid() const noexcept
    {
        if (!prepared) return false;
        for (const auto& route : routes)
        {
            const auto table = *reinterpret_cast<void***>(route.object);
            std::array<unsigned char,16> bytes{};
            if (table != route.table || table[route.slot] != route.target ||
                !CopyBytes(bytes.data(), route.target, bytes.size()) || bytes != route.bytes) return false;
        }
        return true;
    }
    static std::shared_ptr<const NativeRecordingReplayContext> Prepare(
        NativeRecordingHooks& hooks, ID3D12GraphicsCommandList* incoming)
    {
        auto context = std::make_shared<NativeRecordingReplayContext>();
        context->interfaces = NativeRecordingInterfaces::Resolve(incoming);
        if (FAILED(context->interfaces.result) || !context->interfaces.terminal) return context;
        if (context->interfaces.caller.Get() == context->interfaces.terminal.Get())
        { context->prepared = true; return context; }
        if (!hooks.WrapperHeapRouteValid(context->interfaces.caller.Get())) return context;
        const bool stateObjectSupported = bool(hooks.Originals().stateObject);
        if (stateObjectSupported && SUCCEEDED(context->interfaces.caller.As(&context->version4)))
        {
            Microsoft::WRL::ComPtr<IUnknown> callerIdentity, versionIdentity;
            if (FAILED(context->interfaces.caller.As(&callerIdentity)) || FAILED(context->version4.As(&versionIdentity)) ||
                callerIdentity.Get() != versionIdentity.Get()) context->version4.Reset();
        }
        if(hooks.Originals().depthBias&&SUCCEEDED(context->interfaces.caller.As(&context->version9))){
            Microsoft::WRL::ComPtr<IUnknown> callerIdentity,versionIdentity;
            if(FAILED(context->interfaces.caller.As(&callerIdentity))||FAILED(context->version9.As(&versionIdentity))||
                callerIdentity.Get()!=versionIdentity.Get())context->version9.Reset();
        }
        const auto pin = [&](void* object, unsigned slot) {
            auto** table = *reinterpret_cast<void***>(object);
            Route route{object, table, slot, table[slot], {}};
            if (!CopyBytes(route.bytes.data(), route.target, route.bytes.size())) return false;
            context->routes.push_back(route);
            return true;
        };
        // Live route continuity, not a developer DLL/build identity allowlist.
        for (unsigned slot : {23u,25u,26u,28u,29u,30u,31u,32u,35u,36u,37u,38u,39u,40u,41u,42u})
            if (!pin(context->interfaces.caller.Get(), slot)) return context;
        if (context->version4 && !pin(context->version4.Get(), 75)) return context;
        if (context->version9 && !pin(context->version9.Get(), 82)) return context;
        context->prepared = true;
        return context;
    }
};

struct NativeRecordingReplayPlan
{
    NativeRecordingState::OriginalSetters setters;
    const char* refusal = nullptr;
    static NativeRecordingReplayPlan Make(NativeRecordingHooks& hooks,
        std::shared_ptr<const NativeRecordingReplayContext> context, std::uint64_t incarnation,
        RestoreMask mask, UINT heapCount, ID3D12DescriptorHeap* const* terminalHeaps)
    {
        NativeRecordingReplayPlan plan;
        if (!context || !context->Valid())
        { plan.refusal = "Native.State.OuterRoutesUnavailable"; return plan; }
        auto* caller = context->interfaces.caller.Get();
        auto* terminal = context->interfaces.terminal.Get();
        if (caller == terminal) { plan.setters = hooks.Originals(); return plan; }
        NativeWrapperHeapBindings::Owner binding;
        if (Has(mask, RestoreMask::Heaps) && heapCount)
        {
            binding = NativeWrapperHeapBindings::Find(caller, terminal, incarnation, heapCount, terminalHeaps);
            if (!binding) { plan.refusal = "Native.State.OuterHeapArgumentsUnavailable"; return plan; }
        }
        const auto requireList = [context](ID3D12GraphicsCommandList* list) {
            if (list != context->interfaces.terminal.Get() || !context->Valid())
                throw std::runtime_error("native outer replay route changed");
        };
        plan.setters.validate = [context] { return context->Valid(); };
        plan.setters.heaps = [context, binding, requireList](ID3D12GraphicsCommandList* list, UINT count, ID3D12DescriptorHeap* const* heaps) {
            requireList(list);
            std::array<ID3D12DescriptorHeap*,2> outer{};
            if (count)
            {
                if (!binding || count != binding->count || !heaps) throw std::runtime_error("native outer heap mapping absent");
                for (UINT index = 0; index < count; ++index)
                    if (heaps[index] != binding->terminalHeaps[index].Get()) throw std::runtime_error("native outer heap mapping changed");
                outer = binding->CallerArguments();
            }
            // An authentic known empty bind needs no heap-object translation.
            NativeReplayEffects::Invoke<28>(list, [&] { context->interfaces.caller->SetDescriptorHeaps(count, outer.data()); }, count, heaps);
        };
        plan.setters.pipeline = [context, requireList](ID3D12GraphicsCommandList* list, ID3D12PipelineState* value) {
            requireList(list);NativeReplayEffects::Invoke<25>(list,[&]{context->interfaces.caller->SetPipelineState(value);},value);
        };
        if (context->version4 && hooks.Originals().stateObject)
            plan.setters.stateObject = [context, requireList](ID3D12GraphicsCommandList* list, ID3D12StateObject* value) {
                requireList(list);NativeReplayEffects::Invoke<75>(list,[&]{context->version4->SetPipelineState1(value);},value);
            };
        if(context->version9&&hooks.Originals().depthBias)
            plan.setters.depthBias=[context,requireList](ID3D12GraphicsCommandList* list,float bias,float clamp,float slope){
                requireList(list);NativeReplayEffects::Invoke<82>(list,[&]{context->version9->RSSetDepthBias(bias,clamp,slope);},bias,clamp,slope);
            };
        plan.setters.computeSignature = [context, requireList](ID3D12GraphicsCommandList* list, ID3D12RootSignature* value) {
            requireList(list);NativeReplayEffects::Invoke<29>(list,[&]{context->interfaces.caller->SetComputeRootSignature(value);},value);
        };
        plan.setters.graphicsSignature = [context, requireList](ID3D12GraphicsCommandList* list, ID3D12RootSignature* value) {
            requireList(list);NativeReplayEffects::Invoke<30>(list,[&]{context->interfaces.caller->SetGraphicsRootSignature(value);},value);
        };
        plan.setters.computeTable = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE value) {
            requireList(list);NativeReplayEffects::Invoke<31>(list,[&]{context->interfaces.caller->SetComputeRootDescriptorTable(index,value);},index,value);
        };
        plan.setters.graphicsTable = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE value) {
            requireList(list);NativeReplayEffects::Invoke<32>(list,[&]{context->interfaces.caller->SetGraphicsRootDescriptorTable(index,value);},index,value);
        };
        plan.setters.computeConstants = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, UINT count, const void* values, UINT offset) {
            requireList(list);NativeReplayEffects::Invoke<35>(list,[&]{context->interfaces.caller->SetComputeRoot32BitConstants(index,count,values,offset);},index,count,values,offset);
        };
        plan.setters.graphicsConstants = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, UINT count, const void* values, UINT offset) {
            requireList(list);NativeReplayEffects::Invoke<36>(list,[&]{context->interfaces.caller->SetGraphicsRoot32BitConstants(index,count,values,offset);},index,count,values,offset);
        };
        plan.setters.computeCbv = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value) {
            requireList(list);NativeReplayEffects::Invoke<37>(list,[&]{context->interfaces.caller->SetComputeRootConstantBufferView(index,value);},index,value);
        };
        plan.setters.graphicsCbv = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value) {
            requireList(list);NativeReplayEffects::Invoke<38>(list,[&]{context->interfaces.caller->SetGraphicsRootConstantBufferView(index,value);},index,value);
        };
        plan.setters.computeSrv = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value) {
            requireList(list);NativeReplayEffects::Invoke<39>(list,[&]{context->interfaces.caller->SetComputeRootShaderResourceView(index,value);},index,value);
        };
        plan.setters.graphicsSrv = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value) {
            requireList(list);NativeReplayEffects::Invoke<40>(list,[&]{context->interfaces.caller->SetGraphicsRootShaderResourceView(index,value);},index,value);
        };
        plan.setters.computeUav = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value) {
            requireList(list);NativeReplayEffects::Invoke<41>(list,[&]{context->interfaces.caller->SetComputeRootUnorderedAccessView(index,value);},index,value);
        };
        plan.setters.graphicsUav = [context, requireList](ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value) {
            requireList(list);NativeReplayEffects::Invoke<42>(list,[&]{context->interfaces.caller->SetGraphicsRootUnorderedAccessView(index,value);},index,value);
        };
        plan.setters.blendFactor = [context, requireList](ID3D12GraphicsCommandList* list, const float* values) {
            requireList(list);NativeReplayEffects::Invoke<23>(list,[&]{context->interfaces.caller->OMSetBlendFactor(values);},values);
        };
        plan.setters.barriers = [context, requireList](ID3D12GraphicsCommandList* list, UINT count, const D3D12_RESOURCE_BARRIER* values) {
            requireList(list);NativeReplayEffects::Invoke<26>(list,[&]{context->interfaces.caller->ResourceBarrier(count,values);},count,values);
        };
        return plan;
    }
};
}
