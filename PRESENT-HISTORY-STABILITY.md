# Present History-Stability Diagnostic

## Scope and provenance

- Branch: `exp/neurotic-present-history-stability`
- Parent/control: `c1c410987265f673653f3b8373160ee9980c16b3`
- Hypothesis: the white and rapid Present flicker is principally caused by sending `Reset=true`
  to Feature 18 for every processed image.  Retaining history only across completed original
  Presents should stabilize the established D3D12 and D3D11 compatibility routes.
- Changed variable: Present Image-Only Feature 18 history cadence and its compact diagnostics;
  the Present Workload control is moved below NR route selection.
- Named controls: the parent compatibility implementation and Native Temporal.
- Disposition: experimental only.  No live INI, installation, release, default, candidate, or
  stable baseline is changed.

## History contract

The first admitted frame after an enable/resume/route transition, target-signature change, or
interruption sends `Reset=true`.  A successful original Present promotes that output to history;
subsequent uninterrupted frames send `Reset=false`.  Every unsupported target, admission failure,
bridge/conversion/queue/copy-back failure, or failed original Present invalidates history and leaves
the next admitted frame reset pending.

The existing capability contract remains unchanged: D3D12 RGBA8 direct, D3D12 10-bit SDR conversion,
and D3D11 shared RGBA8/R10 routes are available only when their existing single-sample, flip-model,
SDR, format, device, private-resource, and synchronization guards pass.  Unsupported targets retain
the original image and execute no model work.

## Runtime itinerary

Keep one Present workload fixed during each sustained run, then test a deliberate workload change
separately.  In all three games, capture the compatibility path, history reset reason, uninterrupted
output count, fallback count, model/composite counters, and exact configuration hash.

| Game | Expected route | Required result |
| --- | --- | --- |
| Monster Hunter Wilds | D3D12 R10 SDR conversion | One initial reset, reset-free sustained motion, no white flicker, stable menu/load transition. |
| Baldur's Gate 3 | D3D11 shared Present bridge | Continuous handoff/copy-back, no fallback alternation, reset only after a recorded interruption. |
| Crimson Desert | Established D3D12 control | No newly visible flicker and no route regression. |

For each game test stationary detail, camera motion, scene/load transition, Present enable/disable/re-enable,
and one workload change.  Classify each outcome Better, Worse, Equivalent, or Inconclusive.  This
diagnostic package is uninstalled; a runtime deployment requires a separate decision.
