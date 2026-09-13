# NeuRotic Alpha 0.9.6 — coherent NR integration

## Identity and scope

Alpha 0.9.6 is the public-format bundle built from the coherent Neural Rendering
integration whose exact source parent is
`a9106f483e2e503c4baf75cb62e536d364b9eea3`. It supersedes the unreleased,
UI-rejected `exp/alpha-0.9.6-dlssg` preparation as the meaning of this Alpha while
preserving that branch as diagnostic history.

The milestone combines the revised NR interface and Advisor, corrected Cyberpunk
Present completion ownership, Monster Hunter Wilds frame-ledger association,
route and placement transition safety, native DirectX 11 NR transport, comparison
screenshots for supported routes, localization, and the recovery-safe public
installer/uninstaller. OptiClip and screenshot brightness manipulation are absent.

## Comparison screenshots

- DirectX 12 exposes Native Temporal, Present Image Only and Present Enhanced
  comparison capture when their route-specific resource checks pass.
- DirectX 11 exposes comparison capture on active Present Image Only and Present
  Enhanced routes. Native Temporal and NR-off comparison capture require DirectX 12.
- Native Temporal comparisons remain experimental. The original/reference image
  can appear darker than it should because display conversion is still incorrect.
  No brightness gain or hidden correction is applied.
- Capture requests remain bound to route, placement, configuration, frame identity
  and resource generation. Invalidated or incomplete requests report refusal.

## Installation and recovery

The bundle contains `NeuRotic-Setup.cmd`, `NeuRotic-Uninstall.cmd`, the exact
Release/x64 binary pair, the reviewed integration INI, dependencies and licenses.
The private NVIDIA `nvngx_dlssnr.dll` model is not redistributed. Setup preserves
existing settings and model files, verifies proxy ownership, refuses linked paths,
and retains rollback records. The uninstaller uses those records and preserves the
private model unless its separate advanced removal is explicitly chosen.

## Acceptance and limits

Offline source, configuration, localization, GPU-fixture, build, package and
disposable installation checks are required for the matched bundle. Those checks do
not establish game image quality, generated-frame presentation, every FG/RR/
Multipass combination, long-session behavior, or cross-game compatibility. Present
Enhanced runtime acceptance remains pending. Native comparison display conversion
has the known darker-reference defect above.

Classification: integration/build is judged from the retained checks; runtime stays
Inconclusive until the exact Alpha binary completes authorized game testing. This is
an experimental public-format Alpha and does not promote or repoint the frozen stable
control.

## Old diagnostic launch options

Normal Alpha use does not require a PowerShell Steam wrapper. Before ordinary game
launches, remove any Steam Launch Option that invokes a script under
`C:\OptiScaler-NR-Dev\artifacts\handoffs`. The build-machine audit found these old
wrappers on Cyberpunk 2077, Monster Hunter Wilds and Crimson Desert Enhanced;
Dragon's Dogma 2 was already clear. Unrelated options should be retained.
