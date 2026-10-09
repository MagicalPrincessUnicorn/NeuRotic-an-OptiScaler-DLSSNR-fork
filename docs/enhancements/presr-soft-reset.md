# Pre-SR reset policy contract

The retained reset-policy test consumes the following configuration and interface strings.

- Configuration member: Config::DlssNrPreSrSoftReset
- INI key: [DlssNr] PreSrSoftReset
- Default: off
- UI location: `Neural Rendering > Neural Rendering - Experimental Overrides`
- Title: Preserve NR During Camera Cuts
- Description: Potential fix for situations where camera cuts cause the NR layer to reload, leading to a jarring presentation. This might lead to crashes when loading between worldspaces. Requires more testing.

Only compatible reset-only native D3D12 Pre-SR frames enter this policy. Structural changes retain conservative handling. The policy is latched for a reset burst, so changing the option does not abandon accepted work. Reset observation precedes early resource exits; every pass receives reset handling. Feature replacement ends the burst, native output parameters are restored, and GPU ownership rules remain enforced.
