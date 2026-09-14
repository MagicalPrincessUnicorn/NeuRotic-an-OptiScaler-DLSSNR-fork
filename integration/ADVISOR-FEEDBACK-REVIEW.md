# Advisor five-second feedback and experimental requirements

Exact parent/control: `d9631854ce02a7cc2b9fe95851023feb2ca7a4bc`.
Branch/worktree: `exp/0.9.6-advisor-feedback` /
`C:\OptiScaler-NR-Dev\worktrees\experiments\0.9.6-advisor-feedback`.

User reports the prior Advisor is better and requests five-second initial
feedback plus useful experimental-setting guidance for guarded FG routes. This
is qualitative user feedback, not blanket runtime acceptance of every mode.

Hypothesis: known policy requirements should be shown before testing, while a
trial without matching initial feedback should stop after five seconds and show
the actual current refusal when available. Missing feedback alone must not be
classified as an experimental requirement.

Changed variable: Advisor feedback only. Initial feedback timeout is five
seconds (formerly fifteen). The stable warmup and scored measurement windows
remain unchanged: enough fresh native frames are still required for a result.
Known Present Enhanced FG requirements are shown on the route card immediately,
using the renderer's exact FG predicate and effective saved experimental policy.
The guidance names Unlock Experimental Mode, Override FG Guardrails and Save
experimental settings, then asks the user to retry. Nothing is enabled or saved
automatically. Present Compatibility/Native do not inherit the Enhanced FG gate.

A current renderer policy refusal ends startup without waiting for the timeout.
HDR guidance is restricted to the implemented 10-bit PQ conversion; unsupported
HDR formats remain unsupported. Other current model/device/guide/refusal details
are preserved. Without a current refusal, feedback distinguishes no model output
from model output whose matching native frame timing could not be verified.
Route, trial configuration epoch and fresh attempt checks reject stale warnings.

No rendering-policy, GPU, HDR/readiness, installer, INI, global default or model
changes. Experimental warnings and all actual safety gates remain in force.
Before/After card scope, explicit route/resolution selection and exact restoration
from the parent remain inherited.

Verification: compiled five-second boundary and FG/HDR requirement matrices,
failure freshness, no blanket advice, existing sampling and input tests; full
inherited integration suite; four translated catalogs and layout checks. Exact
source reconstruction, builds and artifact hashes accompany the review package.

Runtime gate: in DD2 with FG configured, Present Enhanced should explain the
missing saved FG option immediately; enable/save explicitly and retry. Disabled
master, disabled individual option and unsaved choices must not qualify. With
options already effective, real unsupported-provider/guide/device/timing issues
must retain their own reason. Test no-feedback at five seconds, individual and
all-route runs, restoration, Before/After and existing overlay/FG/HDR controls.

Result for this binary: Inconclusive until tested. Decision: keep experimental.
Local build and matched non-ZIP package only; no game deployment or public,
stable, candidate or baseline promotion. Preserve existing live INIs/models.
