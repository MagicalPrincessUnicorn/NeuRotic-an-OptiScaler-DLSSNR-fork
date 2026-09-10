# Multipass review fixes

Experiment: `exp/neurotic-multipass-review-fixes`.
Exact parent/control: `fb618f989ff17960f0fc692f64f346f1375cd4fd`.
Authorization: September 10 request to fork and fix all three comparative-review concerns,
then supply an installer bundle for testing. The three related corrections are one expressly
requested source test. Existing two-layer composition and independent histories are retained.

## Changes

- With two layers active, both compose normal images. Debug display inspects the last successfully
  composed layer after both evaluations. Wipe/side-by-side display then runs once, against the
  original first-layer HDR copy. Its copy reuses an existing proxy only after model/capture reads.
  With no comparison/debug requested, no extra presentation work runs. Single-layer display is
  unchanged. The DXBC shader/header were regenerated with Windows SDK 10.0.26100.0 FXC, cs_5_0/O3.
- Both D3D12 layers and the first-layer proxy provider scale motion X/Y independently from actual
  rounded work dimensions. Jitter, guides and native SR dimensions are otherwise unchanged.
- Optional scratch preparation failure releases unpublished allocations, marks layer 2 unavailable,
  and commits/runs layer 1. There is no automatic per-frame retry. Failed layer retirement may remain
  pending while layer 1 runs; actual vendor release still uses a lifecycle-only invocation. Toggle
  layer 2 off/on to explicitly retry after its normal completion gate.

## Offline evidence

- Existing second-layer source gate plus late-display/fallback checks pass.
- Shipped DXBC executes on D3D11 WARP: 54 comparison cases spanning off/wipe/side-by-side,
  swap, zoom and split values, gradient spatial mapping, HDR values and alpha.
- Production D3D12 scratch helpers on WARP pass all five optional allocation failure positions,
  both matching and resized first-layer bundles, plus existing rollback/state-epilogue tests.
- Motion checks verify a vertical traversal maps to 536 working pixels for 1920x1080 -> 960x536,
  exact native identity and defensive zero-dimension behavior.

The 12-slot two-layer pool reservation remains unchanged: the maximum normal encode/downsample/
resolve pair uses six slots; meter/calibration and Pre-SR re-jitter plus two late display dispatches
remain within that reservation. Presentation introduces no additional scratch allocation.

## Test gate

Use the update bundle with an existing working installation. It preserves the live INI and
`nvngx_dlssnr.dll` byte-for-byte, backs up the replaced main/forwarder pair, verifies installed
hashes and records provenance. It does not copy a packaged INI or enable Multipass automatically.

Start with the same settings as the control. Enable layer 2; test 100% and 50% work resolution,
vertical camera pans, shadows/foliage and both layers' controls. Check wipe/side-by-side at different
zoom and swap settings, then return to normal output and inspect for layout artifacts or lasting
history disturbance. With both layers active, debug views refer to the last completed layer.
Also test NR/layer toggles, resize/SR-mode changes, loading and FG transitions. Allocation fallback
is tested offline by deliberate failure injection; do not exhaust game VRAM deliberately.

Result: **Inconclusive** until the user's game test. Decision: **keep experimental**.
No baseline, release, default configuration or existing experiment is superseded.
