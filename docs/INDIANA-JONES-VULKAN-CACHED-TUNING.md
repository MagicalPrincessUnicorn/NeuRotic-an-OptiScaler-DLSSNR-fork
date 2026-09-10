# Indiana Jones Vulkan NR cached tuning experiment

Date: 2026-09-10. Branch: `exp/indiana-jones-vulkan-nr-cached-tuning`.
Exact parent and named control: `6e36b8b27e1760031522805528ecf9ec8a6d1fb2`.
This deliberately extends the inconclusive containment experiment at the user's explicit request.
Result: **Inconclusive pending tester runtime evidence**. Decision: **keep experimental**.
No baseline, installation, INI, NVIDIA model DLL, D3D12 rendering path, or FG setting change.

## Hypothesis and approved combined change

The control ran NR after successful native non-upscaling feature evaluations, including FG.
Its Vulkan forwarder used a shared, unverified float setter, and it did not recreate a model
when immutable model tuning changed. Correct those entry points and use a bounded retained
configuration cache together. This tests their combined effect, not independent attribution.
The tester's FG-off run avoided the freeze but did not establish correct model tuning or image quality.

- Native pass-through NGX features are mapped at both creation APIs, with successful-release removal
  and fresh generations on handle reuse. These evaluations return directly without entering NR.
- Existing owned primary RR selection, auxiliary exclusion and duplicate suppression are unchanged.
  Quality and Performance remain the temporary selected post-RR path. No pre-RR substitution.
- The host writes and reads back typed NGX creation parameters. Versioned `dlssnr_vk_*_v2` exports
  forward prepared blocks without the shared vtable setters. D3D12 and legacy exports are unchanged.
- Seven tuning fields identify eight retained slots, including the initial configuration. Each owns
  its feature, parameters and creation settings. Matching slots switch without allocation and request
  history reset. Slider release commits the request; composition controls remain live.
- A failed creation or parameter check retains its attempted slot and any allocations, without retrying
  each frame. Therefore failures also consume the eight-slot budget. A ninth distinct request leaves
  the applied slot active. Returning all controls to an existing configuration selects it again.
  The UI distinguishes requested slider positions from the applied slot and settings.
- Resource images are shared within one fixed device/physical format and output/working-size contract;
  model entries, not images, are cached. Physical/working size and supersampling-filter changes latch
  native-RR-only bypass until shutdown/restart. No live retirement or idle wait applies tuning.
- Shutdown checks device drain, each model release and parameter destruction results. Failed teardown
  blocks reinitialization and is not logged as successful cleanup. Dead-device abandonment is retained.
- Vulkan Frame Hold is disabled and explicitly described as unsupported. FG is not forced off.

## Evidence and limits

`VK-NATIVE` records feature mapping, generation, command buffer and bypass counts.
`VK-RR-ROUTE eligibleCount` counts routing eligibility, **not successful NR**.
`VK-NR` records requested/applied tuning, typed parameter verification, create results, slots,
and bounded model/compose markers (first three, every 300, and configuration switches).
`successfulNR` increments only after model API success and successful composition command recording.
It is not proof of GPU completion, presentation, or visible enhancement. Typed readback proves
parameter representation, not that the proprietary model consumes those controls as expected.

Multiple retained model features may increase VRAM use and first-use creation may hitch.
Their actual model/runtime compatibility must be tested; do not call this fix runtime-validated yet.
Exposure, guide interpretation, queue submission, presentation and composition algorithms are otherwise
unchanged. If the visual response or hang persists, use the evidence for the next targeted experiment.

## Offline verification

Run `tests\Run-VulkanNrTuning.cmd` from this worktree: typed float/integer roundtrips, readback faults,
all seven key fields, initial plus seven slots, ninth deferral, reselection, reset signaling, failed/invalid
creation preservation, unknown/native identity, failed/successful release and handle reuse.
Source guards check both creation hooks, native evaluation exclusion, reset wiring, versioned forwarding
and absence of explicit retirement/waits in evaluation. These are narrow source regression checks,
not an exhaustive Vulkan call-graph or driver proof. Initial-allocation failure rollback is allowed;
never-submitted resources are not live retirement.
Run `tests\Run-VulkanRrRouting.cmd` and `tests\Run-NrRobustness.cmd` for existing routing and NR controls.

Commit and build with the canonical workspace command:
`C:\OptiScaler-NR-Dev\scripts\Build-Install.cmd indiana-jones-vulkan-nr-cached-tuning -BuildOnly`.
Retain the build manifest, numeric exit, source-immutability result, exact output hashes and test logs.

## Minimal friend test / acceptance

Close the game and back up the two existing DLLs. Copy over **both** `OptiScaler.dll` and
`nvngx.dll_dlssnr.dll` from the matching build, using the same locations as the previous bundle.
Keep the existing `OptiScaler.ini` and the large NVIDIA `nvngx_dlssnr.dll` model untouched.
No installer or local deployment is part of this experiment.

1. RR on, FG off: enable NR and compare a few intensity/structure settings after releasing sliders.
   Check the applied-settings display. Leave working resolution/filter unchanged for this first run.
2. Return to a previously used configuration and confirm cached switching.
3. Enable FG and close/reopen the OptiScaler menu.
4. Return one OptiScaler log and report visual changes and any freeze.

Do not deliberately fill eight slots; offline checks cover the limit. If a pending message appears,
use an already cached configuration. Loading and DLSS-quality transitions follow only if this run succeeds.
Acceptance requires visible response to applied settings, a stable primary route with auxiliary bypass,
successful recorded NR and stable FG-on behavior without device loss or queue/fence -4.
Passing offline tests or compiling does not satisfy that gate.
