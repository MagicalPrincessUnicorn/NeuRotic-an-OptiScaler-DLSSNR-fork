# ReShade prepared-guide caller

Build `addons/prepared-guides/build.cmd` for x64 or pass `x86`. Stable outputs are
`builds/prepared-guides/NeuRotic-PreparedGuides.addon64` and the `x86` subdirectory's
`.addon32`. The x64 caller links the checkout-local completed FFX/NVOF flow build;
run `tests/prepared_flow_backends_build.cmd` first. Bundle WinPixEventRuntime.dll
and the dependencies/licences identified by the flow backend documentation.

Install the add-on, guide shader and ReShade.fxh in an add-on-enabled ReShade
runtime. External provider modes 0..4 require the separately installed provider
technique above `NeuRotic_PreparedGuides`. Common DRME/qUINT/dh_uber_motion,
Launchpad, VORT and Lumenite mappings arm only after the selected technique's
actual after-render event and checked texture shape. A meaningful selected
ReShade depth binding is required. Camera fitting and synthetic jitter are absent.

`NRPG_MV_PROVIDER=5` selects the integrated x64 D3D12 flow producer. ReShade config
section `[NeuRoticPreparedGuides]`, `FlowBackend=software` (default), `nvidia` or
`auto` selects its backend. A source capture is copied into an owned immutable
image, retired by its actual private fence, then submitted to the flow backend.
Only an actually completed output for the same capture/generation is bound to
`NRPG_BUILTIN_MOTION` and `NRPG_BUILTIN_DISTRUST` before the guide technique.
The output motion is full-resolution current-to-previous pixels. Shader sampling
converts pixels to UV once and the prepared output converts UV back once. The
consumer restores NON_PIXEL state and registers the actual source queue's reader
fence before destroying output leases and returning the backend slot. Startup,
cut and warmup flags reset consumer history without continually resetting the
producer's six-capture startup history. Provider 5 currently requires x64 D3D12;
other API callers use separately installed external provider techniques.

Default x64 D3D12 uses the existing core exports Prepare/Record/Seal, an owned
DIRECT queue/list/fence and private color/depth/motion/distrust copies. Prepare
precedes successful list Reset; Seal follows submission; Recorded alone is not
completion. Successful private Signal and observed completion precede runtime
copyback. The private renderer's distrust mask is diagnostic (`requireModelDistrust=0`).
Raw depth R32F, RG16F/RG32F motion and R8 distrust are full-size, single-sample,
single-mip and one layer. Direct color accepts encoded SDR RGBA8 or RGBA16F.

D3D10, D3D11, Vulkan, OpenGL, and every x86 caller use the completed CPU helper
transport. An explicit `NgxPath` also selects the helper for x64 D3D12, enabling
genuine DLAA through the helper's public D3D11 NGX host before NR. Required config:

```
[NeuRoticPreparedGuides]
HelperExe=C:\absolute\NeuRotic.PreparedHelper.exe
Model=C:\absolute\nvngx.dll
Forwarder=C:\absolute\nvngx.dll_dlssnr.dll
; Optional: NgxPath=C:\absolute\public-ngx.dll
; AdapterLuid=00000000:00000000
; ColorStrength=0.0
; ProbeContent=1
; SharedGpu=1
; TransportOnly=1
```

Native adapter LUID comes from the source ReShade device property when available;
otherwise an explicit adapter selection is required (including OpenGL). This CPU
route does not assert cross-adapter GPU sharing. Only typed SDR RGBA/BGRA8 input
is accepted. GPU texture-to-buffer copies use 256-byte row strides when supported;
D3D10/11 staging textures provide the fallback. Completed mapping converts RG16F
motion to float2 without scaling its pixel units and R8 distrust to floats. OpenGL native rows and vector Y are converted from bottom-left to canonical
top-left and uploads reverse the row conversion. Packed payloads are capped at
160 MiB. The helper validates the exact capture/device/
generation and completed reply before the caller uploads the returned BGRA8 and
establishes source completion. Public DLAA can bind distrust; private NR remains
diagnostic. Model initialization/provider failure bypasses; no readiness is invented.

