# Experimental prepared guides

Before starting the game, explicitly set environment variable
`NEUROTIC_PREPARED_GUIDES=1`. This reserves NR processing for this add-on for the
entire process and makes the core bypass ordinary DXGI/Vulkan NR processing.
Frame generation and original presentation remain independent. All model routes
(direct/shared/helper) require this opt-in; missing/failed prepared input leaves
the source unchanged. Remove the variable and restart to restore ordinary NR.
Explicit Vulkan SharedGpu=1 + TransportOnly=1 diagnostics are exempt because
they perform an actual GPU roundtrip without evaluating NR. Capability refusal
in that diagnostic mode never falls back into helper model evaluation.

This optional add-on/shader/helper path leaves product defaults unchanged. It
captures ReShade-selected raw depth and motion guides for the existing NR renderer.
It excludes fitted cameras and synthetic jitter. All platform paths are implemented
and compile-checked; none is game/GPU/presentation-qualified in this checkout.

Prerequisites: an add-on-enabled ReShade runtime, ReShade.fxh, the selected external
motion producer (unless using integrated D3D12 provider 5), the built NeuRotic core,
and the model/forwarder dependencies. External shaders and NVIDIA runtime/model
binaries are not bundled or relicensed. MIT donor attribution is preserved in
LICENSE-DLSS5-FEEDER.txt. See flow and helper licences/dependency requirements.

Build flow with `tests/prepared_flow_backends_build.cmd`, build the helper with
`addons/prepared-guides/helper/build_helper.cmd`, and then run this directory's `build.cmd` for x64; `build.cmd x86` emits addon32 and
uses the x64 helper for model execution. Stable caller outputs are under this
checkout's `builds/prepared-guides`; copy the matching add-on and its required
runtime dependencies (including WinPixEventRuntime.dll for x64) to ReShade's add-on
folder. Install shaders from `shaders/` in the configured effect search path.
Keep prepared guide technique enabled. Documentation: [caller](src/README.md),
[shader contract](shaders/README.md), [flow](flow/README.md), [helper](helper/README.md).

Compile `NRPG_MV_PROVIDER` selects 0 common DRME/qUINT/dh_uber_motion, 1 Launchpad,
2 VORT, 3 Lumenite Kernel, 4 Lumenite QuantMotion, 5 integrated x64 D3D12 flow.
For modes 0..4 put the selected external technique before NeuRotic_PreparedGuides.
The host accepts only its actual current render event and checked resource shape.
A valid selected ReShade depth binding and exact matching SDR swapchain are required.

ReShade configuration is read from `[NeuRoticPreparedGuides]`:

```
[NeuRoticPreparedGuides]
FlowBackend=software
; Alternative: nvidia or auto (provider 5, x64 D3D12 only)
HelperExe=C:\absolute\NeuRotic.PreparedHelper.exe
Model=C:\absolute\nvngx.dll
Forwarder=C:\absolute\nvngx.dll_dlssnr.dll
; AdapterLuid=00000000:00000000
; NgxPath=C:\absolute\public-ngx.dll
; ColorStrength=0.0
; ProbeContent=1
; SharedGpu=1
; TransportOnly=1
```

Default x64 D3D12 uses Prepare/Record/Seal on an owned private DIRECT list and the
existing renderer. Provider 5 consumes completed immutable captured color through
software FFX or optional NVIDIA optical flow and publishes full-resolution pixel
vectors plus distrust only after its actual fence completes. Unknown/pending flow
never establishes valid guide history. Motion/capture identity cannot be relabelled.

