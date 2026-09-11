# Present Enhanced experimental compatibility unlock

Exact parent/control: `41274da7b86dd1cc1fd88908d8b149ee7fb06495`, the completed
UI-polish source. Branch: `exp/present-enhanced-experimental-unlock`. This isolated
extension preserves prior experiments, the selected candidate and frozen stable.

The user explicitly requested removal of compatibility locks, Experimental labels,
and a record for future releases. Hypothesis: existing capture/evaluation paths can
be attempted with FG, Ray Reconstruction and NR Multipass without blanket rejection.
Changed variable: admission of these combinations; actual guide/resource validity
continues to decide each frame. This is not a claim that each combination works.

## Changes

- Remove FG and NR Multipass rejection in Present and RR rejection in Present and
  Native guide capture. The existing Multipass dispatch receives the saved profiles.
- Remove the DX12 API-name rejection ahead of actual guide matching/binding. DX11
  may attempt its existing shared DX12 resource path. Present-route Multipass can
  be selected on DX11 as well. Native Temporal controls retain their restrictions.
- Label FG, RR, Multipass and DX11 Experimental in the UI and translated tooltip.
- On changes to active FG/RR/Multipass flags, invalidate Present history through the
  existing mechanism and emit a transition diagnostic through the existing logger.
  No per-frame log flood and no automatic enabling of file logging.
- Preserve unique/fresh guide capture, same-queue ordering, device/identity/subrect
  matching, GPU completion tracking and original-image fallback reasons.
- No new Vulkan Present adapter or HDR conversion is implemented. Vulkan cannot
  process Present images without that adapter. SDR resource requirements remain.

Defaults remain Enhanced / Follow Native with NR disabled. Fresh file logging stays
off; explicitly saved logging preferences survive. INI changes are comments only.
Native Temporal math, shaders, model interface and installer/restore policy are unchanged.

## Future-release validation log

Runtime result: **Inconclusive**. Decision: **keep experimental**. Positive user
feedback on the parent is preserved; it does not validate these newly admitted cases.

Retain the DX12 SDR comparison with FG/RR/Multipass disabled as a named control,
not a restriction. At 100%, Follow Native, 67% and 50%, require sustained increasing
Enhanced capture/match/evaluation counters, actual active output, and visual
Image Only / Enhanced / Image Only A/B/A. Inspect motion, disocclusions, faces,
cloth, highlights and HUD; scene guides do not describe HUD pixels.

Repeat with FG, RR and Multipass individually and in combination, recording saved
profiles, actual pass counts, output dimensions, successful evaluations and fallback
reasons. FG needs evidence for generated versus rendered frame association; RR needs
evidence that its depth/motion/reset/jitter conventions match Present; Multipass needs
per-pass history, motion and disocclusion comparisons plus sustained cost measurements.
Additional passes currently use the inherited legacy evaluation entry point, while
Enhanced's first pass uses the guided entry point with explicit subrect origins.
Nonzero guide origins in later passes are therefore a specific unresolved case;
this unlock does not change their parameter handling or claim equivalence.
DX11 needs a producer/Present device and ordered-queue match through its bridge.
Vulkan requires implementation of a Present adapter before meaningful runtime testing.

Missing, duplicate, stale or unmatched guides still leave the original image intact.
This can include generated frames or RR pipelines without a suitable guide producer.
Test long sessions, camera cuts, loading, enable/route/feature switches, resize and
dynamic resolution. Do not infer functioning Enhanced from a dropdown or no crash.

## Evidence and handoff

Source contracts, existing GPU safety, guide-copy/matching, conversion, resolution,
forwarder, configuration/robustness, localization/layout and installer fixtures are
run for the final handoff. Exact numeric results, build logs, source-immutability
proof and hashes are retained in the workspace experiment report and package build
manifest. Offline tests do not load the private model in a live game.

Package: `NeuRotic-Present-Enhanced-Unlocked-EXPERIMENT-<commit>`. Keep the whole folder
together and run `NeuRotic-Setup.cmd` with the game closed when choosing to install.
Existing settings/model are preserved under the inherited installer policy; restore
uses the backup folder's `Restore.cmd`. No live deployment or promotion is performed.
