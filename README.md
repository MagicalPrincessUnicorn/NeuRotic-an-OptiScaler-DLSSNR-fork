# What NeuRotic Adds

NeuRotic is an experimental OptiScaler DLSS Neural Rendering fork focused on stronger visuals, practical performance control, and safer behavior during real gameplay.

It builds on the excellent work in [official OptiScaler](https://github.com/optiscaler/OptiScaler) and the [OptiScaler DLSS-NR fork](https://github.com/Dagherbou/OptiScaler_DLSSNR). Those projects provide the foundation; NeuRotic explores additional Neural Rendering routes, workload controls, Multipass rendering, diagnostics, compatibility work, and installation tooling.

## Latest release: Alpha 0.9.5

[NeuRotic Alpha 0.9.5](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/tag/alpha-0.9.5) is a large community-shaped update built from implementation commit `a57e4456`.

The practical goal is a visibly worthwhile NR result on the hardware people actually play games with—not treating maximum resolution, maximum pass count, or an RTX 5090-class GPU as the starting assumption.

- **Three NR routes:** Native Temporal, Present Image-Only, and experimental Present Enhanced.
- **Present Enhanced:** processes the final presented image, including the game HUD, while using fresh matched Native depth, motion, jitter, reset, and render-area information when available.
- **Real workload control:** follow the game's current native render area, use full output resolution, or select 100%, 77%, 67%, 58%, 50%, or 33% custom scale.
- **Visible dimensions:** the overlay shows the actual NR and output dimensions instead of hiding the cost behind a preset name.
- **Up to ten NR passes:** independent child-pass controls and shared strength tuning provide room to experiment without making every added layer all-or-nothing.
- **Stronger safety:** submission-aware GPU resource lifetime, transactional configuration and allocation, transition resets, guide matching, and original-image fallback when a safe result cannot be proven.
- **Vulkan NR improvements:** tighter RR ownership, resource validation, typed parameters, cached tuning configurations, history handling, and checked teardown.
- **Clearer interface:** reorganized NR controls, compact status, expandable diagnostics, and English, Spanish, French, German, and Portuguese localization.
- **Rebuilt installer and restore:** nine supported proxy names, explicit ReShade coexistence, verified payloads, rollback, and refusal to overwrite unrelated later changes.

Read the [full Alpha 0.9.5 patch notes](ALPHA-0.9.5.md) or the [complete enhancement inventory](docs/WHAT-NEUROTIC-ADDS.md).

## Installation

1. Download `NeuRotic-Alpha-0.9.5.zip` from the [Alpha 0.9.5 release](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/tag/alpha-0.9.5). GitHub's automatic source archives are not install packages.
2. Extract the complete NeuRotic folder somewhere outside the game directory.
3. Fully close the game and run `NeuRotic-Setup.cmd`.
4. Select the game's real executable and an appropriate proxy filename.
5. Supply your own licensed NVIDIA `nvngx_dlssnr.dll` model in the game folder. NeuRotic does not redistribute it.
6. Enable Neural Rendering in the overlay and confirm that the active status and evaluation counters advance.

The included `nvngx.dll_dlssnr.dll` is NeuRotic's forwarder; it is not the proprietary NVIDIA model despite the similar filename. Existing configuration and model files are preserved during an ordinary update. Setup creates a verified backup with its own `Restore.cmd`.

## Experimental boundaries

NeuRotic remains experimental rendering middleware. Results vary by game, GPU, driver, rendering API, DLSS files, output mode, and other injectors.

Present Enhanced can now attempt Frame Generation, Ray Reconstruction, NR Multipass, and DX11 combinations. That is experimental access, not a compatibility guarantee. Every frame must still pass the real guide, resource, device, queue, identity, and subrect checks. D3D12 SDR with those additional features off remains the comparison control.

Vulkan Present routes and HDR Present Enhanced are not implemented in Alpha 0.9.5. The private NVIDIA Feature 18 model is not included. Avoid anti-cheat-protected multiplayer and keep game-folder changes reversible.

## Thanks

Special thanks to **Tommy Creo** for testing the Vulkan issues and helping turn a difficult report into a concrete fix path. Thanks as well to everyone sharing screenshots, compatibility results, bug reports, and feature ideas—this release is full of things that began with community feedback.

If NeuRotic has improved a game for you and you ever feel like buying me a coffee while I keep tinkering with it, [Ko-fi is available](https://ko-fi.com/espiownage). There is no expectation whatsoever; testing and useful reports already make a real difference.

## Source, credits, and licensing

- [Alpha 0.9.5 source branch](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/tree/alpha-0.9.5)
- [NeuRotic releases](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases)
- [Official OptiScaler](https://github.com/optiscaler/OptiScaler)
- [Parent OptiScaler DLSS-NR fork](https://github.com/Dagherbou/OptiScaler_DLSSNR)

Review [LICENSE](LICENSE), the source branch's `Licenses` directory, and the parent projects for original authorship, attribution, and licensing.
