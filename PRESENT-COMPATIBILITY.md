# Present Compatibility Experiment

## Scope and provenance

- Branch: `exp/neurotic-present-compatibility`
- Exact parent/control: `606346ea4d6ed5418428ba8d0101cc9db5f57dc5`
- Hypothesis: Present Image-Only can safely admit capability-compatible 10-bit D3D12 SDR and D3D11 flip-model targets while preserving the established RGBA8 D3D12 route and untouched-original fallback.
- Changed variable: Present target admission, R10 conversion, D3D11 shared-resource copy/synchronization/copy-back, compatibility messaging, and the session-only user-toggle note source.
- Named control: the unchanged D3D12 RGBA8 SDR Present path inherited from the exact parent.
- Disposition: experimental only. This branch does not change a live INI, install a build, select a candidate, change defaults, or move a stable baseline.

## Capability contract

| API and target | Route | Required evidence |
|---|---|---|
| D3D12 `R8G8B8A8_UNORM`, SDR, single-sample flip model | Existing direct-queue copy to/from the private RGBA8 model image | `D3D12 RGBA8 direct` diagnostic; active state; increasing model and composite counters |
| D3D12 `R10G10B10A2_UNORM`, SDR, single-sample flip model | Copy to private R10, convert to RGBA8, run NR, convert to private R10, copy back | `D3D12 R10 SDR conversion` diagnostic; active state; increasing model and composite counters |
| D3D11 supported RGBA8/R10 SDR, single-sample flip model | D3D11 copy to private shared input, D3D11→D3D12 fence handoff, NR, D3D12→D3D11 handoff, private-output copy-back before original Present | `D3D11 shared RGBA8 direct` or `D3D11 shared R10 SDR conversion`; active state; increasing counters |

Admission fails before model work for HDR/non-SDR color, dirty rectangles or scroll updates, multisampling, non-flip swapchains, unknown formats, missing format/UAV support, device mismatch, unavailable shared resources or fences, and private-resource creation failure. An admission failure leaves the original target unchanged.

## Runtime evidence matrix

The package is intentionally uninstalled, so in-game results remain **Inconclusive** until the following runs are performed. A checkbox or static timer is not acceptance evidence.

| Game | Expected capability | Runtime acceptance gate |
|---|---|---|
| Monster Hunter Wilds | D3D12, 3840×2160 `R10G10B10A2_UNORM` SDR | Compatibility path reads `D3D12 R10 SDR conversion`; active message is visible; live output changes with NR; model/composite counters advance; motion and transitions remain stable; no fallback streak |
| Baldur's Gate 3 | D3D11 Present bridge | Compatibility path reads `D3D11 shared RGBA8 direct` or the exact detected R10 variant; shared handoff, model, composite, and copy-back behavior is visible through advancing counters and live output; no unchanged-frame fallback |
| Crimson Desert | Established D3D12 control | `D3D12 RGBA8 direct` remains active with live output and pacing behavior equivalent to the parent control |

For every rejected target, capture the compact API/size/format/sample/swap-effect/color-space descriptor and fallback cause, and verify that model and composite counters do not advance and the image remains unchanged.

## Test commands

From this worktree:

```cmd
tests\Run-NrRobustness.cmd
tests\Run-NrCompositionPool.cmd
tests\Run-Localization.cmd
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NeuroticUi.ps1
"C:\Users\Josh Parke\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" tests\integration_contract.py
```

Build-only command (no installation):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\OptiScaler-NR-Dev\scripts\Build-Install.ps1 -Experiment C:\OptiScaler-NR-Dev\worktrees\experiments\neurotic-present-compatibility -BuildOnly
```

Result classification remains **Inconclusive / keep experimental** until all three runtime rows have captured capability, live-output, counter, transition, and pacing evidence.
