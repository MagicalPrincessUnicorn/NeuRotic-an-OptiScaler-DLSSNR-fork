# Crimson FG lifecycle diagnostic experiment

Parent and named control: `38b904d4df865ac676b184bff97742a74bf95fd8`.
Branch: `exp/0.9.5-crimson-fg-lifecycle`.
Hypothesis: FG teardown/recreation before NR activation exposes a lifecycle or
queue ownership failure absent when the initial FG instance is retained.
Successful control evidence: session `05527886d1064866957a4e063c248c80`, 280 NR outputs.
The failed run recreated FG before NR activation. This difference is a lead,
not proof of causation; diagnostic timing remains a confounder.

Changed variable: process-local diagnostic instrumentation. No rendering
correction, new UI setting, default, GPU wait, or resource-lifetime change.
Preserve live INI, patched NR model, driver and NVIDIA runtime. Do not update
folder DLLs to infer the OTA plugin version: retain actually loaded paths.

## Interfaces and bounds

The launcher supplies `NEUROTIC_FG_LIFECYCLE=1`, `NEUROTIC_DRED=1`, a fresh
`NEUROTIC_DIAGNOSTIC_DIRECTORY`, and the existing valid 32-hex trace session
with `NEUROTIC_FRAME_TRACE_TRIGGER=nr-enable` to the child process only.
No persistent environment/configuration changes. Ordinary launches are inert.

`FG-LIFECYCLE.log` starts at process attach independently of NR activation.
It has at most 8192 records (last record reports exhaustion), 2048 detail bytes
per record, and 128 tracked live FG handles. Unique instance IDs survive handle
reuse; failed release retains the observed instance, and late release completion
cannot erase a newer instance. Instance 0 means unknown. Snapshot instance is
the sole observed active instance, not proof of association with a swapchain.
Creation/release records describe native NGX FG; replacement FG is outside scope.

Options calls have begin IDs, returned result/accepted option values, and thread
and QPC stamps. Unchanged option details are sampled every 120 observations but
are always re-recorded after an observed instance generation change. Present and
existing GetState summaries use first-eight/every-120 sampling; active frame
traces carry per-call fence targets, generation, frame claims and copybacks.
GetState call count is unchanged. No extra counter-consuming queries or waits.
Completion fence addresses/targets are observations, not proof of completion.
Asynchronous FG evaluations without a forwarding Present scope remain unknown.
The diagnostic epoch is never passed into rendering admission/reset decisions.

DRED breadcrumbs/page faults are requested before the device hook's capability
probe and creation. Earlier devices outside our hooks remain unknown. The journal
records configuration and device interface availability explicitly. NR-owned
textures, allocators, command list and fence receive names only when opted in.
On first observed device removal, `DRED.log` retains API results, up to 64
breadcrumb nodes, 32 operations and 8 contexts per node, and 128 existing plus
128 recently freed allocation nodes. Names are capped at 96 characters. Lists
report truncation. Missing/unavailable/partial reports must never count as a
healthy GPU. DRED is approximate and can affect timing. Other faults or abrupt
process termination may prevent this hook from collecting a report.

The launcher retains session, DLL/INI/model hashes, actual loaded module paths
and their on-disk versions/hashes, driver metadata, journal, trace, DRED status,
and exit result. Module snapshots occur every five seconds, at most 360 snapshots
and 512 distinct paths; transient modules between snapshots can be missed.
Readiness requires matching session/process/clock, live trace setup, DRED setup
and device interface, an observed FG module with a hash, and driver metadata.
Capture readiness is not rendering validation.

## Acceptance and runtime handoff

Offline fixtures: create/failure/release/recreation/reused handles, unchanged
options across replacement, concurrent options/Present observations, bounded and
disabled journal, unavailable paths/DRED, WARP device removal/report retention.
Existing Pre-FG identity/native queue/startup, GPU lifetime/delayed completion,
and frame-trace checks remain required. Launcher fixtures cover readiness,
missing/stale/wrong-process/clock/gapped evidence, faults, module metadata and exit.

Build Release/x64 through canonical Build-Install, verify source immutability and
installed hashes, preserve the successful control and retain the patch/provenance.

Two user-controlled fresh launches, identical FG 2x, RR off, Multipass off,
Present Image Only at output resolution:

1. Initial FG instance: after readiness, enable NR 15 seconds, NR off, exit.
2. Only if no fault: after readiness with NR off, FG off/on twice, five seconds
   between changes; then NR on 15 seconds, NR off, exit.

Stop at the first fault. If neither fails, result is Inconclusive, with no repeat
loop and no promotion. Compare instance generations, settings/Present ordering,
creation/native queues, fence targets and DRED before proposing a separate fix.
RR integration, Wilds dropouts and BG3 DX11 guide support are excluded.

Current result: Inconclusive (runtime not performed). Decision: keep experimental.
