# NeuRotic cross-game Present diagnostics and BG3 bridge experiment

This is one isolated source-and-package diagnostic experiment, not a release or candidate
promotion. Branch: `exp/neurotic-cross-game-bg3-bridge`. Parent/control:
`62a70c1eb4fc2a9d673c97a9a070764cd097492d`, the approved-component integration described below.

Hypothesis: the existing D3D11-to-D3D12 bridge can run Native Temporal NR by following the
same Pre-SR/upscale/restore/Post-SR order as the native DX12 path, while added Present target
telemetry identifies unsupported Wilds and GTA frames without widening compatibility.
Result: **Inconclusive** until the focused runtime matrix is completed.
Decision: **keep experimental**. No installation, live INI edit, tag, push or publication.

## Exact component provenance

| Component | Implementation | Integration scope |
|---|---|---|
| UI/updater | `2cad2876aaefed3d904af012ca96ba53319e0414` | Source parent and visual/navigation authority |
| Adaptive pool | `cdaddfac3a8d1f4bc4e11332e68c3c1c06db1a8b` | Runtime delta from `5b4d87939fd94275dc6c7f60906756c4a01fbb2d`; 48/32/256, ownership and telemetry |
| Exposure hold | `cde28f4d991fe798d96dc2584ee0515f1da20c00` | Delta from `b2ce284aefea13ce6e87e00b70700d49e1ced945`, including exact HLSL and precompiled shader pair |
| Present Image-Only/pacing | `01d5cfa2cb923cca47c896a70a5d7461c1167027` | Runtime delta from `5b4d8793`, including route `c9bcb695`, compile fix `989fe0d2`, completion latch `04706781`, diagnostics `b2ce284a` |
| Installer/configuration | `659b9801c2a339cacaddda04a842278bd8967d6f` | Exact installer, distinct INI helper and installation notes; separate local package assembly; never a source parent |
| Approved integration parent | `62a70c1eb4fc2a9d673c97a9a070764cd097492d` | Exact source parent for this diagnostic experiment |
| Cross-game diagnostics/BG3 repair | This experiment commit | Bridge sequencing and outcome telemetry, exact Present target telemetry, status UI, support label and checkbox notes |

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
- The D3D11 bridge now captures one immutable NR settings snapshot, runs Pre-SR NR before
  its D3D12 upscaler when requested, restores the output binding, then admits Post-SR NR.
  Model builds, evaluations and completed compositions are counted before D3D11 copy-back.
  The speculative direct D3D11 model probe is not called.
- Present fallback logs the actual target width, height, format, sample count, swap effect and
  color space before the existing guard returns the untouched frame. No format/API is admitted.
- Neural Rendering reports selected-but-blocked Present separately from active composition and
  reports the latest D3D11 bridge outcome. The support action is `Send Coffee` in its existing row.
- The main NR checkbox owns a session-only two-second burst tracker: click 4 and every second
  click after it display one of 25 localized notes without immediate repeats. Hotkeys and config
  changes do not call the tracker.
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

`Package-Integration.ps1` assembles a new local diagnostic folder from the canonical build manifest's
verified pair. It performs no build, installation, deletion, ZIP creation or network action.
The folder has one `NeuRotic-Setup.bat`, the distinct `NeuRotic-IniEdit.ps1`, reviewed notes,
this experiment record, the reviewed INI and explicit build dependencies. Proprietary
`nvngx_dlssnr.dll` is excluded. Installer tests use disposable workspace fixtures only.

## Focused runtime matrix (runtime execution pending)

Record game/GPU/driver/model/provider versions, exact DLL/INI hashes, resolution, mode,
route, layer count, exposure source, NR workload and FG state for every interval. Compare
the exact UI-parent control using the same test INI, and retain component logs as narrower
references. Do not compare unequal workloads as a performance regression/equivalence test.

| Game | Focused intervals and transitions | Evidence and acceptance gate |
|---|---|---|
| Crimson Desert | Use the previously working Present Image-Only scene as control. Record the new target descriptor, then run >=900 eligible samples after 32 warm-up calls; switch Present → Native → Present with FG off. | `active` is true; model and composite counters rise together; fallback streak remains zero; descriptor and pacing window are retained as the known-working control. |
| Monster Hunter Wilds | Select Present Image-Only in the reported scene and capture the first guard line plus the menu target descriptor. Repeat after one resize/display-mode transition. | Route says selected-but-blocked; width, height, format, samples, swap effect and color space are nonzero where available; model/composite/submission counters remain zero; original frames continue. Do not admit the descriptor in this experiment. |
| GTA V Enhanced | Repeat the Wilds descriptor capture at the reported resolution and after one display-mode transition. | Same fail-closed requirements as Wilds. Record the actual guard independently rather than assuming it matches Wilds. |
| Baldur's Gate 3 | Launch `bg3_dx11.exe`, select Native Temporal, enable NR, and test Performance then Quality after a restart where required. Retain the bridge status/counter lines and ordered log events. | Bridge reports config/route/resources accepted, model build or reuse, model evaluation and composition before copy-back. The live image changes with Apply Model; disabling it gives the clean upscaler control. D3D11 Present selection remains blocked and untouched. |

All games: reject unsupported Present HDR/DX11/Vulkan/DXVK/partial updates safely; preserve
the original presentation. Confirm dimensions and actual model/composite counts. Repeat
long-session and motion/occlusion comparisons before any candidate or baseline decision.

Known limitations: BG3 runtime output is not validated by offline tests; first invalid exposure sample still uses manual fallback; one-time startup
artifacts were reported in component testing. Present is conservative SDR D3D12 and uses
synthetic guides/reset each evaluation; reduced workloads remain image-quality-unvalidated.
Fence/CPU timing does not prove scanout. Wilds and GTA target formats are still unknown until
this package records them. D3D11 Present remains unsupported. Two layers roughly double model cost and inherit
tuning. Extreme rapid NR toggle bursts have historical Event 153 risk. Translations are
assistant-authored and need native-speaker/runtime visual review. Offline checks cannot
validate NVIDIA model output, cross-game image quality or actual FG delivery.
