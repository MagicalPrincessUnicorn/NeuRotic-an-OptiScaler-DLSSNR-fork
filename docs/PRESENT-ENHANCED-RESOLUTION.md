# Present Enhanced resolution experiment

Exact parent/control: `e7d46b899740ed25c78f11ae484366a2ba049b75`.
Branch: `exp/present-enhanced-resolution`.
Worktree: `C:\OptiScaler-NR-Dev\worktrees\experiments\present-enhanced-resolution`.
Guide source: `885901ecd2ca3b2c99b813211c297a963971b8bd`, ported mechanically from
its common candidate ancestor `145ebdab` without merging or rewriting history.
The first 100%-only checkpoint is `90895cf8`. Its guide tests pass on WARP.

Hypothesis: captured Native temporal guides can improve final Present NR while
independent working-resolution policies reduce cost without changing Native NR.
The user explicitly authorized this integration over the candidate, including its
existing installer work. Candidate/stable references and live files remain unchanged.

Routes: Native Temporal (0), Present Image-Only (1), Present Enhanced (2).
Native Quality/Performance, original model-resolution options, multipass profiles,
installer/restore behavior and the shipped candidate INI are preserved.

Resolution keys under `[DlssNr]` are `PresentResolution`, `PresentCustomScale`,
`EnhancedResolution`, `EnhancedCustomScale`. Policies are 0 Follow native, 1 full
output, 2 custom. Custom indices 0..5 mean 100, 77, 67, 58, 50, 33 percent.
Each route retains its hidden custom choice when changing policies/routes.
Existing PresentWorkload 0 migrates to full output; 1..5 migrate to the equivalent
custom percentage. Explicit new keys win. Save removes the obsolete key. Missing
keys retain the prior full-output behavior; no game INI is edited by this task.

Full output and Custom 100% use exact output dimensions. Other custom sizes use
the existing nearest-eight calculation, bounded by output. Follow native uses
fresh game render-subrect width/height, aligning private NR dimensions down to eight;
it never uses a DLSS preset name or stale allocation dimensions. Missing metadata
means visible original-image fallback. Image-Only Follow uses metadata without
copying depth/motion; its constant-guide model behavior remains Image-Only.

Enhanced copies Native base depth/motion planes, including D32S8 depth without
touching independently stored stencil. Eight slots / 512 MiB remain bounded by
producer/consumer completion AND recording sealing. Match requires one Native
evaluation per Present interval, matching swapchain identity/backbuffer/output
dimensions, and one observed submission on the Present queue. No cross-queue wait
is inferred. Guide origins and bounds are preserved; a new guided forwarder export
sets distinct depth/motion origins. The legacy Native export still writes zeros.
Game reset, depth direction, motion scales, jitter and exact guide subrect dimensions
travel with the copies. Native exposure never applies to tone-mapped Present color.

100% retains the proven temporal convention. For reduced work, motion scales once
inside Dispatch by actual NR/output dimensions; jitter uses that same per-axis
ratio once before Dispatch. Guide dimensions/origins are not scaled or inferred.
Route/policy changes invalidate selection generations and history. Effective sizes
rebuild through the existing completed-Present resources and NR scratch/feature
retirement machinery; in-flight work causes fallback until it is safe. Native guide
size/origin/convention changes and game resets also invalidate history.

Enhanced is limited to DX12 SDR, FG off, RR off, NR Multipass off. No DX11, Vulkan,
HDR, RR or FG support is claimed. Partial output subrects and unsupported resource
formats fail closed. Captured scene guides do not describe HUD pixels: the game HUD
is retained in the final image, but unchanged HUD pixels are not guaranteed.
Same-queue interval matching is observable evidence, not an engine-provided frame ID.

Offline tests: production configuration migration/save/load using SimpleIni,
independent policies and dimensions; guide copy/matching/bounds/metadata tests;
forwarder parameter capture and legacy-interface regression; GPU safety, conversion,
robustness/configuration/lifetime/readiness, UI/localization. Complete build/package
results and hashes are recorded in the workspace project evidence after execution.
No offline suite evaluates the private NVIDIA model or proves game image quality.

Runtime acceptance: for 100%, Follow native, 67%, and 50%, require sustained successful
Enhanced capture/match/evaluation counters AND active placement/output; then visual
Image-Only / Enhanced / Image-Only A/B/A with equal resolution, model and tuning.
Allow history to settle. Assess faces/cloth/highlights, pans, moving objects,
disocclusions and HUD. Exercise cuts, menus/loading, enable/route changes, dynamic
resolution and output resize; confirm safe fallback and successful recovery. Retain
long-session logs, actual dimensions, settings and screenshots/video. Fallback is
not a successful B sample. Result: Inconclusive; decision: keep experimental.
