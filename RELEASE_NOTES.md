# NeuRotic Alpha 0.9.8 Hotfix — Patch notes

**Native Temporal compatibility is still a work in progress.** This hotfix improves stability and fixes tested paths; it does not complete compatibility across games, graphics APIs, or rendering modes. More Native work remains.

- **Native stability:** improved activation ordering and restoration of the game's rendering state. Unsupported or unrestorable work is rejected before custom rendering changes begin.
- **Compatibility and portability:** improved command recording, resource ownership, input and format validation, and safe resource retirement. Automatic hook setup handles more supported paths without weakening restoration checks.
- **Desktop shutdown:** the App stays visible while background work finishes, shows what is pending, and closes after cleanup completes.
- **Fresh-install defaults:** neural rendering starts off. Existing users' saved choices and settings are preserved.
- **Diagnostics:** current recording failures are distinguished from earlier refusal history, making remaining compatibility problems easier to identify. File logging is off by default in this public bundle.

Monster Hunter Wilds recovery and Dragon's Dogma 2 regression tests passed on the tested candidate and settings. Wilds was tested with `RestoreComputeSignature=false`; that result does not qualify its Auto setting. Support still depends on the game, route, GPU, driver, and provider files.

Known Native limitations remain in Stellar Blade, Crimson Desert, Doom Eternal, and Doom: The Dark Ages. Compatible Present/Post routes may work where Native does not. Use **NR Anything** when compatible in-game integration is unavailable.

[Download the Alpha 0.9.8 full installer](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/download/alpha-0.9.8/NeuRotic-Alpha-0.9.8-Full-Installer.zip)

---

[Download the full installer](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/releases/download/alpha-0.9.7/NeuRotic-Alpha-0.9.7-Full-Installer.zip)

# NeuRotic Alpha 0.9.7 — Patch notes

Features added and expanded since Alpha 0.9.6.

[Main page](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/blob/alpha-0.9.7/README.md) · [Credits and attribution](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork/blob/alpha-0.9.7/ATTRIBUTION.md)

## Desktop application

The new desktop application is the main hub for game discovery, installation, component management, settings, and diagnostics.

- **Installation Library:** discover games, scan selected directories, or add a game manually by choosing its executable. Search, favorites, hidden games, cover artwork, and grouped installation choices help organize the library.
- **Component management:** supply your NR model DLL and, when needed, a matching set of Streamline DLLs through the App's component folders. The installer copies available, accepted components into the selected game's installation.
- **Install and uninstall:** manage NeuRotic from the App or command-line installer, with progress, cancellation, and choices for unfamiliar files.
- **Settings before launch:** edit per-game INI settings from the Library. Game profiles and graphics-API recommendations provide starting settings and proxy choices where available.
- **Game shortcuts:** launch a selected game, open its game folder, open the INI file, or view its screenshot folder.
- **ReShade detection:** identify existing ReShade files and offer supported coexistence choices during installation.
- **Diagnostics:** inspect installation status, refresh observations, and export diagnostic bundles.

## Expanded neural-rendering compatibility

Present neural rendering has expanded across DirectX 11, DirectX 12, and Vulkan. It can process the presented game image on supported paths without requiring the game to provide a native DLSS integration. Native rendering remains available for games that supply compatible rendering inputs.

- Expanded combinations of neural rendering with DLSS Super Resolution, Ray Reconstruction, HDR, and frame generation on supported routes.
- Added Vulkan Present integration alongside the existing native Vulkan route. Supported Vulkan Present processing can use a DirectX 12 processing bridge.
- Improved DirectX 11 Present compatibility, including discard and sequential swapchains.
- Improved automatic use of available depth and motion guides, resolution matching, and handling of missing or stale inputs.
- Added capture routes for available depth and motion guides in supported games without native DLSS inputs. Guide availability depends on the capture path.
- Reduced flicker from repeated native-guide dropouts and improved fallback to the original game frame when processing is unavailable.
- Expanded resolution controls for the NR processing layer, including supported downscaling and output matching. Available choices depend on the route.

### Reported game tests

Neural rendering has been reported working in the games below. Testing includes games with and without DLSS; a working NR result does not imply that every HDR, Ray Reconstruction, or frame-generation combination is supported.

<details>
<summary><strong>Games reported working with neural rendering</strong></summary>

- Baldur's Gate 3
- Borderlands 3
- Clair Obscur: Expedition 33
- Crimson Desert
- Cyberpunk 2077
- Detroit: Become Human
- Doom Eternal
- Doom: The Dark Ages
- Dragon's Dogma 2
- God of War
- God of War Ragnarök
- Grand Theft Auto V
- Horizon Zero Dawn
- Kingdom Come: Deliverance II
- Marvel's Spider-Man Remastered
- Monster Hunter Rise: Sunbreak
- Monster Hunter Wilds
- No Man's Sky
- Onimusha
- Pragmata
- Satisfactory
- Stellar Blade
- The Sims 4
- The Witcher 3: Wild Hunt

</details>

- **Detroit: Become Human:** working Vulkan neural rendering without DLSS. It served as a primary test case for capturing rendering data without native DLSS inputs.
- **Borderlands 3:** working DirectX 11 Present rendering.
- **The Sims 4:** working neural rendering; a frame-generation solution is not yet available for this game.
- **Onimusha:** working neural rendering, with a game-detection profile to select appropriate starting settings.

