# HDR compatibility phase 1: per-swapchain observation

## Experiment identity

- Branch: `exp/0.9.6-hdr-compat-phase1`
- Exact parent/control: `exp/0.9.6-menu-experimental-controls` at
  `2c9628abade3ccde83cfd32c058ee9408794df29`
- Hypothesis: replacing process-wide inferred HDR state with successful, per-swapchain observations
  will distinguish SDR, HDR10/PQ, scRGB, HLG, wide-gamut gamma, failed calls, resize transitions,
  metadata changes, multiple swapchains and pointer reuse without changing the parent's pixel path.
- Changed variable: swapchain color-state observation and diagnostic reporting only.
- Configuration: the parent's `OptiScaler.ini` is unchanged.
- Runtime result: **Pass**. On 2026-09-13 the user reported that the exact packaged binaries performed
  very nicely with no issues in Monster Hunter Wilds and Baldur's Gate 3. Frame Generation was also
  enabled in Monster Hunter Wilds and worked without a hitch.
- Decision: keep experimental. This is not a baseline, candidate, release or stable promotion.

## Implemented behavior

The wrapper forwards `SetColorSpace1` first and publishes a new descriptor only when DXGI accepts
the call. Failed calls retain the last successful color interpretation and expose the failed HRESULT.
Each real swapchain has independent state. Destruction removes its record so a reused pointer cannot
inherit stale HDR data.

Resize begins a transition generation before forwarding and ends it with the exact HRESULT. A
successful resize records the current format and advances the successful-resize generation. Present
uses a copied descriptor for the frame and preserves the original image while a resize transition is
active. Metadata calls retain their type, size and a hash of at most 256 bytes after success; the game
payload is forwarded unchanged. Metadata is evidence only and does not determine pixel encoding.

The existing default-off experimental HDR option and RGB10 conversion attempt are inherited without
pixel or shader changes. This phase adds no PQ decoder, gamut conversion, tone mapper, forced color
space, exposure rule, provider bypass, mode substitution or brightness correction. The diagnostic
terms `HDR10/PQ`, `scRGB`, `HLG`, `Rec.2020 gamma`, `SDR`, `other` and `unknown` remain distinct.

## Runtime test itinerary

Use one exact game version, GPU/driver, display, Windows HDR state, game HDR state, provider binaries,
private model and game settings for all runs. Record their versions or hashes before comparing output.
Start at 100% workload with Frame Generation, Ray Reconstruction and Multipass off.

1. Install the package with `NeuRotic-Setup.cmd`. Follow the installer backup flow and keep the live
   `OptiScaler.ini` unless a separate configuration test is intentional.
2. Start the game with Windows HDR and game HDR off. Leave **Override HDR Guardrails** off. Reach live
   gameplay, move the camera for two minutes, open and close the menu, then exit normally.
3. Start the same scene with Windows HDR and game HDR on. First leave the HDR override off and confirm
   the original-image fallback. Then enable Experimental Mode and the saved HDR override, restart if
   the menu requests it, and repeat the same camera path for two minutes.
4. During the HDR run, change resolution once or switch window mode once, alt-tab out and back, then
   toggle the game's HDR off and on if the title supports a live transition. Do one normal exit.
5. Run `HDR-Diagnostic-Tester.cmd` from the package and select or pass the produced `OptiScaler.log`.
   Keep its timestamped report with screenshots or capture notes for the same run.
6. Repeat step 3 with NR off as the visual control. Compare brightness, black level, highlight detail,
   saturation, gradients, HUD/text edges, motion, pause transitions and the normal exit.

Stop the matrix and retain the original game image if the report shows an unknown/other color space,
only failed color-space calls, an unfinished resize transition, a format other than the observed HDR
contract, device removal, corrupted output or a repeatable normal-exit failure. Do not extend this run
to RR, FG, Multipass, another route, another resolution or another title; each changes the hypothesis.

## Evidence and classification

### Runtime result recorded 2026-09-13

- Monster Hunter Wilds: user-reported functional, stability and visual acceptance **Pass**, including
  a successful Frame Generation run with no observed hitch or issue.
- Baldur's Gate 3: user-reported functional, stability and visual acceptance **Pass**.
- Exact binaries in both game folders matched experiment commit
  `849ed5a9a74052482b961eaa19ffb2e2c87b7e01` at evidence collection time:
  `OptiScaler.dll`/`dxgi.dll` SHA256
  `1BE1F89252F1BC9D9947DE525C76E0169AFCBA836FAB6E159A9DC3C9F3CEA571` and forwarder SHA256
  `0FFE60F7B6691222C07EDB66F53F005B7E813E47C5E5D2FB89BACDE7430D8D3B`.
- Baldur's Gate 3 diagnostic reports captured successful HDR10/PQ color-space and Present
  observations, no failed color-space calls, no device-failure lines, metadata observation, and a
  completed resize sequence in the longer run.
- The selected Monster Hunter Wilds log contained no new HDR diagnostic records and was older than
  the HDR installation. Its telemetry classification remains **Incomplete** even though the user's
  runtime acceptance result is **Pass**.
- Evidence bundle: `artifacts\0.9.6-hdr-compat-phase1\runtime-evidence\20260913-user-pass`.
- Decision remains **keep experimental**. No baseline, candidate, default, release or stable promotion
  was requested or inferred from this pass.

The diagnostic report must contain at least one successful `SetColorSpace1` observation for the tested
swapchain, a Present observation with the same color space and generation, the exact format, and a
complete resize transition if resize was exercised. Static metadata may be absent and is not a failure.

Classify image quality and stability separately as Better, Worse, Equivalent or Inconclusive against
the NR-off control. A submitted/completed experimental output is not `HDR Active` and does not validate
color accuracy. Promotion requires a later explicit decision after sustained live output and calibrated
or raw HDR comparison evidence.
