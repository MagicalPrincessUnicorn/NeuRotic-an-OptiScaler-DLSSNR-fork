# NeuRotic approved-component integration experiment

This is one combined source-and-package experiment, not a release or candidate promotion.
Branch: `exp/neurotic-approved-integration`. Parent/control:
`2cad2876aaefed3d904af012ca96ba53319e0414`, including flagship
`7040d75d6bc53e747a6448c82ddf3470e016e840`.

Hypothesis: the approved runtime and package components can coexist with the flagship
UI, preserving routing, resource ownership, fallback behavior and image quality.
Result: **Inconclusive** until the combined runtime matrix is completed.
Decision: **keep experimental**. No installation, live INI edit, tag, push or publication.

## Exact component provenance

| Component | Implementation | Integration scope |
|---|---|---|
| UI/updater | `2cad2876aaefed3d904af012ca96ba53319e0414` | Source parent and visual/navigation authority |
| Adaptive pool | `cdaddfac3a8d1f4bc4e11332e68c3c1c06db1a8b` | Runtime delta from `5b4d87939fd94275dc6c7f60906756c4a01fbb2d`; 48/32/256, ownership and telemetry |
| Exposure hold | `cde28f4d991fe798d96dc2584ee0515f1da20c00` | Delta from `b2ce284aefea13ce6e87e00b70700d49e1ced945`, including exact HLSL and precompiled shader pair |
| Present Image-Only/pacing | `01d5cfa2cb923cca47c896a70a5d7461c1167027` | Runtime delta from `5b4d8793`, including route `c9bcb695`, compile fix `989fe0d2`, completion latch `04706781`, diagnostics `b2ce284a` |
| Installer/configuration | `659b9801c2a339cacaddda04a842278bd8967d6f` | Exact installer, distinct INI helper and installation notes; separate local package assembly; never a source parent |

Exposure record `b5c78a29de586f5e532f8e864556e46c22268ce5` and Present closeout
`b5391d0bd2828f0a4d73e724b0d469487bedb168` are evidence, not implementation imports.
Historical public-release README, release notes and packaging builder are not imported.
The reviewed installer wording still identifies Alpha 0.9.4; this document identifies
the new artifact as an unvalidated integration experiment, not a new Alpha release.

## Source conflict resolutions

- Config load/save/snapshot and telemetry retain both route/pacing and second-layer fields.
- Native Temporal, Present Image-Only and the existing second-layer control stay on Neural
  Rendering. Updates stay in General; all other flagship pages and controls are retained.
- The second layer has separate Feature 18 lifecycle, history and resources. Its model and
  composition settings remain inherited from layer 1. Enable is D3D12-only, default off.
- Adaptive admission retains eight slots for one layer and reserves twelve for the flagship
  two-layer path. Both Pre-SR preflight and Post-SR/Present dispatch use this bound. Pool
  capacity, transactional allocation, completion and replay rules are unchanged.
- Route-domain changes retire the flagship's actual layer-2 feature instead of the older
  removed pass-feature array. Layer 1 also waits for its recorded retirement before reuse.
- Present's synthetic guides, per-evaluation reset, original Present count, format gates,
  private queue submission order and terminal untrackable-completion latch are preserved.
- The updater's misleading automatic-installation promise is replaced by manual release-page
  guidance. Repository/release-note links, one-shot check and terminal failure remain.
- New strings and dynamic fallback reasons have English/Spanish/French/German/Portuguese
  catalogs. Lookup normalizes source whitespace while retaining translated explicit newlines.
  Layout checks cover every supported scale from 0.5 to 2.0 in 0.1 steps.

## Separate configuration/package delta

`integration/OptiScaler.ini` is the exact reviewed packaged INI, SHA256
`CD3D9E9908A3A61B4CDADFE2689E9444511DDADEB885CE12988C3D944A8D044D`.
Relative to the reviewed Alpha package input `F584726C40DA026A557F24E971F01C574DA2C4D51652E789F56BFB164C03D02E`,
only `[DLSS] RenderPresetPerformance` changes 11 to 12; Ultra Performance stays 12.
`[DLSSD]` presets stay `auto`. This package profile does not alter source defaults or live INIs.
The root source INI separately documents the imported Route/PresentWorkload keys at their
existing native/full defaults. The package omits those optional keys, resolving to the same defaults.

`Package-Integration.ps1` assembles a new local folder from the canonical build manifest's
verified pair. It performs no build, installation, deletion, ZIP creation or network action.
The folder has one `NeuRotic-Setup.bat`, the distinct `NeuRotic-IniEdit.ps1`, reviewed notes,
this experiment record, the reviewed INI and explicit build dependencies. Proprietary
`nvngx_dlssnr.dll` is excluded. Installer tests use disposable workspace fixtures only.

## Focused runtime matrix (not executed)

Record game/GPU/driver/model/provider versions, exact DLL/INI hashes, resolution, mode,
route, layer count, exposure source, NR workload and FG state for every interval. Compare
the exact UI-parent control using the same test INI, and retain component logs as narrower
references. Do not compare unequal workloads as a performance regression/equivalence test.

| Game | Focused intervals and transitions | Evidence and acceptance gate |
|---|---|---|
| Crimson Desert | Native Temporal at fixed SDR/HDR scene: cold enable, dark-to-bright and flashbang; RR/SR/DLAA and menu/loading transitions; Present full workload in SDR, FG off, Present → Native → Present, >=900 eligible samples per interval after 32 warm-up calls; repeat with layer 2 off/on using paced toggles | Inspect first invalid exposure sample separately from later holds; no persistent green/static field, unintended manual fallback after a valid sample, readiness alternation or new image defects; route-separated p50/p95/max CPU/GPU/interval data; actual delivery requires external presentation evidence |
| Monster Hunter Wilds | Performance and Ultra Performance Preset L, other modes unchanged; Native Temporal, RR and loading/cutscene/resize transitions; exposure hold; layer 2 off/on and route changes; all five UI languages, 0.5/1.0/1.5/2.0 scales, saved language restart and dragged window | Preset routing and native dimensions confirmed; separate layer build/retire/reset counters advance; no stale layer output; startup artifact recorded; General update success/failure/disabled states and links, NR tooltips, scrolling and persistence visually checked |
| GTA V Enhanced | Reproduce approved pool-demand scene on Native Temporal, layer 2 off then on; FG off then actual supported FG on; paced NR/route transitions; long session and resize; supported SDR Present interval | Growth can exceed 128 without recycling replayable owners; cap/allocation/tracking failure bypass stays safe; no flicker; distinguish reset/readiness recoveries from pool rejection; measure actual FG presentations, not selector state |

All games: reject unsupported Present HDR/DX11/Vulkan/DXVK/partial updates safely; preserve
the original presentation. Confirm dimensions and actual model/composite counts. Repeat
long-session and motion/occlusion comparisons before any candidate or baseline decision.

Known limitations: first invalid exposure sample still uses manual fallback; one-time startup
artifacts were reported in component testing. Present is conservative SDR D3D12 and uses
synthetic guides/reset each evaluation; reduced workloads remain image-quality-unvalidated.
Fence/CPU timing does not prove scanout. Two layers roughly double model cost and inherit
tuning. Extreme rapid NR toggle bursts have historical Event 153 risk. Translations are
assistant-authored and need native-speaker/runtime visual review. Offline checks cannot
validate NVIDIA model output, cross-game image quality or actual FG delivery.
