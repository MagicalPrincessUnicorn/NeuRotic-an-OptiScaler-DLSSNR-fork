# Experimental UI, Present and Vulkan integration

Hypothesis: the unified ten-pass UI and viable Present converter fix can coexist
with the cleaned cumulative Indiana Jones Vulkan NR component while retaining
their independent routing, resource ownership and configuration behavior.

## Source identity and ancestry correction

Branch: `exp/neurotic-ui-present-vulkan-integration`.
Worktree: `C:\OptiScaler-NR-Dev\worktrees\experiments\neurotic-ui-present-vulkan-integration`.
Requested starting control: `8e1ceebff83914fea90e9f8ca907cd3d9c6d74be`.

The initial plan incorrectly claimed this control already contained the UI and
ten-pass commits. Git proves they diverge from
`d079ccd72003470662270af086de73ea442da45d`. Preparatory merge `616f8e7`
joins the requested starting control with UI flagship
`4ff2666644abb4ec848665d743924f52b64a6daf`, which contains ten-pass
`532682fff8f42530507ae1498d976f220b34067a`. Its tree is exactly the UI flagship
plus the ten files changed by the converter fix.

The final merge has that combined control as first parent and
`2cb26d3cffccd01e92b44d1c9ea90695634e4497` as second parent. The Vulkan
component is cumulative from `f519382d352ca9d7a348d7db2df30609bd7c94f4`;
its final cleanup commit alone would omit the execution fix.

## Resolution and retained behavior

- `DlssNr_Menu.cpp`: keep unified Multipass, Present and bridge UI; add concise
  Vulkan model/cache status and the native Vulkan Hold-frame restriction. Add the
  three imported messages to the existing four-language catalog.
- `DlssNr_Dx12.h`: preserve the combined control exactly, including ten-pass
  adaptive composition-pool admission. Discard the obsolete fixed-128 definition
  inherited from the Vulkan component's earlier ancestor.
- Preserve primary presented RR selection, auxiliary/native-FG exclusion,
  duplicate suppression, typed creation/resource parameters, eight retained
  configurations, history reset, checked teardown and physical-contract bypass.
- Preserve the newer Native/Present route guards in Vulkan evaluation/readiness,
  the D3D12 forwarder capability probe and Vulkan Present-unavailable reporting.
- Exclude ancestor-only `ALPHA-0.9.4.md`, README and release-packager changes.
  Both tracked INIs remain byte-identical to the starting control. One pass and
  Multipass disabled remain the source defaults from the requested UI component.

Changed variable: integration of the four explicitly selected source states.
No screenshot/capture follow-ups or separate Crimson hardening are included.
Existing candidate references, source worktrees and frozen control are retained.

## Verification and acceptance boundary

Run UI and localization contracts, ten-pass review, Present conversion, adaptive
composition pool, NR robustness (including configuration, second-layer, dispatch
and readiness), and Vulkan frame-parameter/tuning/routing entry points. Verify
all four supplied commits are ancestors and compare the Vulkan-specific files
against their component, allowing only the explicitly retained newer route gates.

Build the clean committed result with:

```powershell
& 'C:\OptiScaler-NR-Dev\scripts\Build-Install.cmd' neurotic-ui-present-vulkan-integration -BuildOnly
```

Evidence and test-run records:
`C:\OptiScaler-NR-Dev\logs\neurotic-ui-present-vulkan-integration`.
The canonical build manifest records full commit/parents, pinned dependencies,
numeric exit, source immutability, exact output paths and SHA256 of the matched
DLL pair. The workspace handoff records final test/build outcomes.

Result: runtime Inconclusive. Decision: keep experimental. This authorization
ends at build-only verification; no game deployment, candidate tagging, stable
promotion, private-model change, or INI-default change is included. NVIDIA model
behavior, image quality, sustained FG and transition evidence remain open.
