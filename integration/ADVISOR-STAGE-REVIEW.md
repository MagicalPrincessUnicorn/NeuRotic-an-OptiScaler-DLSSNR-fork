# Advisor stage-scoped review — experimental successor

Exact parent and runtime control: `e471204539234884feb57adae5077ed3538b935e`
(combined HDR, accepted overlay correction, readiness and installer recovery).
Branch/worktree: `exp/0.9.6-advisor-stage-review` /
`C:\OptiScaler-NR-Dev\worktrees\experiments\0.9.6-advisor-stage-review`.

On 2026-09-13 the user reported the combined test mostly successful, except
Advisor's Test All in DD2 with FG off; manual route selection worked. This is
partial user acceptance, not completion of every earlier runtime matrix item.
The retained DD2 log identifies e4712045 but contains no Advisor failure record;
the exact observed failure sequence cannot be reconstructed from that log.

Hypothesis: Advisor must explicitly select the same placement/resolution policy
as manual controls, observe Native cadence despite the new Native fast exit,
and separate model startup from scored frames. Integration is limited to the
Advisor UI, its reversible trial settings, timing observation and test coverage.

Changes:
- Advisor scope defaults After upscaling (three cards); Before shows Native only.
  Scope selection never changes live manual configuration or saved settings.
- One policy performs pure preflight and explicit placement for individual and
  batch tests. Native no longer inherits an unrelated manual Before/After value.
- Existing resolution preference is retained: Native After requires Full Output
  or Manual; Native Before requires Match Game Render or Manual. Unsupported
  combinations show a reason. Experimental overrides do not bypass these limits.
- All trial-modified values, including resolution preset hints, restore after
  completion, failure or cancellation. Individual and batch trials both use
  reversible single-pass settings; existing overrides are preserved.
- The Streamline Native fast exit reports timing only during Advisor trials.
  It does not admit a Present model, query buffers, claim GPU work or alter FG.
  Current known provider identity replaces stale cadence identity at trial start.
- Bounded 15-second startup and stable 30-frame warmup precede measurement;
  initialization stalls are not scored. Measurement still requires 120 fresh
  verified native frames and three seconds, with a 15-second sample deadline.
  Session/provider/resource changes and genuine scored stalls remain failures.
- New failure logging records stage, route, resolution and rendering refusal.
  New visible text is translated in all four existing non-English locales.

No INI, global rendering default, HDR policy, GPU dependency, installer or model
changes. Earlier HDR/readiness fixes remain inherited, including descriptor
identity and pending-work ownership. Existing worktrees/packages are preserved.

Verification: compiled stage/route/resolution and sampling tests; production
wiring/restoration contracts; localization layout and catalog checks; inherited
HDR, readiness, Present/FG, GPU, DX11, Native inputs and configuration suites.
Build manifests and package support files record exact results and hashes.

DD2 runtime gate: compare with e4712045, FG off first. After + Always Full Output
must attempt all three cards, then Before + Match Game Render only Native. Test
individual and batch runs, cancellation, restore, unsupported combinations,
resize/focus return and ordinary overlay/menu/dialogue transitions. Require fresh
live output/counters and no new visual/device faults; a dropdown alone is not
proof. Repeat relevant FG/HDR controls afterward. Preserve live INI and model.

Runtime result: Inconclusive until the user tests this new binary.
Decision: keep experimental; local build/package only, no game installation,
candidate/baseline replacement or public/stable promotion.
