# Present FG/RR: Phase A observation checkpoint

## September 12: defer trace until user enables NR

The initial Steam capture verified session propagation but exhausted its 65536
event budget during startup. Set `NEUROTIC_FRAME_TRACE_TRIGGER=nr-enable` alongside
the session ID to defer budgeted observations until a user off-to-on NR toggle.
The existing enable setter publishes its unchanged state before notifying the
diagnostic gate. Config loading does not trigger capture. If NR is already enabled,
turn it off and then on after loading gameplay. The mode-selection and subsequent
disable transitions remain inside the same one-shot capture; toggles never reset
or enlarge the budget. Missing trigger preserves immediate tracing; an invalid
nonempty trigger refuses capture. Without a valid session all tracing stays off.

One unbudgeted `NR_FRAME_TRACE_CONTROL` record announces waiting-for-nr-enable,
allowing the Steam launcher to verify setup without claiming active capture.
`trace-started` marks the winning trigger; concurrent render observations may
receive an earlier event ID, so do not infer ordering from that marker alone.
Startup attempts do not consume event IDs or evaluate macro arguments. This is
a diagnostics-only continuation from `634edbe594ae602bd422a0bb8d6e2151704fdc48`;
resource ownership, queue synchronization, presets, INI, NR history and rendering
behavior are unchanged. Full compatibility and the silent shutdown fault remain
unresolved. The historical initial checkpoint below predates deployment.

This is an incomplete implementation of the enhancement story, held at its mandatory
runtime gate. It adds observations only. No real-frame packet, generated-frame
classifier, provider ordering adapter, exposure handoff, or compatibility claim is
enabled. The unmodified public 0.9.5 behavior remains experimental for FG/RR.

- Parent/control: `00dbd0bc5f60f84a759cfa9cd9dcc081327f96c7` (dereferenced public tag).
- Branch/worktree: `exp/0.9.5-present-fg-rr-handoff`,
  `C:\OptiScaler-NR-Dev\worktrees\experiments\0.9.5-present-fg-rr-handoff`.
- Hypothesis: correlated native evaluation, private guide recording, queue submission,
  NR completion and provider resource observations can locate the actual DLSSG
  capture boundary before any ownership change is implemented.
- Changed variable: opt-in observation; no shader, admission, resource state,
  INI/default, menu, provider scheduling, or Present-count change.
- Initial game selected by user: Dragon's Dogma 2, D3D12 SDR, ordinary DLSSG 2x.
- Runtime result: **Inconclusive; keep experimental**. Phases B/C/D are pending.
- Other UI/overlay experiments are not ancestors and were not cherry-picked.

## Trace contract

Set `NEUROTIC_FRAME_TRACE_SESSION` to a fresh 32-character lowercase hexadecimal
session ID **in the environment inherited by the game before launch**. An absent or
invalid value disables tracing. Existing file logging must already be enabled at
Info or more detailed; this experiment never changes logging settings. Missing log
records are missing evidence. No file-logging fallback or automatic upload is added.

Every `NR_FRAME_TRACE` line contains version, session, process/thread IDs, QPC ticks
and frequency, a bounded unique event sequence, thread-local nested native/Present
observation IDs and explicit `classification=unknown`. These observation IDs must
never be promoted to authoritative real-frame tokens. Address reuse is possible:
join addresses only within observed command-list/ticket lifetimes and resets.

The cap is 65,536 records per process; the last is `trace-ended`. Restart for a new
capture. Capture is intentionally a short diagnostic window, not long-session proof.
Logging can perturb CPU timing; compare unarmed and armed instrumented runs.
No locks/resources/worker threads are added by the observer beyond the existing
logger, atomic event budget, and one-time environment initialization. Normal unarmed
calls do not format strings or evaluate macro arguments.

Observed seams: NGX native/replacement SR/RR and FG calls; native guide inputs and
capture return; existing GPU-safety ticket recording/submission/reset; Present
selection/backbuffer/fallback/model/copyback/fence/completion polling; wrapped and
FG Present calls; game Streamline tags; OptiScaler DLSSG constants, optional source
copies, resource tags, and provider Present. Null/unknown fields stay unknown.
Queue execution observation exists only after the existing GPU safety hooks have
been installed; it does not install hooks or create GPU tickets for extra coverage.

`nr-model-submitted` means commands were submitted. `nr-fence-signaled` means the
queue accepted a signal. Only a compatible completion poll proves that fence value
completed; none of these proves the FG consumer accepted that dependency. A DLSSG
resource tag can cause an earlier private copy; its return does not prove capture.
Legacy Streamline tags and proprietary internal capture/generated paths are gaps.
All provider support below remains unproven.

