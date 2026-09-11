#pragma once

// The composition pass for Neural Rendering.
//
// Neural Rendering is two things, and only one of them is a shader. The model is an NGX feature --
// created and evaluated, not dispatched -- and that stays where it is. This is the other half: the
// pass that builds the tone-mapped proxy the model is shown, and then transfers the model's answer
// back onto the real frame.
//
// It is an ordinary compute shader with a constant struct, so it belongs here alongside RCAS and
// Output Scaling rather than owning a bespoke root signature and descriptor ring of its own.
//
// One shader, three modes, because all three read and write the same set of resources and differ
// only in what they compute:
//
//   Encode   the frame -> a tone-mapped proxy, plus an untouched copy to transfer against later
//   Down     the proxy -> a smaller proxy, when the model is asked to work below full resolution
//   Resolve  proxy + model answer + untouched copy -> the frame, edited

#include "DlssNr_Common.h"
#include "NrCompositionPool.h"
#include <dlssnr/NrGpuSafety.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12.h>
#include <shaders/Shader_Dx12Utils.h>

class Config;
template<class Source> struct NrConfigSnapshot;

class DlssNr_Dx12 : public Shader_Dx12, public DlssNr_Common
{
  private:
    DlssNr::CompositionPool _pool;
    unsigned long long _lastPoolReportMs = 0;
    unsigned long long _reportedGrowthEvents = 0;
    bool _lastPoolPreSr = false;
    DlssNr::CompositionPool::Admission _lastAdmission = DlssNr::CompositionPool::Admission::Accepted;
    void ReportPool(DlssNr::CompositionPool::Admission admission, bool preSr, bool force = false);

    // The shader reads five inputs and writes two, and not every mode uses all of them. Unused slots
    // still need a view bound -- an unbound descriptor is not an empty read, it is a read from
    // nothing -- so a stand-in is written into whichever are spare.
    static constexpr uint32_t kSrvCount = DlssNr::CompositionPool::SrvCount;
    static constexpr uint32_t kUavCount = DlssNr::CompositionPool::UavCount;

    uint32_t _numThreadsX = 8;
    uint32_t _numThreadsY = 8;

  public:
    // Caller holds the lifecycle lock. Reserves before Pre-SR copies or composition transitions.
    bool Prepare(ID3D12GraphicsCommandList* list, bool preSr, unsigned int passCount = 1);
    // Caller holds the same locks as shutdown; report even when drain later fails.
    void ReportFinalPool() { ReportPool(_lastAdmission, _lastPoolPreSr, true); }
    DlssNr_Dx12(std::string InName, ID3D12Device* InDevice);
    ~DlssNr_Dx12();

    // The pass. Resources in, and nothing read from anywhere the caller cannot see.
    //
    // This is the whole filter: it brings the model up if it is not already, builds the feature and
    // rebuilds it when the tuning or the resolution changes, evaluates it, and runs the compute passes
    // that show it the frame and bring its answer back. One call, like any other shader here.
    //
    // Sizes come from the resources. Everything the pass cannot work out for itself is in
    // DlssNrFrameInfo; everything the user chose stays in Config. colour and output may be the same
    // resource. timingQueue is the queue this list will be executed on, when the caller knows it.
    void Dispatch(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colour, ID3D12Resource* depth,
                  ID3D12Resource* motion, ID3D12Resource* output, const DlssNrFrameInfo& frame,
                  ID3D12CommandQueue* timingQueue, const NrConfigSnapshot<Config>& cfg,
                  bool privateCommandList = false, unsigned int exactWorkWidth = 0,
                  unsigned int exactWorkHeight = 0);

    // Records one pass. Resources that a given mode does not read may be null; a stand-in is bound in
    // their place so every descriptor in the table is valid.
    // One compute pass. The public entry below drives three of these plus the model.
    bool DispatchPass(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                  ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                  ID3D12Resource* InMotion,
                  // Vestigial. Fed to the slot the removed edit accumulator read its history from;
                  // nothing reads it now and every caller passes nullptr. Kept only so the binding
                  // table keeps its shape -- not evidence that temporal accumulation exists.
                  ID3D12Resource* InPrevEdit, ID3D12Resource* OutTarget,
                  ID3D12Resource* OutKeep);
};
