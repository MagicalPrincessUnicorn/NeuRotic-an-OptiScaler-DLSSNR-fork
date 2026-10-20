# Optional prepared-guide helper

`build_helper.cmd` builds `builds/universal-guides/helper/NeuRotic.PreparedHelper.exe`
and its pinned NR forwarder. Dependencies are read from `NR_DEPENDENCIES`, defaulting
to `external`. No model or public NGX runtime is bundled,
downloaded or selected automatically.

The executable requires explicit absolute `--model` and `--forwarder` paths and
`--adapter-luid high32:low32`. The D3D11 capture/NR relay and existing `nrw::NrHost`
create their private GPU devices on that exact adapter. Rendering is lazy: a valid
frame must arrive before any GPU/model initialization.

`PreparedHelperClient.h` provides a serialized, bounded Windows child-process
client. Set `ClientOptions` with those paths, adapter LUID, and timeout (maximum
120 seconds), call `Start`, then `Process(Request, packedPayload, Reply, output,
reason)`. Child stdin/stdout use fixed-width pointer-free envelopes; only the
required child handles are inherited. Parent transfer timeout closes the session
and preserves any unresolved I/O packet. No helper fence is inserted into a game
GPU queue. Client Stop closes its endpoints; it does not fabricate cancellation
or terminate arbitrary processes.

The version-1 request is 288 bytes and reply 136 bytes on x86 and x64. A frame
payload is bounded to 160 MiB and contains tightly packed active-region arrays:
BGRA8 SDR color, float device-depth [0,1], float2 motion at the declared grid, then
optional float distrust [0,1]. The sender must finish its actual source readback
before asserting `CallerReadbackCompleted`. The helper calls the shared prepared
normalizer, expands grid sampling, and converts displacement into capture-pixel
current-to-previous motion once. The host applies its separate guide-to-model
resolution ratio. Captured device depth/reversal remains distinct from the
existing window worker's default normalized relative inverse depth.

`NrHost.Process` performs actual provider evaluation/composition/completion.
The helper then copies the owned BGRA output into staging, waits for a real D3D11
event query, maps that completed staging image with its actual row pitch, and
only then emits `CompletedSuccess`. The exact producer/session/stream/device/
generation/capture and actual host completion value accompany output bytes.
Failures and cancellation carry no valid output. Unknown versions, malformed
sizes, nonfinite values, invalid masks and stale captures are refused. This CPU
transport is a conservative optional fallback, not a performance result.

Optional `--ngx <absolute public NGX runtime>` enables the helper-owned genuine
D3D11 DLAA adapter before NR. Its prepared inputs retain their provenance.
Optional distrust is bound to public DLSS current-color bias; the reply separately
records DLAA evaluation/mask consumption. Without DLAA, distrust is diagnostics
only because the private NR interface has no supported mask binding. This path
does not certify native engine motion or implement frame generation.

Optional `--color-strength 0..1` (default 0) runs the same-capture source-guided
SDR color experiment after completed NR readback. Optional `--probe-content 1`
records at most sixteen captures of bounded actual source/prepared/processed byte
samples to stderr, including capture, generation, stage and hash. Prepared samples
come from completed helper input staging readback; processed samples are the
returned output after optional color composition. Present returns and helper
output receipt do not establish displayed pixels. The addon must independently
verify upload/copy-back and associate its own Present attempt.

`tests/prepared_helper_tests.cmd` checks normalization/layout under x64 and Win32
and performs an actual x86-client/x64-helper cancellation exchange without GPU or
model initialization. Real model evaluation, legacy capture/copy-back, shared GPU
transport and game/display qualification are separate observations; this helper
alone does not establish those outcomes.

The x64 addon also owns an optional `SharedGpu=1` Vulkan route through
`src/SharedGpuClient.h`. It uses the existing observed Vulkan device/queue
generation and enabled extensions, matches the physical adapter LUID, and imports
dedicated D3D12 images/fences. Queue operations stay with the serialized ReShade
queue owner. Source copy, private NR submission and Vulkan copyback each require
real fence completion within a bounded wait. `TransportOnly=1` skips the model;
it performs the actual shared roundtrip, recaptures the actual destination into
completed D3D12 readback, and compares bounded capture/prepared/processed/copyback
samples. Logs include exact capture identity and never claim observed display.

`SharedGpuClient::Process(queue, descriptor, sources, reason, transportOnly)`
accepts whole-image SDR RGBA8/RGBA16F color, R32 depth, RG16/RG32 motion and optional
R8 diagnostic mask, with one mip/layer/sample and at most 4096 by 2160 pixels.
Capability refusal before recording permits the CPU helper fallback. Uncertain
referenced work sets `RequiresRestart`; the client never resumes or replays that
slot. Its bounded native packet, private device/queue/images and core module lease
remain retained until canonical completion/provider/recording release evidence
is supplied to `Retire`. The borrowed source Vulkan images remain owned by the
host; retaining the addon session grants no right to keep those images alive if
the host destroys them after uncertain source work. This boundary needs runtime
qualification. Normal NR use requires the explicit prepared process route;
transport-only diagnosis makes no NR model call.
