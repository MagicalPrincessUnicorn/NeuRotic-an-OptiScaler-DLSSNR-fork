# Indiana Jones Vulkan NR integration-pool component

User-approved September 10, 2026. This is a local experimental component, not a
stable release, public push, mainline merge or HDR-quality approval.

Cleanup parent and runtime-tested control:
`fd1fbf150e5e02f0fb66d9bc1d03a8a573d4cc89`.
Branch: `exp/indiana-jones-vulkan-nr-clean`.
The cumulative component starts after `f519382d352ca9d7a348d7db2df30609bd7c94f4`;
the final cleanup commit alone is not the fix.

## Carry forward

- Primary presented RR route only; native FG/unknown and auxiliary routes never
  enter NR. Preserve context correlation, duplicate suppression and physical-contract bypass.
- Typed Vulkan creation settings, matching forwarder exports and eight retained
  model configurations with history reset on switching. No evaluation-time retirement.
- Typed void-pointer resource parameters, exact readback and wrapper/subrect checks
  before evaluating the model. Preserve native RR on rejection.
- Slider-release application, requested/applied distinction, cache-full/failure
  status and unsupported Vulkan Frame Hold restriction.

## Remove from the current runtime tree

Temporary RR/native creation/evaluation traces, raw resource/command addresses,
periodic phase/composition trace dumps and multiline applied-setting UI readouts.
Keep essential errors, failed-tuning reasons, successful NR counters and safety checks.
Historical diagnostic commits/docs remain available as evidence, not runtime UI.

## Evidence and limits

`OptiScaler (IJAnother).log` SHA256:
`70568C5AF421ADAE790F5D7460684CEAF351D4D936227187E9ED64D900E53600`.
The tested parent recorded 3,000 successful NR evaluations/composition recordings,
stable primary selection and no NR evaluation failure or device loss in this run.
Tester reported no crash and that manual paper white 40 improved appearance.
The paper-white observation is not timestamp-correlated with this log and is not
approval to change defaults. Only small 1% model-setting changes appear in the log.

Result: Better for execution in this run; image quality, cached reselection,
sustained FG-on stability and transitions remain Inconclusive. Cleanup is an
offline-reviewed/build-tested successor, not a separately game-validated binary.
Decision: keep experimental; approved for later integration with these limits.

No HDR conversion, default/INI, model DLL, D3D12 rendering or FG-policy change is
part of this cleanup. Both Vulkan RR modes remain on the post-RR control path.
The friend's existing fd1fbf15 bundle stays the current tester artifact; do not
silently replace it with this cleanup build. Continued use is provisional with
rollback on freezes, errors or unacceptable image quality.

Run the Vulkan frame-parameter, tuning and routing tests plus NR robustness, then
the canonical clean Release/x64 build-only workflow. Shared workspace pool handoff
records final source, local candidate tag, evidence locations and artifact hashes.
