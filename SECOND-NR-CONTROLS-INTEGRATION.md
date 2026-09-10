# Independent second NR pass controls

## Experiment

- Hypothesis: the existing D3D12 second NR pass can own its tuning without changing
  first-pass behavior or sharing mutable state.
- Parent/control: `91d551add820eb76064ab0d0b2e946f845933c7c`
  (`exp/neurotic-present-history-stability`).
- Recovered implementation: `3a32b94963174e092702dfceeb61ad1d31f95491`
  (`exp/alpha-0.9.4-second-nr-controls`), based on second-pass control
  `50c033c5c2e2050def8dc999d9288735f8310152`.
- Changed variable: independent layer-2 configuration, UI and D3D12 consumption.
- Result: offline checks pass; runtime result is **Inconclusive** pending an
  explicitly authorized test deployment.
- Decision: keep experimental. This does not change a stable baseline, default
  pass count, candidate package, or live INI.

## Behaviour

The existing second-pass checkbox remains default-off. Its compact, collapsible
settings panel is directly below it in Neural Rendering and is disabled while the
pass is unavailable. Layer 2 persists its values under `[DlssNrLayer2]`; an older
profile without that section is seeded once from its already-loaded first-pass
values, preserving its former two-pass picture before any later edits diverge.

The layer owns working scale, resampler, enlargement mode, model preset/style and
tuning, composition controls, skin mask and model application. It has a separate
feature-creation signature, model session, work surfaces and temporal history.
First-pass-only tuning and working-scale changes do not retire layer 2. A changed
composed-frame size safely retires it after the existing GPU-completion gate.

## Diagnostics

Retained: lifecycle-only creation/release, independent build/evaluation counters,
failure reasons, and composition/work-resolution logging. They remain runtime
safety and verification signals, not exposed as developer controls. No unfinished
diagnostic UI was added. The user-visible panel contains only supported layer-2
controls and an experimental performance warning.

## Offline validation

`tests\Run-NrRobustness.cmd` passes D3D12 lifecycle/resource tests, second-layer
ordering and isolation checks, Present-history checks, and the 62-option snapshot
fixture. `tests\Test-NeuroticUi.ps1` and localization catalog verification pass.
These checks do not replace a game runtime test of disabled/enabled pass state,
first/second-pass isolation, or performance at full and reduced layer-2 scale.
