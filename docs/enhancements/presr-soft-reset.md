# Preserve NR During Camera Cuts

Enhancement ID: `NR-PRESR-SOFT-RESET-HARDENED-02`

This revision extends exact `05d9e538d24b5c457d74687e5f723089e9eb0d90`. Public Alpha
0.9.5 `00dbd0bc5f60f84a759cfa9cd9dcc081327f96c7` remains the behavior control.
This is a source integration handoff for a separately selected 0.9.6 tree, not a 0.9.6 binary.

## Integration contract

- Configuration member: `Config::DlssNrPreSrSoftReset`
- INI key: `[DlssNr] PreSrSoftReset`
- Default: off
- UI location: `Neural Rendering > Neural Rendering experimental settings`
- Intended control type: persistent checkbox
- Title: `Preserve NR During Camera Cuts`
- Description: `Potential fix for situations where camera cuts cause the NR layer to reload, leading to a jarring presentation. This might lead to crashes when loading between worldspaces. Requires more testing.`

The option applies only to compatible reset-only native D3D12 Pre-SR frames. Structural changes
retain conservative transition handling. The selected policy is latched at the beginning of a Reset
burst, so changing the option while Reset is held takes effect on the next burst.

## Hardened behavior

- Only the native D3D12 SR passthrough explicitly enables the internal authoritative entry flag.
  DX11 bridges, replacement upscalers, RR, Present, Vulkan and Post-SR do not opt into this policy.
- CPU-only Reset observation and all-pass latching precede missing color/output, null command-list,
  tracking and resource-dependent exits. No GPU safety check is bypassed.
- Temporary preparation/publication unreadiness is not session incompatibility. Resource failures
  skip the frame without latching a structural hold; genuine structural evidence still holds the burst.
- Failed main-pass reset retention is scoped to this native Pre-SR dispatch, rather than all routes.
  The default-off policy keeps the public rising/falling-edge lifecycle and failure behavior.
  If switched off after an experimental Reset was accepted, existing reset debt must still drain
  safely on successful pass evaluations; disabling the option is not permission to forget that debt.
- New configuration/restart classification is gated. Existing Event 153 restart recreation,
  failure circuits, native-release invalidation and GPU-safe retirement remain authoritative.
- Summaries cover all observed experimental Reset frames, plus lifecycle work on the falling-edge
  frame. Structural escalation does not end accounting prematurely. Interrupted summaries explicitly
  identify partial lifecycle counts. Actual pass attempts/successes and degraded publications are
  separate from skipped frames and published frames; vendor evaluation success is not GPU completion.

## Integration and tests

Integrate the complete enhancement relative to public 0.9.5, or the hardening delta if the destination
already contains the exact first enhancement. Do not blindly apply both. Preserve the destination's
newer handle/session identity, GPU lifetime, route and restart safeguards during conflict resolution.
The extra entry argument is internal C++ plumbing, not a forwarder/export API change. The native
passthrough call opts in; every other caller defaults out. UI implementation remains deferred with
the exact title/description above and an off default. Do not ship this old-base DLL pair over 0.9.6.

`tests\Run-NrReadiness.cmd` exercises one-/multi-frame bursts, skipped-entry debt, preparation
failure/recovery, every failing pass, structural escalation, baseline-off transitions and telemetry
accounting through production value policies. Source assertions check backend wiring and observation
order separately. These are deterministic policy/fault-injection tests, not full D3D12/NGX execution.
Run the existing configuration, forwarder lifecycle, GPU-safety, multipass and robustness suites too.
Retest on the integrated 0.9.6 build; passing this source branch cannot certify a different merge.

## Evidence and disposition

A Death Stranding 2 tester reported that the reset-only camera-cut problem was fixed. No video or
log was supplied, so runtime evidence remains Inconclusive. Keep this enhancement experimental and
default-off; this commit is technology for later integration, not a candidate or release promotion.
