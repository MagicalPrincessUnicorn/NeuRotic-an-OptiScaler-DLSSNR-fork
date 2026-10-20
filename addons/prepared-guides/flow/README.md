# Prepared D3D12 optical-flow producers

`PreparedFlowD3D12.h` exposes an executable producer, independent of proprietary
upscalers. `Session` owns a private D3D12 queue, command recordings, fences,
backend context, normalization shader, full-resolution RG16F motion, R8 distrust
and producer history. The caller supplies an owned immutable whole color image
and its actual same-device producer fence/value. No game texture is registered
with the NVIDIA runtime.

The default software backend calls the FidelityFX optical-flow 1.1.2 context
create/dispatch/destroy APIs and the SDK's native D3D12 interface. `Auto` tries
installed NVIDIA API5, then falls back to software only after a safely closed
NVIDIA attempt. Explicit `Nvidia` does not silently select software. Selection
and reasons are reported independently from GPU qualification.

FidelityFX output is signed integer pixel displacement on an 8-pixel grid. NVIDIA
output is signed S10.5 displacement on a 4-pixel grid, divided by 32. The shader
expands either grid to full-resolution current-to-previous **pixel** vectors,
without multiplying by grid spacing or input dimensions. NVIDIA submits current
as `inputFrame`, previous as `referenceFrame`, and requests both directions plus
cost. The normalized output always has grid1, scales1.

Distrust includes reset, out-of-image reprojection, nonfinite values, and
reprojected luminance disagreement. NVIDIA adds actual cost and forward/reverse
consistency. FidelityFX has no reverse-flow output in this lane, so its mask does
not claim that check. Appearance disagreement never overwrites a finite measured
vector with zero. FidelityFX's first six frames after reset are warmup; the SDK's
low four scene-history bits mark current/recent cuts. Both force mask1. Poll
reads the scene history only after the producer fence completes. `sceneCut`
therefore means current/recent cut history, not a recovered camera observation.

## Caller sequence and ownership

1. Initialize with actual resolution and transfer encoding. Supported dimensions
   are 256..4096 by 64..2160. The minimum avoids the SDK's seven-level packed-luma
   search reading fewer than four horizontal pixels. Software positively queries
   shader model6.2 and wave operations (the SDK's compiled wave32 permutation
   target is cs_6_2); creation may still refuse driver limits.
   NVIDIA positively queries dimensions, grid4 and grayscale/vector/cost formats,
   initializes BOTH directions, and supports SDR sRGB in this profile.
2. Submit a private, immutable, single-sample one-mip color texture with a retained
   ownership lease and actual producer completion. Typed RGBA8, BGRA8, RGBA16F and
   RGB10A2 are accepted; encoding is supplied independently of format. A normal
   pair must name the last actually submitted capture; a reset names previous0.
   First capture, stream changes and generation changes require explicit reset.
   Repeated/out-of-order captures and same-texture current/previous are refused.
3. Poll until the actual private completion fence finishes. Poll never waits on
   a CPU event and never converts elapsed time into completion. Do not relabel
   the output's exact capture pair to match a newer addon effect boundary.
4. Use `Output.motion` and `Output.distrust` in
   `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE`. The consumer must restore
   this state and guarantee that its command recordings cannot be replayed.
   Register every consumer's actual queue signal **after** its read using
   `AddReader`. Registering an unrelated/already-signaled fence is not authority
   to discard a reader. At most eight readers are retained per output.
5. Destroy all copied Output ownership leases and resource aliases, then call
   `ReturnOutput`. The next Submit remains blocked until all registered readers
   actually complete. `Close` likewise requires real completion and return of
   output. Input color is retained as previous history until it is no longer
   needed. ReturnOutput is also required when discarding unused completed output.

Input `reset` must come from actual upstream history discontinuity. Do not feed
every `Output.warmup` or recent `sceneCut` back as a fresh input reset: that would
restart the six-capture warmup continuously. Consumer guides may separately be
invalidated during those output intervals.

`PreparedFlowContract.h::ApplyCompletedFlow` fills only flow-derived descriptor
fields after matching the caller's actual capture/stream/generation pair. The
caller retains authority for device, producer, session, color/depth and completion
transport. The producer does not certify engine-native guides, model acceptance,
displayed output or game performance.

Failure after possible submission/registration poisons the session. Destruction
retains and pins the whole context, module, aliases and upstream ownership for
process lifetime when retirement is uncertain. No timeout or cancellation frees
possibly used resources. Four process-wide sessions bound this quarantine; a
quarantined slot is never made reusable. Methods are caller-serialized.