## Provider capability table

| Provider | Adapter revision | API/ratio | Source/queue contract | Classifier | Title/evidence | Result/fallback |
|---|---|---|---|---|---|---|
| Native DLSSG / NGX | observation-v1; no ordering adapter | D3D12 / 2x | observe NGX backbuffer/HUDless/list; capture queue unresolved | unavailable | DD2 / pending | Inconclusive; inherited 0.9.5 behavior |
| DLSSG / Streamline | observation-v1; no ordering adapter | D3D12 / 2x | observe tags, constants, copies and provider queue; internal capture unresolved | unavailable | DD2 / pending | Inconclusive; inherited 0.9.5 behavior |
| FSR FG | none | untested | undeclared | unavailable | none | Experimental, not certified |
| XeSS FG | none | untested | undeclared | unavailable | none | Experimental, not certified |
| Higher ratios | none | untested | undeclared | unavailable | none | Experimental, not certified |

HUD policy is the inherited final-backbuffer policy: game HUD pixels included,
overlay still follows NR at the existing call site. No HUDless policy is invented.

## Evidence collection and next gate

Use the existing, separate test-only harness at
`C:\OptiScaler-NR-Dev\worktrees\experiments\0.9.5-diagnostic-test-harness\tools\diagnostics\Run-NeuRotic-Diagnostic-Test.cmd`.
Retain its entire evidence directory (manifest, QPC phase notes, exact INI/source,
GPU/driver/settings, PresentMon version/hash/command/raw CSV or unavailable reason).
The harness's offline certification is not a passed DD2 rendering control.

First certify it in DD2 on the exact public binary with FG/RR/Multipass off, then
compare the instrumented binary with identical INI/game settings, unarmed and armed.
Inspect active output and actual presentation cadence. Test both Present routes.
Only after that no-regression check, capture ordinary DLSSG 2x with RR/Multipass off.
Keep INI disposition **undecided until test deployment is selected**; these are build
artifacts, not an installation. No game files have been replaced by this checkpoint.

Example arming in the same PowerShell session used to launch the selected game
(existing launchers such as Steam may not forward this environment; verify the log):

```powershell
$env:NEUROTIC_FRAME_TRACE_SESSION = [Guid]::NewGuid().ToString('N')
$env:NEUROTIC_FRAME_TRACE_SESSION
```

After collection stops, correlate by the exact session ID using the committed reader:

```powershell
& 'C:\OptiScaler-NR-Dev\worktrees\experiments\0.9.5-present-fg-rr-handoff\tools\diagnostics\Read-FrameTrace.ps1' `
  -LogPath 'C:\path\to\retained\OptiScaler.log' `
  -HarnessManifest 'C:\path\to\harness\manifest.json' `
  -SessionId $env:NEUROTIC_FRAME_TRACE_SESSION `
  -OutputDirectory 'C:\OptiScaler-NR-Dev\logs\dd2-frame-trace-review'
```

The reader retains raw log and harness manifest hashes, creates `events.csv`, rejects
duplicate IDs/mixed processes/mismatched clocks, flags missing records and QPC
non-overlap, and always leaves runtime Inconclusive. It never fabricates a
one-to-one PresentMon join. Preserve the original harness directory too.

Build command (prepared committed source only):

```powershell
& 'C:\OptiScaler-NR-Dev\scripts\Build-Install.cmd' 0.9.5-present-fg-rr-handoff -BuildOnly
```

Tests: run `tests\Run-NrFrameTrace.cmd`, `tests\Run-NrPresentGuides.cmd`, and
`tests\Run-NrGpuSafety.cmd` from this worktree. Source/patch/build evidence is retained
under the workspace logs and patches directories.

The Phase A gate requires a reviewable observed capture/source/queue graph and
classification evidence. If proprietary DLSSG does not expose enough identity or
ordering, explicitly decide a narrower adapter/unsupported policy before Phase C.
Phase B validates RR alone. Phase C validates FG alone. Phase D must combine only
independently accepted states; a compile is never permission to skip these gates.

## Primary contract references

The [NVIDIA DLSSG guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)
describes per-frame resource tagging and frame-index alignment. The
[manual-hooking guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md)
is the reference for proxy boundaries. These guide observation; they do not prove
DD2's proprietary capture or generated-frame identity. No vendor code was copied.
