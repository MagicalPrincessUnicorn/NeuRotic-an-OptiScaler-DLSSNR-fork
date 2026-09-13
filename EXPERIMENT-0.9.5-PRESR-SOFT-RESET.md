# Alpha 0.9.5 Pre-SR Soft-Reset Experiment

## Provenance

- Branch: exp/0.9.5-presr-soft-reset
- Control: public Alpha 0.9.5 commit 00dbd0bc5f60f84a759cfa9cd9dcc081327f96c7
- Route: native D3D12 Pre-SR only
- Configuration: unchanged; the tester's existing OptiScaler.ini is the control
- Initial DS2 comparison: the same two-pass settings on both builds

## Hypothesis

When the game asserts Reset while the native upscaler session, Feature 18 session, Pre-SR
resources, and model/pass signature are still compatible, NR can reset and evaluate on that frame
without retiring the feature or scratch resources. This should remove the extended Native Temporal
fallback seen across Death Stranding 2 cuts while still discarding old-scene temporal history.

## Changed variable

Reset-only Pre-SR events are classified as soft resets. Every requested pass receives Reset on every
asserted frame, evaluates immediately, and consumes its reset latch only after a successful model
evaluation. The falling edge is normal continuation.

Structural evidence still wins: first observation, native feature release/replacement, size or format
change, quality-mode change, incompatible model/pass configuration, device transition, and NR restart
retain conservative transition handling. Missing resources and failed/skipped work retain Reset.
The Event 153 NR off-to-on model-recreation safeguard is unchanged.

No INI, default, UI, public API, Post-SR route, image-only route, native DLSS Reset value, or stable
baseline is changed.

## Automated evidence

- Pre-SR soft/hard policy and readiness test: PASS
- Reset-only source/log assertions: PASS
- Forwarder lifecycle: PASS
- GPU safety: PASS
- Multipass review: PASS
- NR robustness suite: PASS

The source assertion proves that the soft-reset branch contains no Feature 18 creation/retirement,
scratch retirement, or Pre-SR quarantine operation. Burst telemetry reports reset frames,
evaluation attempts/successes/failures, and any coincident build, rebuild, feature-retirement, or
resource-retirement count.

## Current result and decision

- Result: **Inconclusive** pending DS2 runtime evidence.
- Decision: keep experimental.
- Promotion: requires a separate explicit decision after cutscene, transition, failure, and sustained
  play acceptance checks. Any driver fault, crash, repeatable corruption, stale-scene ghost, or new
  lifecycle instability is Worse or Inconclusive and requires restoring public Alpha 0.9.5.
