# NeuRotic Alpha 0.4

Alpha 0.4 is a public NeuRotic/OptiScaler development fork focused on making DLSS Neural Rendering easier to measure and safer to use during normal game transitions.

## What this fork adds

- **Validated jitter-aware Pre-SR Neural Rendering.** Neural Rendering can run on the game's native, jittered DLSS input before the game's own final DLSS upscale. The original temporal input and mode-dependent render sizes are preserved.
- **Per-mode DLSS preset routing.** Performance and Ultra Performance can use their own explicit DLSS presets, while `USE GLOBAL`, `NVIDIA DEFAULT`, and the global preset remain distinct. This makes preset testing reproducible and avoids silently replacing explicit per-mode choices.
- **NR telemetry.** The in-game performance information reports total NR time, model time, composition/copy time, input dimensions, output dimensions, mode, frame and reset state, feature builds/rebuilds, and evaluation failures. Structured logging is included for troubleshooting and performance comparisons.
- **Readiness and transition handling.** NR waits for valid resources and stable inputs, quarantines NR during held resets, and clears NR history when the Pre-SR path changes mode, dimensions, format, or lifecycle state. This replaces timing-only assumptions with explicit safety checks.
- **NR Performance Mode.** The Neural Rendering menu exposes a Performance Mode option. `false` preserves the established Post-SR path; `true` requests the tested Pre-SR path. The default `auto` value leaves the existing configuration behavior unchanged.
- **Clearer keybind guidance.** The Neural Rendering menu identifies the key used to toggle Neural Rendering, making the control easier to discover.

## Bug fixes and stability changes

This Alpha includes fixes and hardening, not just new controls:

- corrected per-mode DLSS preset selection and inheritance;
- replaced the fixed startup frame guard with resource/readiness checks;
- prevented NR work while a reset is being held;
- reset NR history across Pre-SR transitions;
- guarded exposure and upscaler lifecycle access during initialization and transitions.

These changes were developed as an experimental Alpha. They are not a replacement for the upstream OptiScaler project, and runtime behavior can vary by game, GPU, driver, API, and DLSS model files.

## Simple installation

1. Install **OptiScaler** using its normal installation instructions.
2. Open this repository's **`alpha-0.4` branch** and download the compiled Alpha 0.4 files when they are provided with the branch/release.
3. Back up the existing OptiScaler files, then overwrite the matching files in the OptiScaler/game installation directory.
4. Start the game and open the OptiScaler overlay to review the Neural Rendering and telemetry settings.

There are no Dagger or Bows prerequisites. Those names were included in an earlier documentation mistake and are not part of this fork's installation requirements.

If you download the repository source rather than a compiled package, it must be built first; the source tree itself is not a drop-in binary installation.

The branch's `OptiScaler.ini` contains Alpha test defaults, including explicit DLSS preset overrides and `PerformanceMode=auto`. Preserve your current INI if you do not want those settings changed, or copy the relevant `[DlssNr]` and preset settings manually.

## Alpha 0.4 identity

- Branch: `alpha-0.4`
- Source commit: `d6a14cd78d6fc6d1b25a8eca62784847ab9b2e2f`
- Milestone: NR telemetry
- Status: experimental public fork