Skyrim with Community Shaders and DLSS is expected to work, but a working result is not yet confirmed. Metal Gear Solid V: The Phantom Pain also remains unconfirmed.

## Input sources

New input-source and transport controls distinguish how NeuRotic receives rendering data:

- **Native:** uses compatible inputs exposed by the game's rendering or upscaling integration, including available depth and motion data.
- **Built-in capture:** uses NeuRotic's own capture path to acquire supported rendering inputs.
- **ReShade:** provides a connection option for a compatible ReShade input provider.
- **External:** accepts inputs supplied by a compatible external provider.

Automatic selection uses eligible inputs when available. ReShade and external provider routes remain experimental; selecting a source does not guarantee that it supplies usable data in a particular game.

## Improved Multipass

Native Multipass was already available in 0.9.6. This update extends Multipass to supported Present routes and improves integration with the expanded rendering pipeline.

- Basic controls apply shared settings across passes; Advanced controls allow individual pass adjustments.
- Model and detail strengths, processing resolution, and supported downscaling choices remain configurable.
- Supported Present chains complete their processing before handing the result to frame generation. Availability depends on the rendering route and combination of features.

## HDR and Ray Reconstruction

HDR support has expanded beyond the earlier experimental paths, with improved NR composition and color handling. Ray Reconstruction support has also been refined.

- Expanded supported NR + HDR10 and NR + Ray Reconstruction combinations.
- Improved HDR10/PQ and scRGB conversion, normalization, and composition.
- Improved native Vulkan HDR handling, including exposure and white-point handling where applicable.
- Added clearer HDR format and color-space information in diagnostics.
- Improved Ray Reconstruction preset controls, including Default, Use Global, and Use Game selections.

## Frame generation and MFG

- Added native Multi Frame Generation compatibility for RTX 40 series GPUs on supported DirectX 12 paths, with manual ratio controls within the provider's supported limits.
- Added opt-in experimental MFG compatibility for RTX 20 and RTX 30 series GPUs.
- Expanded supported neural-rendering combinations with native frame generation, including HDR10 and Ray Reconstruction routes.
- Improved Vulkan integration with supported frame-generation paths and menu behavior while frame generation is active.
- Added clearer information about requested ratios, provider activity, observed output, and settings that require a restart.

## Renewed in-game interface

The in-game menu has been redesigned around dedicated sections for neural rendering, upscaling, frame generation, comparison, and diagnostics.

- Official NeuRotic branding, light and dark themes, and a draggable menu.
- Revised layouts, scaling, popup sizing, and translated-label fit.
- Individual and section Reset controls, with clearer saved-setting and restart information.
- Configurable menu shortcuts and mouse, keyboard, and controller input settings.
- Comparison and capture controls, including Original, processed, Split, and Stripes views where supported.
- Rendering diagnostics organized into Runtime, Inputs, Timing, and Connections, with refresh and report-copy actions.

## Languages and community translations

Completed interface translations include Simplified Chinese, Japanese, Korean, German, French, Spanish, Portuguese (Brazil), Polish, and Russian.

- Shared language packs for the desktop application and in-game interface.
- Import and export of community language packs.
- A translation editor with search, filters, editable metadata, and control previews.
- Separate Save Draft and Apply actions, with improved custom-pack persistence and CJK font rendering.

## Desktop NR Anything — early alpha

NR Anything applies neural rendering to a selected desktop window. It provides window selection, processing quality, detail and color controls, saved preferences, comparison views, and PNG capture.

## Experimental inspection tools

- Character Inspector controls for people and object detection, optional pose detail, detection limits, labels, and appearance.
- Inspector integration for supported DirectX 11, DirectX 12, and Vulkan paths.
- Searchable object rules with per-rule properties, sorting, Remove, Undo, and individual resets.

## Planned improvements

Development priorities for future updates:

- **Multipass performance:** reduce the processing cost of additional passes and improve control over the balance between image quality and frame rate.
- **Broader frame generation:** expand support across more games, applications, and hardware, especially older games and games without built-in frame generation. Use available depth and motion data where supported, including generation after neural rendering.
- **NR Anything:** expand desktop processing with upscaling and frame generation, including DLSS and FSR options where applicable, and improve depth and motion inputs.
- **Customizable input capture:** provide more control over which sources supply image, depth, and motion data, with more flexible connections between capture and processing.
- **Older and 32-bit games:** develop compatible integration routes for executable architectures and legacy graphics APIs that the current in-game package does not support.
- **Motion clarity and stability:** improve temporal consistency and reduce motion-related artifacts.
- **Native Temporal color controls:** make color adjustments more direct within the Native Temporal rendering path.
- **Upscaling integration:** bring OptiScaler's upscaling capabilities and NeuRotic's neural rendering into a more consistent processing workflow, with coordinated controls.
- **Per-object weighting:** explore finer control over how strongly processing applies to specific objects.
- **Light mode:** complete remaining theme coverage and improve control styling, contrast, and readability.

Feature availability depends on the game, graphics API, GPU, driver, and provider version. Experimental options remain opt-in.
