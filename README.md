```text
███╗   ██╗███████╗██╗   ██╗██████╗  ██████╗ ████████╗██╗ ██████╗
████╗  ██║██╔════╝██║   ██║██╔══██╗██╔═══██╗╚══██╔══╝██║██╔════╝
██╔██╗ ██║█████╗  ██║   ██║██████╔╝██║   ██║   ██║   ██║██║
██║╚██╗██║██╔══╝  ██║   ██║██╔══██╗██║   ██║   ██║   ██║██║
██║ ╚████║███████╗╚██████╔╝██║  ██║╚██████╔╝   ██║   ██║╚██████╗
╚═╝  ╚═══╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝ ╚═════╝    ╚═╝   ╚═╝ ╚═════╝
```

# A forked branch of OptiScaler DLSS-NR

NeuRotic is an experimental OptiScaler fork focused on making Neural Rendering practical, playable, and approachable in real games.

It builds on the excellent work of [OptiScaler](https://github.com/optiscaler/OptiScaler) and the [OptiScaler DLSS-NR fork](https://github.com/Dagherbou/OptiScaler_DLSSNR). NeuRotic extends that foundation with additional rendering routes, performance controls, Multipass processing, compatibility work, diagnostics, a redesigned interface, and safer installation tooling.

> NeuRotic is experimental rendering middleware. Results vary by game, GPU, driver, graphics API, DLSS files, display mode, and other tools in the rendering chain.

## Table of Contents

- [The mission](#the-mission)
- [Latest release: Alpha 0.9.6](#latest-release-alpha-096)
- [Important: Experimental Mode](#important-experimental-mode)
- [What Alpha 0.9.6 adds](#what-alpha-096-adds)
- [An interface designed for easy access](#an-interface-designed-for-easy-access)
  - [Neural Rendering Advisor](#neural-rendering-advisor)
  - [DLSS Neural Rendering](#dlss-neural-rendering)
  - [Basic and Advanced Multipass](#basic-and-advanced-multipass)
- [Current status and roadmap](#current-status-and-roadmap)
- [Installation](#installation)
  - [Getting the NVIDIA model](#getting-the-nvidia-model)
  - [Installer safeguards](#installer-safeguards)
- [Uninstallation](#uninstallation)
- [Help shape NeuRotic](#help-shape-neurotic)
- [Support NeuRotic](#support-neurotic)
- [Special thanks](#special-thanks)
- [Source, credits, and licensing](#source-credits-and-licensing)

## The Mission

NeuRotic exists to make Neural Rendering as accessible, approachable, compatible, and useful as possible across a broad range of games.

Performance is fundamental to that mission. Not everyone owns an RTX 5090—or even a 50-series card—and a visual improvement is not especially useful if the game no longer runs well enough to enjoy. NeuRotic therefore provides multiple rendering routes, lower-cost workload choices, resolution controls, and per-pass tuning so people can pursue a worthwhile visual improvement on the hardware they actually own.

Even at this early Alpha stage, I firmly believe NeuRotic has achieved the core of that goal in the configurations where it has been successfully tested: improving Neural Rendering visuals and effectiveness while also increasing performance.

Some additions exist because they move that mission forward. Others exist because I thought they would be fun to build. Both are part of what NeuRotic is.

Anti-cheat is a separate problem. NeuRotic cannot promise compatibility with protected online games, and it should only be used where game modifications are permitted.

## Latest Release: Alpha 0.9.6

[NeuRotic Alpha 0.9.6](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/tag/alpha-0.9.6) is the largest NeuRotic update so far. It substantially rebuilds the Present rendering foundation introduced in Alpha 0.9.5 and adds DirectX 11 transport, experimental HDR handling, a new interface, smarter configuration guidance, stronger Frame Generation recovery, and a complete public installer/uninstaller workflow.

The final Advisor pass also makes blocked routes much easier to understand. It now separates Before- and After-upscaling analysis, responds sooner when a route cannot begin, and tells you when Present Enhanced specifically requires a saved Experimental Mode setting instead of leaving you to wonder why Neural Rendering is not running.

Read the [complete Alpha 0.9.6 patch notes](ALPHA-0.9.6.md) for the full feature list, experimental boundaries, and known limitations.

## Important: Experimental Mode

If Neural Rendering says **BLOCKED**, refuses to start, or returns to the original game image, it may be protecting you from a rendering combination that is not supported by the normal out-of-box configuration.

Many of the frontier combinations people have asked for—including certain uses of Frame Generation, HDR, Multipass, DirectX 11, and Present Enhanced—live behind **Unlock Experimental Mode**. Ray Reconstruction also has route and placement restrictions; some combinations remain unavailable even in Experimental Mode when NeuRotic cannot support them safely.

To try an available experimental path:

1. Open the **Neural Rendering** page.
2. Scroll below Multipass to **Neural Rendering — Experimental Overrides**.
3. Enable **Unlock Experimental Mode** and accept the warning.
4. Enable only the override needed for the feature you are testing.
5. Select **Save experimental settings** or use the global **Save Settings** button. Checking a box without saving it does not activate the override.
6. Read any BLOCKED, waiting, or fallback explanation shown by NeuRotic. The Advisor will name the required saved override when it recognizes a supported experimental prerequisite.

Experimental Mode does not force every combination to run. It opens implemented but less-tested paths while leaving known unsafe GPU-resource behavior and confirmed hard failures locked.

> **Personal commentary:** I am one person with one computer. I have fixed or isolated every issue I could reproduce and put this release through as many hardening and validation passes as I reasonably could. I believe NeuRotic has received unusually thorough hardening for a project at this stage, and I am proud of that—but I cannot reproduce every game, system, or rendering combination. That is why NeuRotic is still in Alpha. The normal experience is intentionally conservative; Experimental Mode is there for people who want to explore newer paths that may still crash, behave strangely, or need more work. Known GPU-resource corruption risks and other confirmed hard failures remain blocked. If you encounter a bug, I am sorry; I tried my best to keep the default experience safe, and your report may help me improve the next release.

## What Alpha 0.9.6 Adds

- **Three Neural Rendering routes:** Native Temporal, Present Compatibility, and experimental Present Enhanced.
- **Native Temporal improvements:** better handling of inputs, guide dimensions, route transitions, resolution changes, and an optional experimental camera-cut preservation mode.
- **Rebuilt Present behavior:** stronger resource ownership, history handling, fallback, resize recovery, and Frame Generation synchronization.
- **DirectX 11 Neural Rendering:** Native and Present paths can use NeuRotic's synchronized DirectX 12 bridge in compatible games.
- **Experimental HDR handling:** successful color-space, format, resize, metadata, and swapchain observations are tracked independently instead of relying on one inferred process-wide state.
- **Neural Rendering Advisor:** analyze Before- or After-upscaling routes, receive useful feedback sooner, and see the exact saved experimental prerequisite when a supported route is blocked.
- **Simpler resolution choices:** match the game's render resolution, process at full output, or choose a manual workload.
- **Basic and Advanced Multipass:** quickly tune a selected pass range or take individual control of as many as ten passes.
- **Experimental Options:** deliberately unlock less-tested combinations while confirmed unsafe conditions remain blocked.
- **Comparison screenshots:** create matched PNG comparisons with NeuRotic version and configuration information embedded inside the images.
- **Improved Frame Generation recovery:** Present modes can pause and safely requalify across supported graphics and compute handoffs instead of leaving gameplay frozen.
- **Optional camera-cut preservation:** an experimental, default-off Native Temporal option can preserve NR through compatible reset-only cuts.
- **Independent menu input controls:** choose whether mouse, keyboard, and controller input remain available to the game while the overlay is open.
- **Interface brightness:** tune the brightness of NeuRotic's interface without changing the game, HDR output, or captured screenshots.
- **OptiClip:** a completely essential Clippy-inspired assistant. This description of his importance has not been independently verified.
- **Safer Setup and Uninstall tools:** verified packages, backups, rollback, repair, proxy changes, ReShade coexistence, interrupted-operation recovery, and preservation of user settings and models.

## An Interface Designed for Easy Access

Alpha 0.9.6 reorganizes NeuRotic around the choices most people actually need. Essential controls and current status appear first; detailed diagnostics remain available when they are useful.

### Neural Rendering Advisor

The **Neural Rendering Advisor** considers the current graphics path, GPU, output, guide availability, Frame Generation state, and your preferred performance/quality goal. Choose whether to analyze Neural Rendering **Before** or **After** upscaling: After exposes all three compatible route cards, while Before focuses on Native Temporal.

The Advisor now stops an unproductive initial wait after five seconds and shows the current reason when one is available. If Present Enhanced is blocked by Frame Generation policy, its card names **Unlock Experimental Mode**, **Override FG Guardrails**, and **Save experimental settings** directly. It never enables or saves those choices for you.

It rejects measurements that are incomplete or stale and recommends the best option from the routes it successfully tests.

Temporary analysis settings are restored after the test. The final choice remains yours.

### DLSS Neural Rendering

The redesigned **DLSS Neural Rendering** page places activation, Apply, live status, timing, route, and working dimensions where they are easier to understand. Advanced diagnostics and less-common settings stay available without dominating the main experience.

### Basic and Advanced Multipass

In **Basic Multipass**, choose your maximum pass count and use the shared **Model Strength** and **Detail Strength** sliders to adjust the selected additional passes with far less clicking. Pass 1 remains independent.

**Advanced Multipass** retains individual pass tabs, model choices, resolution, composition, strength, and diagnostic controls for users who want precise control over every layer.

Two passes are the sensible starting point. Higher counts become increasingly expensive and experimental.

## Current Status and Roadmap

| Area | Status |
| --- | --- |
| Experimental Mode | Included; unlocks implemented but less-tested paths while hard safety blocks remain |
| DirectX 12 Native Temporal | Available; primary foundation |
| Present Compatibility | Available; substantially revised in 0.9.6 |
| Present Enhanced | Available; **Experimental** |
| DirectX 11 through the D3D12 bridge | Included; **Experimental and game-dependent** |
| HDR observation and guarded Present attempts | Included; **Experimental** |
| Multipass | Available; higher counts are **Experimental** |
| Vulkan Native path | Limited and less extensively tested |
| Vulkan Present routes | Roadmap; not ready |
| Anti-cheat compatibility | Separate problem; not guaranteed |

The next phase of development will continue strengthening the DirectX foundation while expanding game coverage, HDR and Ray Reconstruction compatibility, Frame Generation transitions, Vulkan support, performance tuning, and the broader rendering framework.

Vulkan is absolutely a direction I want to pursue. It has simply received less implementation and testing attention because I have fewer Vulkan games available, and I want the existing DirectX foundation to be dependable before expanding too aggressively.

## Installation

1. Download the complete `NeuRotic-Alpha-0.9.6.zip` release package from the [Alpha 0.9.6 release](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/tag/alpha-0.9.6). GitHub's automatic source archives are not installation packages.
2. Extract the complete package somewhere outside the game directory.
3. Fully close the game.
4. Run `NeuRotic-Setup.cmd`.
5. Select the game's real executable rather than its launcher.
6. Choose an appropriate proxy filename. DirectX games commonly use `dxgi.dll`; Vulkan games commonly use `winmm.dll`.
7. Wait for Setup to report that the complete installation was verified.
8. Supply the required NVIDIA Neural Rendering model, launch the game, and press `Insert` to open NeuRotic unless you have saved a different shortcut.

### Getting the NVIDIA Model

NVIDIA's proprietary `nvngx_dlssnr.dll` model is not distributed with NeuRotic. Obtain a genuine NVIDIA-signed copy from a game installation or NVIDIA driver package that legitimately includes it, then place it beside the game executable.

The included `nvngx.dll_dlssnr.dll` is NeuRotic's forwarder. Despite the similar name, it is not the NVIDIA model. Neural Rendering requires both files.

The third-party [DLSS Version Toolkit](https://github.com/scubamount/dlss-version-toolkit) can help locate, inspect, and manage compatible NVIDIA DLLs already available on your computer. It is not affiliated with NeuRotic and may not be able to download every required model automatically.

Avoid proprietary DLLs from random mirrors.

### Installer Safeguards

Setup verifies the package, records the selected proxy, protects existing settings and models, creates recovery data, and rolls the complete operation back if installation cannot finish safely.

It supports nine proxy choices, managed updates and repairs, repeated proxy changes, explicit ReShade coexistence, long game-directory paths, interrupted-operation recovery, and refusal to overwrite unrelated files changed after installation.

Setup never guesses who owns an existing DLL. If it finds a collision, it asks what you want to do.

## Uninstallation

Run either `NeuRotic-Uninstall.cmd` from the downloaded package or `Uninstall NeuRotic.cmd` from the game directory.

The uninstaller offers:

- **Keep Settings — Recommended**
- **Remove Settings**
- **Full Cleanup**

Keep Settings restores the recorded pre-NeuRotic game files while retaining your configuration for a future reinstall. The private NVIDIA model remains user-owned and is preserved unless its separate removal is explicitly requested and confirmed.

You are completely free to uninstall NeuRotic. I will take it personally, but only jokingly. Mostly.

## Help Shape NeuRotic

Before reporting a problem, please ask:

**Does the same problem still happen when NeuRotic is completely turned off or Neural Rendering is disabled?**

If the answer is yes, please do not report it as a NeuRotic bug. It is not being caused by NeuRotic.

For NeuRotic-specific bugs, compatibility results, ideas, logs, screenshots, and wonderfully strange edge cases, join the [RenoDX Discord](https://discord.gg/Ce9bQHQrSV) and let me know how it is holding up.

## Support NeuRotic

If NeuRotic has improved a game for you and you would like to help support its continued development and longevity, you can:

- [Support me on Ko-fi](https://ko-fi.com/espiownage)
- [Star the NeuRotic repository](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork)
- Share the project
- Test new releases and provide useful feedback
- Join the [RenoDX Discord](https://discord.gg/Ce9bQHQrSV)

Any support is deeply appreciated, but never a requirement.

## Special Thanks

- The OptiScaler maintainers and contributors.
- Dagherbou and the contributors to the OptiScaler DLSS-NR fork.
- **Tommy Creo**, for testing Vulkan issues and helping investigate a particularly stubborn problem.
- **JuanTacos**, for testing an unusual scene-transition bug and helping isolate its conditions.
- The RenoDX community.
- Everyone who has contributed testing, logs, screenshots, recordings, ideas, and bug reports.

## Source, Credits, and Licensing

- [NeuRotic releases](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases)
- [Official OptiScaler](https://github.com/optiscaler/OptiScaler)
- [Parent OptiScaler DLSS-NR fork](https://github.com/Dagherbou/OptiScaler_DLSSNR)

Review [LICENSE](LICENSE), the `Licenses` directory, and the parent projects for original authorship, attribution, and licensing.