## Build and verification

Run `tests/prepared_flow_backends_build.cmd`. It reuses
this checkout's `builds/universal-guides/flow`, builds SDK OF-only component and backend
targets from `external/FidelityFX-SDK`, and links
`neurotic_prepared_flow.lib`. No SDK or build tree is copied. CMake does not enter
the SDK's top-level build, which otherwise writes into its source/bin tree. An
inert marker handles the SDK shader-output variable-name dependency bug; all 28
real shader-permutation headers remain mandatory build dependencies.

Consumers link `neurotic_prepared_flow.lib`, `lib/ffx_opticalflow_x64.lib`,
`lib/ffx_backend_dx12_x64.lib`, `d3d12`, `dxgi`, `d3dcompiler` and the existing SDK
`sdk/libs/pix/bin/x64/WinPixEventRuntime.lib`. Deploy that directory's
`WinPixEventRuntime.dll` beside the addon. The NVIDIA driver DLL is never bundled;
it is loaded from System32 only. The two API5 declarations in `vendor/nvof/` were
copied from the independent pipeline donor with their permissive header-only
notices intact. The wrapper follows that donor's separate GPU completion and
vendor unregister lifetime rule without importing its core managers.

Local evidence: SDK shaders and native sources compile/link; 48 portable
algorithm, negative admission, empty-session and conversion shader compilation
checks pass. These checks do not exercise GPU execution, installed NVIDIA runtime,
ReShade lifecycle, proprietary models, image quality or games. The addon caller
and its exact capture-boundary admission are integrated separately.

## Native core integration

`OptiScaler/dlssnr/NativeGuideProcessor.cpp` now uses this software backend inside
the core DLL. It owns immutable captures, upload/readback queues and completion;
the existing CPU mainline client then invokes the same Prepare/Record/Seal ABI.
No ReShade resource, addon, external helper, runtime shader file or external
prepared-route environment variable is required by this processor. Camera
remains unknown; observed raw device depth and derived motion keep their distinct
provenance. Capture dimensions are additionally bounded by the mainline client's
160 MiB packed payload ceiling. Uncertain work quarantines the singleton until
process restart; another capture cannot allocate around that retained owner.

The core's existing FSR3.1 library uses optical-flow ABI 1.1.1. The new 1.1.2
component, DX12 backend, shared routines and wrapper are compiled together with
private `nrNative_` symbol prefixes in CMake. Rebuild all three libraries after
this change, and link their **explicit paths** from `builds/universal-guides/flow`;
keep the existing core FSR libraries unchanged. `tests/build_native_flow.cmd`
builds the Release cache. Pass `Debug` to build the separate `flow-debug` cache
with the matching debug C++ runtime; the product project selects that cache for
Debug builds. The Debug variant has not been qualified by the Release tests.
`tests/build_native_flow.cmd`
reuses the existing SDK build script and cache. `tests/test_native_guide_processor.cmd`
forces both ABI families into one executable and exercises actual WARP software
flow and D3D12 guide transport using an explicit model recorder.

The isolated family also includes the DX12 backend's generic C++ callbacks,
barrier/resource helpers and `BackendContext_DX12` type. Older FSR2 backends export
names such as `TIF`, `addBarrier` and `flushBarriers`, so prefixing only `ffx*`
entrypoints is insufficient. The transport test forces the existing FSR2 DX12
entrypoint into the executable alongside the original FSR3.1 OF/DX11 libraries.
The symbol audit covers all 12 existing FSR2/FSR2.2.1/FSR3.1 release archives.

The pinned FidelityFX SDK retains its AMD 2024 permissive license in
`external/FidelityFX-SDK/LICENSE.txt`; preserve the existing distributed
third-party notices. No shared SDK source is modified. Release dependency
inspection and execution without a neighboring PIX DLL show no mandatory PIX
runtime import for this native software path; Windows D3D12/DXGI/D3DCompiler and
the VC runtime remain required. The optional NVIDIA path and its notices remain
unchanged, and native processing explicitly requests software.

Native WARP checks cover real depth/motion/distrust upload, BGRA preservation,
six-frame warmup, paired history, resize/session resets, preparation then
delivery, malformed input rejection, refused seal and owner quarantine. They
establish transport execution, not private-model inference, game presentation,
image quality or frame-time qualification. The current bridge performs bounded
synchronous GPU waits and CPU guide copies; game performance remains unqualified.
