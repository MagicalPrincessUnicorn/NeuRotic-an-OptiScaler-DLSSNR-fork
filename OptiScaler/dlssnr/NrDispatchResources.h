#pragma once

#include <array>
#include <cassert>
#include <memory>
#include <d3d12.h>
#include <wrl/client.h>

namespace DlssNr::Detail
{
// Only unpublished resources are released here. Published resources must be retired by the caller.
class ScratchTransaction
{
  public:
    struct Request
    {
        ID3D12Resource** destination;
        DXGI_FORMAT format;
        UINT width;
        UINT height;
        bool needed;
    };
    static constexpr size_t Count = 5;

  private:
    std::array<Request, Count> _requests;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Count> _replacement;
    std::array<bool, Count> _replace {};
    bool _ready = false;

  public:
    explicit ScratchTransaction(const std::array<Request, Count>& requests) : _requests(requests) {}
    ScratchTransaction(const ScratchTransaction&) = delete;
    ScratchTransaction& operator=(const ScratchTransaction&) = delete;

    template <typename Allocate> bool Prepare(Allocate allocate)
    {
        for (size_t i = 0; i < Count; ++i)
        {
            const auto& want = _requests[i];
            const auto* have = *want.destination;
            bool matches = have == nullptr && !want.needed;
            if (have != nullptr && want.needed)
            {
                const auto desc = (*want.destination)->GetDesc();
                matches = desc.Format == want.format && desc.Width == want.width && desc.Height == want.height;
            }
            _replace[i] = !matches;
            if (_replace[i] && want.needed)
            {
                _replacement[i].Attach(allocate(want.format, want.width, want.height));
                if (!_replacement[i]) return false;
            }
        }
        _ready = true;
        return true;
    }

    template <typename Retire> void Commit(Retire retire)
    {
        assert(_ready);
        for (size_t i = 0; i < Count; ++i)
        {
            if (!_replace[i]) continue;
            retire(*_requests[i].destination);
            *_requests[i].destination = _replacement[i].Detach();
        }
        _ready = false;
    }
};

enum class LayerScratchResult { Ready, FirstUnavailable, SecondUnavailable };

// Optional allocation cannot cancel a prepared first layer. Discard unpublished replacements only.
template <typename Allocate>
LayerScratchResult PrepareLayerScratch(ScratchTransaction& first,
                                      std::unique_ptr<ScratchTransaction>& second, Allocate allocate)
{
    if (!first.Prepare(allocate)) return LayerScratchResult::FirstUnavailable;
    if (second && !second->Prepare(allocate))
    {
        second.reset();
        return LayerScratchResult::SecondUnavailable;
    }
    return LayerScratchResult::Ready;
}

// Track only transitions actually recorded by this invocation. A retained guide clone may not
// have been used this frame, so its mere presence must never cause a restoration barrier.
class DispatchResourceStates
{
    using Emit = void (*)(ID3D12GraphicsCommandList*, ID3D12Resource*, D3D12_RESOURCE_STATES,
                         D3D12_RESOURCE_STATES);
    using EmitWithContext = void (*)(void*,ID3D12GraphicsCommandList*,ID3D12Resource*,
        D3D12_RESOURCE_STATES,D3D12_RESOURCE_STATES,bool);
    struct Entry
    {
        ID3D12Resource* resource;
        D3D12_RESOURCE_STATES arrival;
        D3D12_RESOURCE_STATES current;
    };
    ID3D12GraphicsCommandList* _cmd;
    Emit _emit;
    void* _context=nullptr;
    EmitWithContext _contextEmit=nullptr;
    // Ten composed passes can transition ten independent five-surface scratch sets in one
    // invocation. Sixty-four retains headroom for the target and readable guide clones.
    std::array<Entry, 64> _entries {};
    size_t _count = 0;

  public:
    DispatchResourceStates(ID3D12GraphicsCommandList* cmd, Emit emit,
        void* context=nullptr,EmitWithContext contextEmit=nullptr)
        :_cmd(cmd),_emit(emit),_context(context),_contextEmit(contextEmit){}
    DispatchResourceStates(const DispatchResourceStates&) = delete;
    DispatchResourceStates& operator=(const DispatchResourceStates&) = delete;
    ~DispatchResourceStates() { Restore(); }

    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
    {
        if (from == to) return;
        size_t i = 0;
        while (i < _count && _entries[i].resource != resource) ++i;
        if (i == _count)
        {
            assert(_count < _entries.size());
            _entries[_count++] = { resource, from, from };
        }
        assert(_entries[i].current == from);
        if(_context&&_contextEmit)_contextEmit(_context,_cmd,resource,from,to,false);
        else _emit(_cmd,resource,from,to);
        _entries[i].current = to;
    }

    bool CommitCurrent(ID3D12Resource* resource,D3D12_RESOURCE_STATES expected)noexcept
    {
        for(size_t i=0;i<_count;++i)if(_entries[i].resource==resource){
            if(_entries[i].current!=expected)return false;
            _entries[i].arrival=_entries[i].current;return true;
        }
        return false;
    }
    void Restore()
    {
        while (_count != 0)
        {
            const auto& entry = _entries[--_count];
            if (entry.current != entry.arrival)
            {
                if(_context&&_contextEmit)_contextEmit(_context,_cmd,entry.resource,entry.current,entry.arrival,true);
                else _emit(_cmd,entry.resource,entry.current,entry.arrival);
            }
        }
    }
};
}