Source color is the actual callback RTV in RENDER_TARGET and guide textures are
in combined PIXEL/NON_PIXEL shader read at the documented effect boundary. Exact
states are restored after capture/copyback. Source baseline flushes ReShade's
immediate list, calls its source queue wait_idle, then requires successful source
queue Signal, bounded fence wait and observed completion. wait_idle itself has no
timeout/result in this SDK; this conservative baseline is not performance-qualified.
Private flow/model waits are bounded at five seconds; helper pipe waits are bounded
by the client. No game queue waits on an uncertain provider fence. Failures retain
owned allocations, submitted work, source COM aliases where available and module
leases. Four total sessions include retained failures. Capacity exhaustion bypasses.
A destroyed Vulkan/OpenGL borrowed game image cannot be made owned by the add-on;
retaining private work does not invent rights to that source handle.

Optional shader color refinement uses compile definition
`NEUROTIC_SOURCE_GUIDED_COLOR=1` and order SourceGuided_Save, PreparedGuides,
SourceGuided_Apply. The add-on supplies save and successfully processed exact
capture/generation stamps for the direct path; missing/reordered events bypass.
This shader is encoded SDR 8-bit only. Helper `ColorStrength` instead invokes its
completed source/output CPU composition. Avoid enabling both paths together.

Explicit `ProbeContent=1` logs bounded pixel digests from actual capture/prepared
CPU input, returned helper output, completed second GPU readback of copyback, and
actual backbuffer readback at the before-Present event. Samples share a canonical
BGRA representation and exact declared identities. Later effects may legitimately
change the presented image. PresentAttempt is not ObservedDisplay; no successful
Present return or uploaded bytes establish display observation. The direct D3D12 caller does not yet supply equivalent capture/copyback
pixel probes; its completed fence establishes transfer completion only.

Dropped output, frame gaps, resize/reallocation, reload, selected depth destruction
and provider/depth changes break consumer history. Hidden game-camera cuts cannot
be inferred universally; producer/shader content tests supply rejection heuristics.
Native policy and codec tests plus eight shader source/math tests pass. Both add-on
architectures compile with MSVC. The effect files have no available ReShade FX
compiler here. Live GPU execution, all API/backend combinations, resource-state
conformance, image quality, performance and displayed-output qualification remain
unverified.

Optional x64 Vulkan `SharedGpu=1` attempts the real core Vulkan-to-D3D12 shared
GPU capture/render/copyback route when the observed source device enabled the
required external-memory, external-semaphore and timeline extensions and its
physical adapter matches D3D12. It does not enable extensions after creation.
A pre-record capability refusal permits CPU helper fallback. Referenced uncertain
work quarantines the session and suppresses fallback. NgxPath explicitly uses the
CPU helper instead. Canonical provider/owner release evidence is required before
retiring shared packets; completed frame output alone does not invent that proof.
This shared path compiles; live GPU interoperability remains unverified.

With SharedGpu=1 on Vulkan, explicit TransportOnly=1 performs the actual shared
GPU color roundtrip and completed recapture diagnostics while skipping model
Record. It logs bounded input/processed/copyback samples and cannot establish
model evaluation or displayed pixels. Ordinary processing remains the default.

All NR paths require NEUROTIC_PREPARED_GUIDES=1 set before game start. The core
uses this exclusive process route to suppress duplicate ordinary NR evaluations
while keeping independent frame generation/presentation active. TransportOnly
Vulkan diagnostics perform no NR and can run without the opt-in.

When a core is loaded, also enable NR and select a saved Present route (route 1
or 2). The caller requires NeuRotic_PreparedGuidesEnabledV1 eligibility and
refuses an older identified core without it. Saved native route 0 cannot compete
with the prepared path. Direct dispatch privately requires prepared guides while
preserving the saved route setting. Without a loaded core, the explicit process
environment opt-in is sufficient for the standalone CPU helper route.
