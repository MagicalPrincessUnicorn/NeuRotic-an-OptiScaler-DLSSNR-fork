# SDK checked closure development boundary

This directory contains **experimental SDK mechanics, not a qualified FG capability**. Both public query definitions return `FFX_NR_UNSUPPORTED` and publish no function table. The product must continue to refuse checked FG selection. The full-v1 build wrapper includes the original algorithm providers and checked source instrumentation, but compiling that module grants no runtime qualification or algorithm release contract.

## R16 candidate: actual algorithm ownership and manual measurement

The current source adds the explicitly opted-in one-shot CheckedExperimentalService,
plus an abstract AlgorithmLease implemented by the creating module. Both qualified
queries still return UNSUPPORTED; coverage flags remain zero. This candidate does
not activate product C04 or Strict operation.

The real v1 creator registers exact algorithm contexts. Selection requires the
default allocator, the same actual device, synchronous flags-zero FP16 operation,
and no prior use. The creator retains the exact owned output and actual Prepare
inputs before opaque algorithm entry. Admission and tombstones guard public API
and callback entry before dereferencing context memory. After the SDK's exact
frozen drain, the creator invokes whole-context destruction once. Nested destroy
errors and exceptions retain uncertainty and prevent retry. Successful release
publishes an independent algorithm receipt, preserves immutable identity, and drops
actual COM/context anchors. SDK consumption requires this independent release.

The SDK writer finishes through observed GPU completion and actual allocator/list
Reset plus empty Close before Present. Actual callback submission publishes a
retained immutable receipt; live consumer actions can be reacquired around the
real Close/Execute/Signal boundary.

The build-only manual_fg_probe worker requires explicit consent, a hash-pinned DLL,
and an explicit adapter LUID or WARP selection. It directly writes the owned output,
invokes real Prepare and FI/OF dispatch, then measures drain and whole destruction.
Its oracle binds process identity, context/revision, actual submission, raw drain
domains and final device status. The full algorithm worker has NOT been executed
here. Future successful measurement grants no product qualification.

Full DLL and worker use MSVC 14.51.36231, C++20, Release /MD. The dormant product
factory uses 14.44; crossing that C++ interface from product remains unqualified.
The factory now consumes authentic prior SR and NR retirement receipts, but no
product caller is enabled by this SDK experiment.

Current evidence: 799 CPU/source-cluster WARP checks and 11 Python oracle tests.
Full algorithm DLL/probe are built and inspected statically only. Final source and
artifact hashes are in C:/NeuRotic/builds/w03-fg-sdk-closure/SDK-R16-HANDOFF.md and
sdk-r16-source-receipt.json. The sections below preserve earlier pre-algorithm
history; their 718-check receipt and DLL hash are historical.

## Historical pre-algorithm scope implemented

The ABI-private C header preserves the existing AMD public structures. It adds creation-registry identity, closure begin/poll/domain/consume tables, separate dispatch tickets and actual callback/submission receipts. Pointer values are observations and registry locators: they do not grant application writes or algorithm release rights.

The v1 DX12 implementation instruments one fixed synchronous route configured before first Present: SDK-owned command lists, default compositor, no UI resource, no debug pacing, no raw-list escape, no wait callback, no waitable-object API, no resize/reconfiguration. Creation aliases map the actual swapchain and original ffxContext wrapper. Root/descendant/callback/list accounting, both worker slots and local jobs, all six shared fence roles, every pool slot, actual queue operations and terminal queue markers are retained. Shutdown consumes a specific completed internal receipt; uncertain failures retain the context pin. Checked helper waits and newly entered selected idle waits are bounded. Enrollment and wrapper destruction share a lifecycle claim, and enrollment pins the raw owner before invoking its callback. Each SDK worker retains its registry sidecar at startup, observes shutdown without further registry lookup, uses 50ms waits once selection is observed, and refuses unclaimed jobs before executing helper work. Nonselected operation continues through the legacy path.

Both helper implementations contain checked creation/reset/close/execute/signal/drop operations with device-removal sentinel checks. v2 has partial source instrumentation but no enrollment owner and no usable capability.

Enrollment now wakes both actual workers and waits for each to acknowledge the exact enrollment epoch immediately before a finite wait. This acknowledges departure from any previous legacy infinite wait. The transition has a common 250ms budget. Event creation/publication failure or missing acknowledgement quarantines the context and retains its owner pin; it cannot reopen legacy admission or allow destruction. Worker bounded mode remains sticky after the transition begins.

