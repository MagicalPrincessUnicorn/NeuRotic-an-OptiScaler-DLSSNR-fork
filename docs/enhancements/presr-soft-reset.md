# Preserve NR During Camera Cuts

## Integration contract

- Configuration member: `Config::DlssNrPreSrSoftReset`
- INI key: `[DlssNr] PreSrSoftReset`
- Default: off
- Intended UI location: `Advanced > Experimental`
- Intended control type: persistent checkbox
- Title: `Preserve NR During Camera Cuts`
- Description: `Potential fix for situations where camera cuts cause the NR layer to reload, leading to a jarring presentation. This might lead to crashes when loading between worldspaces. Requires more testing.`

The option applies only to compatible reset-only native D3D12 Pre-SR frames. Structural changes
retain conservative transition handling. The selected policy is latched at the beginning of a Reset
burst, so changing the option while Reset is held takes effect on the next burst.

## Evidence and disposition

A Death Stranding 2 tester reported that the reset-only camera-cut problem was fixed. No video or
log was supplied, so runtime evidence remains Inconclusive. Keep this enhancement experimental and
default-off; this commit is technology for later integration, not a candidate or release promotion.
