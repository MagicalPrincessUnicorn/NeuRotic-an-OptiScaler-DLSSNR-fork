# Present Enhanced UI polish experiment

Exact parent and named comparison control: `6bc9fd6add0fbc53308e84c1636f392eed331acb`.
Branch: `exp/present-enhanced-ui-polish`.
Worktree: `C:\OptiScaler-NR-Dev\worktrees\experiments\present-enhanced-ui-polish`.
Candidate ancestor: `e7d46b899740ed25c78f11ae484366a2ba049b75`.
Inherited guide implementation: `885901ecd2ca3b2c99b813211c297a963971b8bd`.

Hypothesis: a clear enable-first layout, concise status and the Enhanced/Follow
defaults make the common compatible path easier to use without changing rendering.
The user explicitly authorized this combined UI/default-profile polish and selected
fresh defaults only for file logging. No merge, live deployment or promotion.

## Changes and defaults

Enable Neural Rendering is first, with slightly increased padding. Native Temporal
alone shows Quality/Performance and its original model-resolution controls. Both
Present routes retain independent resolution methods and hidden custom scales.
Their visible names are Present Image Only and Present Enhanced; methods are
Follow Native Render Resolution, Always Follow Output Resolution and Custom Scale.
The pass-count reminder is removed; its requirement remains in the tooltip.

Main status uses green/yellow/red with text, keeps fallback reasons and Enhanced
compatibility requirements visible, and reports current dimensions once. A UI-only
observation fence withholds old status/dimensions after selection changes until a
new Present observation. No renderer history or resource state is written by it.
Advanced Data / Diagnostics starts collapsed and retains capture, match, evaluation,
history, pacing, error and resource details. Opening it changes neither NR nor logging.
Affected strings are translated into the existing Spanish, French, German and
Portuguese catalogs. The inherited rendering-contract document is historical;
this document supersedes its route-default and UI descriptions.

Missing/automatic Route now selects Enhanced (2); missing/automatic
EnhancedResolution selects Follow Native (0). NR itself remains off. Explicit saved
values, Image Only's full-output default and PresentWorkload migration are preserved.
Configuration keys, enum values and Native defaults are unchanged. Integration and
reference INIs document all three routes and independent resolution keys.

The existing Logging > To File checkbox reads LogToFile. Both supplied profiles
explicitly set it false; the code default remains false. User-chosen true and false
values survive save/load. Existing INIs and private models are preserved by the
unchanged installer; only its explicit ReShade choice changes LoadReshade. No one-time
logging reset, automatic NR enable, settings override or installer-policy change.

## Validation and acceptance

Required offline gates: configuration defaults, explicit choices and legacy save/load;
file-logging defaults and deliberate save/load; UI visibility/order, translations,
themes/scales and observation freshness; inherited guide-copy/matching, GPU safety,
conversion, forwarder, robustness and installer fixtures. The workspace evidence
record retains exact results, strict patch verification, Release/x64 and incremental
build logs, source immutability, package file inventory and SHA256 hashes. The package
BUILD-MANIFEST.json and PACKAGE-MANIFEST.json identify the exact built commit.

The user reports 6bc9fd6a is working wonderfully. This is positive parent feedback,
not a substitute for every sustained-counter, visual and long-session gate. Offline
tests do not run the private NVIDIA model or prove in-game image quality.

Enhanced remains DX12 SDR only with FG, RR and NR Multipass off. Existing guide
matching, motion/jitter/subrect scaling, HUD inclusion, reset and lifetime behavior
are unchanged. Missing/stale/ambiguous guides preserve the original image.

Runtime gate: sustained successful Enhanced capture/match/evaluation counters AND
active output, followed by Image Only / Enhanced / Image Only A/B/A at 100%, Follow
Native, 67% and 50%. Match scene/model/tuning, allow history to settle, inspect motion,
disocclusions/HUD, and test dynamic resolution, resize, cuts, loading and route/enable
changes over long sessions. Verify clean common UI, visible failures, Native controls,
and fresh-install logging unchecked. A fallback is not a successful Enhanced sample.

Result: Inconclusive pending runtime acceptance. Decision: keep experimental.
One non-ZIP UI-polish handoff supersedes the prior UI handoff for this experiment;
candidate, stable and all older experimental source/package records are preserved.
