<div align="center">

# NeuRotic

### Practical DLSS Neural Rendering for OptiScaler

A community fork focused on making NVIDIA DLSS Neural Rendering usable, configurable, measurable, and safer during real gameplay.

[Download Alpha 0.9.4](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/tag/alpha-0.9.4) · [Read the full release notes](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/blob/alpha-0.9.4/ALPHA-0.9.4.md) · [Support development on Ko-fi](https://ko-fi.com/espiownage)

</div>

> [!IMPORTANT]
> Download the named release ZIP—not GitHub's automatic **Source code** archives. Source archives are for developers and are not drop-in game installations.

## What is NeuRotic?

NeuRotic is an experimental fork of [OptiScaler](https://github.com/optiscaler/OptiScaler) and [Dagherbou's OptiScaler DLSS-NR work](https://github.com/Dagherbou/OptiScaler_DLSSNR). Its purpose is to integrate NVIDIA's DLSS Neural Rendering model into OptiScaler while preserving the game's native temporal inputs and making difficult rendering transitions less fragile.

The project is independent and is not affiliated with NVIDIA, the OptiScaler maintainers, or any game developer. It is intended for experimentation in single-player games.

## Alpha 0.9.4 highlights

- **Performance Mode by default.** Runs NR before native DLSS Super Resolution so the model processes fewer pixels.
- **Quality Mode when preferred.** Keeps NR after native Super Resolution for the established full-resolution path.
- **Ray Reconstruction-aware routing.** Games using native DLSS Ray Reconstruction retain native RR ownership; NeuRotic runs NR afterward instead of forcing an incompatible pre-SR route.
- **Substantial NR lifecycle hardening.** Feature creation, restart, replacement, shutdown, device changes, and partial initialization now have explicit ownership and fail-closed behavior.
- **GPU-completion-based safety.** Command-list submissions and fences protect descriptors, upload buffers, readbacks, capture surfaces, scalers, and retired model sessions from premature CPU reuse or destruction.
- **Synchronized state and configuration.** Each host upscale uses one immutable NR settings snapshot, preventing mixed old/new placement or tuning during live changes.
- **Transactional allocation.** Multi-resource replacements publish only after every required allocation succeeds; failure keeps the last valid state intact.
- **Safer capture and exposure scanning.** Resize, format, cancellation, bounds, readiness, and concurrent reporting paths have been hardened.
- **Correct DLSS preset and upscaler override behavior.** Explicit per-mode choices remain distinct from global and NVIDIA-default selection.
- **A much better overlay.** Reorganized controls, readable two-column sizing, nested text wrapping, upper-right anchoring, and downward expansion without first-open horizontal runaway.

### Rendering modes

| Mode | NR placement | Best for |
|---|---|---|
| **Performance (default)** | Before native DLSS Super Resolution | Lower NR cost and practical gameplay |
| **Quality** | After native DLSS Super Resolution | Maximum-resolution NR processing |
| **Native Ray Reconstruction active** | After native DLSS RR | Preserving the game's RR inputs and reconstruction ownership |

> [!NOTE]
> Neural Rendering itself remains opt-in. “Performance by default” refers to the selected NR route once NR is enabled.

## Installation

1. Download **`OptiScaler-DLSSNR-alpha-0.9.4.zip`** from the [Alpha 0.9.4 release](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/tag/alpha-0.9.4).
2. Fully close the game and back up any existing OptiScaler files and `OptiScaler.ini` beside the game's real executable.
3. Extract the complete ZIP beside that executable.
4. Run `setup_windows.bat` and choose a proxy filename. `dxgi.dll` is the normal first choice for DirectX 12; Vulkan commonly uses `winmm.dll`. Individual games may require another supported proxy.
5. Supply your own licensed NVIDIA **`nvngx_dlssnr.dll`** model beside the game executable. It is proprietary and is deliberately not redistributed here.
6. Keep the included **`nvngx.dll_dlssnr.dll`** forwarder. Despite the similar name, it is a separate NeuRotic component and is required.
7. Start the game, open the OptiScaler overlay, enable Neural Rendering, and confirm that its status reports successful evaluations.

The package includes the tested Alpha 0.9.4 INI profile. Back up your current INI first if you want to preserve game-specific settings.

> [!CAUTION]
> Do not use injected graphics middleware in anti-cheat-protected multiplayer. It may trigger anti-cheat systems or account penalties.

### Green or white particle “flashbang”

Monster Hunter Wilds and some other games can flood the screen with bright green or white particles when NR interacts with the game's tone-mapping or post-processing shaders.

If this happens, install [ReShade with full add-on support](https://reshade.me/) and an SDR/HDR [RenoDX mod for the affected game](https://github.com/clshortfuse/renodx/wiki/Mods). A game-specific RenoDX shader rewrite often resolves or substantially reduces the flashing.

For **Monster Hunter Wilds**, use [RenoDX — HDR and SDR Fix / Tonemap / Color Grade](https://www.nexusmods.com/monsterhunterwilds/mods/202) and follow its current REFramework and ReShade requirements. [Source is available here](https://github.com/MohannedElfatih/renodx/tree/mhwilds). Use the SDR path for SDR or HDR path for HDR, avoid competing Auto HDR/RTX HDR tone mapping, and ensure ReShade and OptiScaler do not overwrite the same proxy DLL.

## What NeuRotic does not provide

NeuRotic is focused on Neural Rendering. It does not itself unlock DLSS Multi Frame Generation on unsupported GPUs, add native DLSS Frame Generation to a game that lacks it, or redistribute NVIDIA's Neural Rendering model.

For capabilities outside this fork's scope:

- **Recommended focused RTX 40-series MFG companion:** [Universal RTX 40 MFG Unlocker](https://github.com/dashdogy/RTX40MFG-Unlock), for supported games that already provide working Streamline DLSS Frame Generation.
- **Alternative Ada MFG project:** [mfg-unlock](https://github.com/matiasLombo/mfg-unlock).
- **Broader frame-generation approaches:** [DLSS Enabler](https://github.com/artur-graniszewski/DLSS-Enabler) and [DLSS Unlocked](https://github.com/ShyVortex/dlss-unlocked).
- **Mainline upscaler and frame-generation development:** [official OptiScaler](https://github.com/optiscaler/OptiScaler).

These are independent projects, not bundled dependencies. Combined compatibility is game-specific; read each project's instructions and warnings before stacking injectors.

## Current limitations

- This is an experimental Alpha, not a universal compatibility guarantee.
- D3D12 has received the strongest runtime and fault-transition coverage. Native Vulkan NR remains less tested.
- Deliberately spamming the NR toggle can still provoke an NVIDIA driver fault. Allow disable/re-enable transitions and resumed rendering to settle before toggling again.
- Third-party overlays, loaders, frame-generation tools, shader mods, and driver overrides can independently alter compatibility.
- A loaded model, static timing value, or lack of a crash does not prove NR is functioning; verify live successful evaluations and visible output.

## Validation

Alpha 0.9.4 passed non-game D3D12 WARP lifetime/fence tests, transactional allocation and failure-injection tests, capture-resize transitions, configuration concurrency checks, exposure-scan bounds/concurrency tests, readiness transitions, and 10,000 synthetic toggle cycles. User testing established the current build as viable for normal Alpha use while retaining the rapid-toggle limitation above.

Build identity and full technical notes are recorded in the [Alpha 0.9.4 release notes](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/blob/alpha-0.9.4/ALPHA-0.9.4.md).

## Building from source

The repository is development source, not an install package.

- Clone the repository with all submodules.
- Open `OptiScaler.sln` in Visual Studio 2022.
- Build `Release` / `x64`.
- The matched outputs are `OptiScaler.dll` and `nvngx.dll_dlssnr.dll`.

## Support NeuRotic

If the fork helps you and you would like to support continued testing and development:

☕ **[Ko-fi.com/espiownage](https://ko-fi.com/espiownage)**

## Credits and licensing

NeuRotic stands on the work of the OptiScaler community, Dagherbou's DLSS-NR research, and RenoDX. The NR colour-composition path includes design derived from [RenoDX](https://github.com/clshortfuse/renodx), with attribution and license text retained in `Licenses/RenoDX_ATTRIBUTION.txt`.

Review [LICENSE](LICENSE), the [`Licenses`](Licenses) directory, and the upstream repositories for complete authorship and licensing information.
