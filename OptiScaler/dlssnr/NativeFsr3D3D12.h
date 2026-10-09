#pragma once
#include "NativeFgHistory.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <ffx_framegeneration.h>
#include <memory>
#include <string>
namespace DlssNr::NativeFg {
struct FsrApi {PfnFfxCreateContext create=nullptr;PfnFfxDestroyContext destroy=nullptr;PfnFfxConfigure configure=nullptr;PfnFfxDispatch dispatch=nullptr;};
bool LoadFsrApi(FsrApi&,std::string&);
struct FsrFrame {
    std::uint64_t session=0,generation=0,frame=0;
    unsigned width=0,height=0;bool inverted=false,reset=true;
    double deltaMs=16.667;
    ID3D12Resource *color=nullptr,*depth=nullptr,*motion=nullptr;
    ID3D12Fence* producer=nullptr;std::uint64_t producerValue=0;
};
struct FsrOutput {
    Microsoft::WRL::ComPtr<ID3D12Resource> color;
    Microsoft::WRL::ComPtr<ID3D12Fence> completion;
    std::uint64_t value=0;bool generated=false;
};
// Serialized GPU owner. Begin/Poll never wait; Process preserves synchronous
// callers. Before Begin, callers prove external consumers of the last output
// completed. Uncertain work retains the provider/resources until process teardown.
class FsrSession {
    struct Impl;std::unique_ptr<Impl> impl_;
    FsrApi injectedApi_;
public:
    FsrSession();~FsrSession();FsrSession(const FsrSession&)=delete;
    bool Process(ID3D12Device*,const FsrFrame&,FsrOutput&,std::string&);
    bool Begin(ID3D12Device*,const FsrFrame&,std::string&);
    // False + Busy + empty reason means pending; otherwise false is terminal.
    bool Poll(FsrOutput&,std::string&);
    bool Busy()const;
    HANDLE CompletionWakeHandle()const;
    bool Close(std::string&);
    bool Unsafe()const;
    explicit FsrSession(FsrApi);
};
}
