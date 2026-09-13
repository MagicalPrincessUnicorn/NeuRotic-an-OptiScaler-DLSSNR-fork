# HDR mainline integration — experimental review build

Approved parent: `14dd0a718f34916a60dec81a897cfefc76e04e4a`.
Explicit HDR merge: `c71e367723b5a9f9d93844524c10b92a1143ce9d`, second parent
`b6a88bd3f8176ea32b28977a896f63f9774db389` (implementation `849ed5a9`).
Separate Advisor localization correction: `41345965`.

Hypothesis: accepted per-swapchain HDR observation can coexist with the viable
Present/FG correction and its successful readiness proof without losing pending
GPU ownership or allowing a certificate to cross swapchain descriptor lifetimes.
Integration itself is the experiment; both INIs and all defaults are unchanged.

Retained ancestry: `2ac97ef1` combined source/installer recovery and `b60a830e`
readiness/installer union. The complete `b7296516` contribution was imported by
`b60a830e` (matching patch identity); it is not merged twice. Earlier Cyberpunk,
Wilds, screenshots, Advisor/OptiClip, native DX11, camera-cut reset and Native
Temporal input corrections remain included. Failed observer/restart experiments
are not dependencies.

User acceptance: on 2026-09-13 the user reported `14dd0a71` viable for the overlay
issue. The linked workspace evidence records continued gameplay without a freeze
and a brief flash accepted as a residual issue. This does not establish unreported
performance, Native FPS or all-route compatibility. HDR's separate MHWilds/BG3
acceptance is inherited component evidence, not this binary's runtime acceptance.

Integration correction: readiness now includes a monotonic HDR descriptor
identity. Successful color-space changes, resize boundaries and recreated
swapchains revoke old certificates even if the final pointer/format/color match.
Failed color-space calls, repeated successful colors and metadata-only updates
retain the structural identity. Descriptor identity is checked again after model
evaluation. Revocation never signals a fence or drops unfinished completion work.
HDR observation/classification is not universal format support or calibrated color.

Validation: combined WARP/helper regressions cover pending proof across color
round trips, successful/failed resize, failed HDR calls, metadata, multiple chains,
pointer reuse, DIRECT/COMPUTE consumer recovery and retained GPU ownership.
The complete integration/localization gates run without a historical exception.
Exact results, source proof, build manifests and hashes accompany the package.

Runtime control: `14dd0a71`; HDR reference: accepted `b6a88bd3` package. Review DD2
overlays/dialogue/menu transitions with FG on/off, MHWilds HDR+FG, BG3 HDR/native
DX11 and Native Temporal/NR-Off controls. Include resize, focus return, route
changes and sustained motion. Require live gameplay, automatic NR recovery and
no new visual or device faults. Record Better/Worse/Equivalent/Inconclusive.

Current runtime result: Inconclusive (new binary not game-tested).
Decision: keep experimental. Build and local packaging only; game deployment,
candidate/baseline replacement and public/stable promotion are separate decisions.
Existing live INIs and models are not touched.