D3D10/D3D11/Vulkan/OpenGL and every x86 caller use completed CPU readback/upload
transport into the loaded core's D3D12 normalization and node renderer. Owned
typed textures use the observed source adapter; missing adapter identity blocks
this route. Output requires actual private GPU completion. This synchronous
compatibility path can be expensive at high resolution. Explicit NgxPath selects
the separate helper also for x64 D3D12 and selects
genuine public D3D11 DLAA before NR. A native source adapter LUID property overrides
manual helper selection when available; OpenGL without that property needs the
explicit NgxPath helper and AdapterLuid.
This CPU route makes no cross-adapter GPU-sharing claim. ReShade on D3D9 needs a
wrapper exposing one of the actual supported APIs. HDR, unknown presentation
space, unsupported formats and missing prerequisites bypass without relabelling
color as SDR. CPU helper input is SDR RGBA/BGRA8, raw R32F depth, RG16F/32F pixel
motion and R8 distrust. Private NR treats distrust diagnostically; the optional
public DLAA path can bind it through documented NGX parameters.

Optional source-guided color composition: helper ColorStrength defaults to 0 and
operates on its completed source/output pair. Direct shader color composition is
compile-off by default; set NEUROTIC_SOURCE_GUIDED_COLOR=1, enable its UI control,
and order NeuRotic_SourceGuided_Save, NeuRotic_PreparedGuides,
NeuRotic_SourceGuided_Apply. Host stamps require a same-frame saved source and a
successful exact-capture output. Shader color refinement supports encoded SDR8
only. Avoid enabling helper and shader composition together.

Transfers preserve source states and require actual completion before reuse.
Four caller session slots include quarantined failures; uncertain private work
and owned resources are retained. Private waits are bounded; the conservative
source ReShade wait_idle API is blocking and supplies no timeout. No game queue
waits on an uncertain model/flow fence. Source/resource generation changes, dropped
outputs and reload invalidate consumer history. Hidden camera cuts are handled by
producer/content heuristics, not fitted geometry.

ProbeContent explicitly samples actual completed readbacks and returned output,
then verifies a second GPU readback after upload and a before-Present backbuffer
sample. PresentAttempt is not ObservedDisplay. Other later effects may legitimately
change the backbuffer. The optional probe adds synchronous readback overhead.

Verification: x64/x86 MSVC add-on builds, native session/half-float/row policy tests,
and eight shader source/math tests. Flow and helper have their own build/check
receipts. No ReShade FX compiler was available, so effect compilation remains
unverified. Live model execution, API/backend combinations, resource-state
conformance, quality, performance and independently observed display output remain
experimental. A compiled or completed helper reply is not game acceptance.

Optional x64 Vulkan `SharedGpu=1` attempts the real core Vulkan-to-D3D12 shared
GPU capture/render/copyback route when the observed source device enabled the
required external-memory, external-semaphore and timeline extensions and its
physical adapter matches D3D12. It does not enable extensions after creation.
A pre-record capability refusal permits CPU mainline fallback. Referenced uncertain
work quarantines the session and suppresses fallback. NgxPath explicitly uses the
CPU helper instead. Canonical provider/owner release evidence is required before
retiring shared packets; completed frame output alone does not invent that proof.
This shared path compiles; live GPU interoperability remains unverified.

With SharedGpu=1 on Vulkan, explicit TransportOnly=1 performs the actual shared
GPU color roundtrip and completed recapture diagnostics while skipping model
Record. It logs bounded input/processed/copyback samples and cannot establish
model evaluation or displayed pixels. Ordinary processing remains the default.

When a core is loaded, also enable NR and select a saved Present route (route 1
or 2). The caller requires NeuRotic_PreparedGuidesEnabledV1 eligibility and
refuses an older identified core without it. Saved native route 0 cannot compete
with the prepared path. Direct dispatch privately requires prepared guides while
preserving the saved route setting. The default CPU route requires the loaded
core and its Prepare/Record/Seal exports. Without a loaded core, explicit process
environment opt-in only permits the separate NgxPath standalone helper route.

NeuRotic's input-readiness UI exposes "ReShade prepared inputs": observed depth,
estimated motion, stage/reason, and distinct capture/model/copyback counters.
The observation export freezes these as prepared_external_inputs separately
from native owner-validated guides. Missing, blocked and stale inputs cannot
establish native camera/jitter evidence or independently observed display.
