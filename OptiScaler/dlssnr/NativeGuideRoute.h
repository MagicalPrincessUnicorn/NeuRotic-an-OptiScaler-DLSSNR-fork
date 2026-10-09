#pragma once
#include <atomic>
#include <Windows.h>
namespace DlssNr::NativeGuides {
inline std::atomic<bool> configured{false};
inline std::atomic<bool> configuredVulkanRenderer{false};
inline void ConfigureVulkanRenderer(bool value) noexcept {configuredVulkanRenderer.store(value);}
inline bool VulkanRendererSelected() noexcept {static const bool value=configuredVulkanRenderer.load();return value;}
inline std::atomic<int> requestedSource{-1},requestedTransport{0};
inline std::atomic<bool> allowCpuFallback{true};
inline void ConfigureInput(int source,int transport,bool fallback) noexcept {
 requestedSource.store(source>=0&&source<=4?source:-1,std::memory_order_relaxed);
 requestedTransport.store(transport>=0&&transport<=2?transport:0,std::memory_order_relaxed);
 allowCpuFallback.store(fallback,std::memory_order_relaxed);
}
// Shared creation policy; edits cannot transfer an active owner's resources.
inline int StartupSource() noexcept {
 static const int source=[] {
  const int explicitSource=requestedSource.load(std::memory_order_relaxed);
  if(explicitSource>=0)return explicitSource;
  wchar_t native[2]{},prepared[2]{};
  if(configured.load(std::memory_order_relaxed)||(GetEnvironmentVariableW(L"NEUROTIC_NATIVE_GUIDES",native,2)==1&&native[0]==L'1'))return 2;
  if(GetEnvironmentVariableW(L"NEUROTIC_PREPARED_GUIDES",prepared,2)==1&&prepared[0]==L'1')return 3;
  return 0;
 }();return source;
}
inline int StartupTransport() noexcept {static const int value=requestedTransport.load();return value;}
inline std::atomic<unsigned> appliedInput{UINT_MAX};
inline std::atomic<uint64_t> inputEpoch{0};
inline unsigned AppliedInput() noexcept {
 unsigned value=appliedInput.load(std::memory_order_acquire);
 if(value==UINT_MAX){const unsigned initial=unsigned(StartupSource())|(unsigned(StartupTransport())<<8);
  appliedInput.compare_exchange_strong(value,initial,std::memory_order_acq_rel);value=appliedInput.load(std::memory_order_acquire);}
 return value;
}
inline int SelectedSource() noexcept {return int(AppliedInput()&255u);}
inline int SelectedTransport() noexcept {return int((AppliedInput()>>8)&255u);}
// Retained shared storage is not a commitment for future capture buffers.
inline bool CaptureDepthGpuOnly(bool sharedReady) noexcept {return sharedReady&&SelectedTransport()!=2;}
// Connection-policy commit only, after admission and GPU completion proof.
inline void ApplyInputSelection(int source,int transport,uint64_t epoch) noexcept {
 inputEpoch.store(epoch,std::memory_order_release);
 appliedInput.store(unsigned(source)|(unsigned(transport)<<8),std::memory_order_release);
}
// A permission for future frames; a current claim keeps its completion duties.
inline void ConfigureCpuFallback(bool value) noexcept {allowCpuFallback.store(value,std::memory_order_relaxed);}
inline bool SelectedCpuFallback() noexcept {return allowCpuFallback.load(std::memory_order_relaxed);}
// -1 automatic, 0 forward (far=1), 1 reversed (far=0).
inline std::atomic<int> depthDirection{-1};
inline void ConfigureDepthDirection(int value) noexcept {depthDirection.store(value==0||value==1?value:-1,std::memory_order_relaxed);}
inline void Configure(bool value) noexcept {configured.store(value,std::memory_order_relaxed);}
// Effective selection changes only after the connection owner commits it.
inline bool Selected() noexcept {
#ifndef _WIN64
    return false;
#else
    return SelectedSource()==2;
#endif
}
// Observation is available in Automatic without reserving rendering rights.
// A qualified frame must still claim the connection policy before processing.
inline bool ObserveBuiltIn() noexcept {
#ifndef _WIN64
 return false;
#else
 const int source=StartupSource();return source==0||source==2;
#endif
}
}
