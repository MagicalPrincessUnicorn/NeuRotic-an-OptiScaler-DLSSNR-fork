# Prepared-guide effects

These effects adapt DLSS5 Feeder v1.18.0-beta.1 MIT source. Keep the adjacent
`../LICENSE-DLSS5-FEEDER.txt` with distributions. No provider source is bundled.
Camera-model fitting and its resources/passes are completely removed.

## Guide host interface

File/technique: `NeuRotic_PreparedGuides.fx` / `NeuRotic_PreparedGuides`.
Compile `NRPG_MV_PROVIDER`: 0 common, 1 Launchpad, 2 VORT, 3 Lumenite Kernel,
4 Lumenite QuantMotion, 5 host-bound integrated pixel flow. Install and run an
external provider ABOVE the guide technique for modes 0..4. Mode 5 is prepared
by the x64 D3D12 host before guide rendering.
External provider output is delta UV current-to-previous. The guide converts it
to pixels. Integrated provider 5 is already pixel flow; sampling converts it to UV
once for validation, then the common output conversion restores pixels.
Lumenite's 1/8 grid can use `MV_LOWRES_FILTER`: 0 bilinear, 1 point.
Launchpad receives a next-frame request after the guide passes.

Before every guide technique, the host reads the source `NRPG_RuntimeFrame` and
updates these uniforms:

| Uniform | Obligation |
| --- | --- |
| `NRPG_HostEnabled` | Enable only within the producer's active runtime/device episode. |
| `NRPG_HostFrame` | Echo the current runtime frame; this rejects stale uniform updates. |
| `NRPG_Capture` | Exact current capture token, integer 1..16777214. |
| `NRPG_PreviousCapture` | The motion estimator's actual previous capture token. |
| `NRPG_Generation` | Episode token 1..16777214; change on resize, reload, provider/depth changes or broken delivery. |
| `NRPG_MotionReady` | Certify provider execution, current/reference association, expected descriptors and semantics. |
| `NRPG_DepthReady` | Declare the current selected ReShade depth binding usable with its configured interpretation; this is not engine/native depth provenance. |
| `NRPG_HistoryValid` | Certify the reference capture really belongs to this ordered episode. |
| `NRPG_Reset` | True on the first capture, cuts, invalid reference or broken episode; clear explicitly afterward. |

Runtime frame numbers alone never establish provider execution. The host must
derive `MotionReady` from an actual producer boundary/contract, not texture-name
presence or advancing frame counts. Missing facts remain false. Default inputs
produce zero motion, full distrust and invalid status. The private history stamp
also checks previous capture, generation and consecutive runtime frame. Rotate the
episode before the exact float32 token/frame bound; no wrapping is accepted.

Outputs are `NRPG_Motion` RG16F, `NRPG_RawDepth` R32F, `NRPG_Distrust` R8 and
`NRPG_Status` RGBA8, all full-size. Raw depth retains ReShade orientation/offset
corrections without linearization. The host separately declares reversed-depth
policy; camera matrices and near/far remain unknown. Status channels are:

- R: exact rejection bits divided by 255 (recover by rounding `R*255`).
- G: finite motion provider plus valid reference history; this is not per-pixel confidence.
- B: usable raw depth for this capture.
- A: history association usable.

Reasons: 1 stale host, 2 invalid/reset history, 4 invalid provider/depth,
8 appearance failure, 16 depth disagreement, 32 flow inconsistency,
64 accepted static hypothesis, 128 offscreen reprojection. A rejected vector can
be zeroed while G remains true: the reason and distrust must travel with it.
Zero motion does not certify stationary geometry. The depth/flow tests are
heuristics, and linearized-depth validation uses ReShade's configured policy.

Consumers must explicitly support derived guides and any distrust binding. This
effect does not grant GPU resource rights, completion, native provenance or FG.

## Optional source-guided SDR color

File: `NeuRotic_SourceGuidedColor.fx`. Compile
`NEUROTIC_SOURCE_GUIDED_COLOR=1`; the actual effect requires a D3D11-or-later shader target
and 8-bit SDR sRGB. The direct D3D12 caller supplies its host stamps. It is off at runtime by default. Run
`NeuRotic_SourceGuided_Save` before processing and
`NeuRotic_SourceGuided_Apply` after matched output returns.

Before Save, set `NRSG_HostEnabled`, echo source `NRSG_Frame` into
`NRSG_HostFrame`, set bounded `NRSG_Capture` and `NRSG_Generation`, and clear
`NRSG_ProcessedCapture`/`NRSG_ProcessedGeneration`. Before Apply, set those last
two only from successful output's actual capture/episode. A private stamp checks
Save executed in this exact frame/capture/generation; stale Save targets bypass.
Controls `NRSG_Enabled`, `NRSG_Strength`, `NRSG_Tone`, `NRSG_Detail` retain the
upstream joint bilateral Oklab algorithm. This is a selectable experiment, with
radius-four sampling cost; no HDR or performance qualification is claimed.

## Verification limits

`tests/prepared_guide_shader_test.py` checks interface/source invariants and
independent CPU math references. A ReShade effect compiler was unavailable during
this adaptation; neither shader compilation nor GPU execution is established.
Native fxc cannot compile ReShade FX source directly.

Provider 5 uses host-bound `NRPG_BUILTIN_MOTION` (full-resolution pixel flow) and
`NRPG_BUILTIN_DISTRUST` semantics from the completed x64 D3D12 software/NVIDIA
producer. Host MotionReady means exact producer completion; absence bypasses.
Source-guided color now permits the encoded SDR8 direct D3D12 caller as well as
D3D11 shader targets; its ReShade effect compilation remains unverified.
