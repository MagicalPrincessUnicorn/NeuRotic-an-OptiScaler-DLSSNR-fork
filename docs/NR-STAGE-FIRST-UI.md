# Stage-first UI experiment

Parent and named control: public `alpha-0.9.5`, exact commit
`00dbd0bc5f60f84a759cfa9cd9dcc081327f96c7`.
Branch: `exp/0.9.5-nr-stage-first-ui`.
Worktree: `C:\OptiScaler-NR-Dev\worktrees\experiments\0.9.5-nr-stage-first-ui`.

Hypothesis: choosing the pipeline stage first makes the existing NR routes and their
resolution controls understandable while preserving equivalent saved rendering choices.
This is an independent sibling of FG/RR work; no diagnostic branch was inherited.

## Configuration truth table

| Stage | Method | Resolution | Existing renderer values | Basis / resampling |
|---|---|---|---|---|
| Before | Native Temporal | Automatic | Route 0, RenderingMode 1, WorkingScale 1 | Current render input |
| Before | Native Temporal | Manual | Route 0, RenderingMode 1, WorkingScale .25–2 | Render input; existing enlargement below 1 / downscaler above 1 |
| After | Native Temporal | Automatic | Route 0, RenderingMode 0, WorkingScale 1 | Final upscaled output |
| After | Native Temporal | Manual | Route 0, RenderingMode 0, WorkingScale .25–2 | Final output; existing enlargement below 1 / downscaler above 1 |
| After | Present Compatibility | Seven fixed presets | Route 1; PresentResolution / PresentCustomScale | Follow captured render dimensions, or existing output-based percentages |
| After | Present Enhanced | Seven fixed presets | Route 2; EnhancedResolution / EnhancedCustomScale | Independent policy; existing guide/admission requirements |

The seven Present choices map to FollowNative, FullOutput, and Custom indices 1–5
(77, 67, 58, 50, 33). Old Custom index 0 displays Full Output without writing the
profile. RenderingMode still wins over PerformanceMode, which wins over RunBeforeSR;
missing/malformed values retain the original 0.9.5 parsing/default/clamping behavior.
No new key can override an existing rendering value.

`UiManualResolution`, `UiManualScale`, and `UiAfterMethod` are UI memory. Missing
keys infer the old selection; non-100% WorkingScale always displays Manual even if
a conflicting UI hint says otherwise. Unknown/future method hints fall back to Native.
Invalid remembered scales fall back to 100%. The effective legacy scale is never
rewritten just by loading or viewing the menu. Stage/method/policy changes publish
under the existing configuration mutex and immutable snapshot. Save uses the same
transaction as the existing NR settings. Unrelated and unknown settings are preserved.

Native shares its existing scale/resampler between its two placements. Automatic
temporarily uses 100% and remembers the last Manual value. Present methods retain
independent presets across stage changes. Manual 100% persists as Manual when selected;
old 100% profiles infer Automatic. The ordinary 0.9.5 INIs are byte-identical.

## Status and limits

The summary shows selection plus observed dimensions, wrapping at the page boundary.
Unavailable dimensions remain explicit. New selections wait for new telemetry.
The production menu accepts an optional bounded read-only RuntimeStatus supplied by
a caller. In this independent experiment no FG/RR producer supplies one, so generic
Present status remains. No provider, real/generated frame identity, queue compatibility,
or successful Enhanced output is inferred from configuration.

The existing RR and native Vulkan paths keep NR after reconstruction regardless of
the Performance selection. This exception is visible in the main area; this UI does
not change those paths. Native final dimensions can remain unavailable when the host
does not expose an output feature. Vulkan does not expose equivalent NR work dimensions.

## Verification and acceptance

Run these existing launchers from the worktree:

```powershell
.\tests\Run-NrConfig.cmd
.\tests\Run-NrPresentResolution.cmd
.\tests\Run-Localization.cmd
.\tests\Test-NeuroticUi.ps1
.\tests\Run-NrRobustness.cmd
```

Python checks: `tests/integration_contract.py`, `tests/stage_first_contract.py`, and
`tools/localization/catalog.py verify`. Tests cover semantic combinations, per-method
memory, real INI round trips, unknown settings, concurrent publication, actual ImGui
controls, keyboard/controller focus, five languages, both themes and 0.5–2.0 scale.

Build only, using the permanent builder:

```powershell
C:\OptiScaler-NR-Dev\scripts\Build-Install.cmd 0.9.5-nr-stage-first-ui -BuildOnly -RecordIniPath C:\OptiScaler-NR-Dev\worktrees\experiments\0.9.5-nr-stage-first-ui\OptiScaler.ini
```

Result: **Inconclusive** for runtime usability/image equivalence; **keep experimental**.
No game installation, INI deployment, candidate designation, release or stable promotion.
Manual acceptance still requires comparison with the exact public control using the
same INI/game settings: each stage/method, Manual 25/67/100/125/200%, all Present presets,
dynamic resolution, fallbacks and transitions, plus real controller/menu interaction.
Do not describe these remaining runtime gates as passed based on a successful build.