The private controlled-output experiment additionally supports a one-shot, pre-first-Present FP16 UAV replacement allocation with no prior ordinary GetBuffer escape. `CheckedOwnedOutput.h` exposes only an abstract SDK-issued lease. Its implementation stays in the creating SDK translation unit, validates live writer/consumer windows, retains the exact COM allocation and generation, refuses ordinary GetBuffer/recycling, and lends actual closure tokens for actions. The SDK calls prepare before Reset, calls the writer after successful Reset, then performs checked Close/Execute/Signal under a caller-provided submission guard. The WARP writer directly clears this same caller Output; no transfer copy is added. A failed writer publishes no output and never executes its list.

## Verified locally

Stable output: `C:/NeuRotic/builds/w03-fg-sdk-closure`.

- CPU accounting/registry/export-refusal controls: 184 checks, zero failures; includes 100,000 callback-to-child transfers under concurrent polling.
- Actual D3D12 helpers on WARP: 36 v1 checks and 19 v2 checks, zero failures. Includes pool exhaustion and real device removal. WARP Signal/Wait returned S_OK after removal; UINT64_MAX completion still correctly retained uncertainty.
- Worker/event transition controls: 37 checks, zero failures. Includes already parked legacy workers, exact acknowledgements, missing worker timeout, actual CreateEvent name collision and SetEvent access denial, quarantine and pin retention.
- Actual v1 swapchain, compositor, two workers and queues on WARP: seven modes with 59/57/58/59/60/86/63 checks, zero failures. Modes cover empty synthetic callback, callback error despite requested output count1, actual Close rejection before Execute, invalid actual Present/GetBuffer results, genuine controlled-output writing and callback consumption, and writer failure without Execute. Also covers ticket forgery, cross-owner reentry, lifecycle exclusion, live action windows, one-shot refusal and retained COM allocation. A deterministic cross-thread end during action acquisition reproduced a token-publication race; acquiring/active/ending states now prevent token access until publication. The test hook is compiled only into the local test executable.
- v1 main/helper/provider translation units compile under MSVC14.44 C++20.
- v2 helper compiles. v2 main/provider fail on missing `Kits/FidelityFX/amdinternal/api/internal/dx12/ffx_provider_external.h`, included by `api/internal/ffx_provider.h:97`. No substitute header or build flag bypass was supplied.

These tests execute no real FG algorithm, driver provider, game, or physical GPU. The v1 executable deliberately links the SDK backend with algorithm shader families disabled; its success is not a complete shipping FidelityFX build.

Separately, the original full v1 API and algorithm families were built and linked with MSVC14.51/VS18, C++20, using the stable `full-v1` cache. All 144 generated permutation headers are present. The original FSR2, FSR3 upscaler, optical-flow and frame-interpolation sources and checked swapchain sources compile; `CheckedSdkExports.cpp` is part of the DLL target. Static export inspection confirms five original API names and both unsupported checked-query names. The DLL is 6,706,176 bytes, SHA256 `fe8fb3523372036037a167c9ebedcf81f00567a043fac948e49905cbd0de07ce`. It has **not** been loaded or executed. It is not installed into a product package or allowlisted. Final build/cleanup and source manifests are in `sdk-final-source-receipt.json` and `SDK-FINAL-HANDOFF.md` under the stable output directory.

The first sandboxed MSBuild compiler probe failed in FileTracker with E_ACCESSDENIED. Authorized elevated configure succeeded, but its supervisor timed out waiting for a lingering child and cleaned its exact job; the configure process exit was zero. Full build and final incremental relink both exited zero. Each residual MSVC vctip helper was identified through the exact supervisor's own job handle, then that exact job was terminated; the original supervisor independently observed parent exit zero and an empty job. These outcomes are recorded separately rather than treating a configure timeout as an ordinary successful supervised run.

## Remaining qualification work

The full production service must remain unavailable until all owner/lifetime and negative-path coverage is reviewed and tested. Remaining examples include broader concurrent lifecycle stress, genuine failing queue Signal/Wait HRESULTs (WARP removal did not produce them), and default-compositor allocation/reset failure schedules. Coverage flags remain zero until a matched contract defines and qualifies them. Actual Present/GetBuffer error observation tests do not establish every compositor failure path.

The SDK dispatch ticket alone reports observations and grants no write rights. The private owned-output lease proves only its narrow SDK allocation and writer lifecycle. The existing selected SR/NativeAfter route still lacks a complete prior-output registration/release and future-entry exclusion contract; no production C03 adapter is enabled. The independent FG algorithm release contract remains unknown. Neither a swapchain completion receipt nor the synthetic callback tests close these holds. Shared module compositor resources are retained by checked cleanup rather than released while another swapchain may use them.

`CheckedSwapchainProvider.cpp` is an unbuilt inventory source for a possible future swapchain-only provider DLL; it is not a delivered provider. No file digest allowlist or production export qualification exists.
