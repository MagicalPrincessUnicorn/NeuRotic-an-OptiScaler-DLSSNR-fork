<!--
Alpha 0.9.5 inventory for NeuRotic implementation
a57e445604e3aa42c32d92908074ef0b565f04b6.
Comparison base: Dagherbou/OptiScaler_DLSSNR, origin/dlss-neural-rendering,
commit 973761621353b99bee3dc7d4bb27b117fef2644f.
This is a source-ancestry inventory, not a universal runtime-support claim.
-->

# What NeuRotic Adds

> **Inventory ID:** `NR-PARENT-DIFF-ALPHA-095-A57E4456-R01`
> **NeuRotic Alpha 0.9.5 implementation:** `a57e445604e3aa42c32d92908074ef0b565f04b6`
> **Parent source:** `973761621353b99bee3dc7d4bb27b117fef2644f` on `Dagherbou/OptiScaler_DLSSNR:dlss-neural-rendering`

NeuRotic is built on the excellent work already present in OptiScaler and the OptiScaler DLSS-NR fork. Those projects provide the foundation; NeuRotic is my experimental extension focused specifically on Neural Rendering image quality, practical in-game performance, control, diagnostics, and safety. This inventory covers the exact 93-commit NeuRotic ancestry above the parent branch tip: 179 changed files, approximately 30,811 added lines, and 3,430 removed lines in the current source snapshot.

Those numbers include tests, documentation, localization, and packaging—not just rendering code. More importantly, they represent several distinct improvements: new Neural Rendering placements, direct control over NR workload, Multipass rendering, stronger GPU-lifetime safety, clearer diagnostics, Vulkan repairs, a rebuilt interface, and a reversible installer.

## Short version for the main page

NeuRotic extends the OptiScaler DLSS-NR foundation with an experimental rendering stack focused on stronger visuals, practical performance control, and safer long-session behavior.

The performance goal is ordinary gameplay—not designing every feature around the assumption that everyone owns an RTX 5090-class GPU. NeuRotic provides lower-resolution paths, per-route workloads, and per-pass controls so users can aim for a visibly worthwhile result on the hardware they actually have. Maximum resolution and maximum pass count are options, not the definition of success.

- **Three NR routes:** Native Temporal, Present Image-Only, and the experimental Present Enhanced route with captured Native depth and motion guides.
- **Practical performance controls:** follow the game’s real native render resolution, run at full output, or choose a custom NR scale while seeing the actual working dimensions.
- **Up to ten NR passes:** independent child-pass state and controls, plus shared strength tuning when you want to adjust a stack together.
- **Ray Reconstruction-aware routing:** preserves native RR ownership and handles its dimensions, subrects, and DLAA transitions explicitly.
- **Stronger GPU safety:** submission-aware resource lifetime, reset quarantine, transactional allocation, immutable configuration snapshots, and fail-closed recovery paths.
- **Improved Vulkan NR:** tighter RR selection, resource validation, typed tuning, cached configurations, history handling, and checked teardown.
- **A clearer overlay:** reorganized NR controls, detailed timing/readiness/fallback diagnostics, and English, Spanish, French, German, and Portuguese localization.
- **Safer installation and restoration:** nine selectable proxy names, explicit ReShade coexistence, package verification, automatic rollback, and a standalone Restore tool.

NeuRotic remains experimental rendering middleware. Feature availability and results can vary by game, GPU, driver, API, DLSS files, and other tools in the rendering chain.

## Homepage-ready highlights

These are the strongest differences to consider featuring on the main page.

### More ways to run Neural Rendering

NeuRotic adds three user-facing ways to use the underlying NR technology:

- **Native Temporal** retains the game’s temporal inputs and offers established Quality and Performance placements.
- **Present Image-Only** processes the final presented image without Native depth or motion guides.
- **Present Enhanced** processes the final presented image while using a fresh matched pair of captured Native depth and motion guides when it is safe to do so.

Together, these routes provide a flexible image-quality and performance tuning space while preserving the parent project’s underlying NR foundation. Present Enhanced keeps the game HUD in the processed image and is designed to fail closed to the original frame, with a visible reason, whenever the temporal guides cannot be trusted.

### Practical performance for playing games

NeuRotic is meant to be played, not merely configured for a maximum-quality screenshot on the fastest available GPU. Present routes can follow the game’s real native render area, run at full output resolution, or use a custom 100%, 77%, 67%, 58%, 50%, or 33% scale. The overlay reports the actual NR and output dimensions so users can make an informed visual/performance trade-off.

Native Temporal also offers a Performance placement that runs NR before the game’s final DLSS Super Resolution pass, reducing the model’s pixel workload while retaining the game’s native jitter and mode-dependent input sizes. Multipass layers can be reduced independently, and Present routes remember their own resolution choices. The intention is to make enhanced visuals practical during real gameplay, including on hardware below the absolute top end. Exact performance and visual results remain game-, model-, scene-, and hardware-dependent.

### Neural Rendering Multipass

NeuRotic expands NR from a single model evaluation into a configurable Multipass stack with as many as ten passes. Additional passes can own independent model sessions, work surfaces, temporal history, model resolution, resampling, model style and tuning, composition settings, skin masking, and application strength.

Per-pass controls make it possible to decide where extra image enhancement is worth its cost. Shared Model Strength and Detail Strength controls can also adjust selected child passes together without overwriting Pass 1 or unrelated saved profiles. Two total passes are the sensible starting point; three and four are advanced, while five through ten remain experimental.

### Stronger rendering safety and transition handling

NeuRotic adds explicit readiness, generation, submission, and GPU-completion tracking throughout its NR extensions. It is designed to retain or bypass work when ownership is uncertain rather than prematurely reusing command-list resources, descriptors, uploads, readbacks, model sessions, or scratch surfaces.

NR enable/disable, route changes, resets, resolution changes, output resizing, device changes, and shutdown now have explicit lifecycle behavior. This work is less flashy than a new slider, but it is a major part of making NeuRotic’s added routes and controls practical during normal gameplay.

### Ray Reconstruction-aware NR routing

NeuRotic recognizes native DLSS Ray Reconstruction ownership and keeps that ownership intact where required. Its Performance route steps aside at an incompatible reconstruction boundary, while logical size, subrect, and DLAA transitions are handled explicitly around the native RR route.

### Vulkan NR repairs and tighter routing

NeuRotic includes a cleaned Vulkan NR component with stricter device and resource validation, primary presented-RR selection, auxiliary/native-frame-generation exclusion, duplicate suppression, typed parameters, cached tuning configurations, history reset, and checked teardown. Vulkan remains a game-specific compatibility path and does not inherit every D3D12-only route.

Special thanks to Tommy Creo for serving as a tester on the Vulkan issues and helping track down a stubborn problem in this path.

### A clearer overlay with real NR diagnostics

The interface is reorganized around Neural Rendering routes, Multipass, and child-pass controls. It adds clearer active states, route descriptions, actual working dimensions, model and total NR timing, composition/copy timing, rebuild and reset information, evaluation failures, fallback reasons, and more useful readiness reporting.

The UI also gains top-level tabs, improved menu sizing and anchoring, safer text wrapping, clearer keybind guidance, distinct control identifiers, improved selected-state styling, and directly authored English, Spanish, French, German, and Portuguese localization.

### A reversible NeuRotic installer

NeuRotic adds a customer-oriented Setup and Restore system for its fork-specific packages. Setup supports the nine proxy names implemented by this build, backs up occupied targets, verifies package contents, preserves existing configuration and private-model files, and rolls back the complete transaction if installation fails.

If an occupied `dxgi.dll` is known by the user to be ReShade, Setup can explicitly rename it to `ReShade64.dll` and enable NeuRotic’s ReShade compatibility setting. It never guesses the identity of an unrelated DLL. Restore records and verifies what it replaces and refuses to overwrite later unrelated runtime changes.

## Complete enhancement inventory

### Neural Rendering placement and route architecture

- Added validated jitter-aware Pre-SR Neural Rendering while preserving the game’s original temporal inputs and mode-dependent render sizes.
- Added distinct Native Temporal Quality and Performance behavior alongside the established NR path.
- Added readiness-driven Pre-SR admission so the new route waits for compatible resources rather than relying only on a startup-frame delay.
- Added reset quarantine so NR does not run through a held or incompatible reset.
- Added history invalidation when Pre-SR mode, logical dimensions, format, subrect, or lifecycle state changes.
- Added route persistence and transactional route publication.
- Added Ray Reconstruction-aware placement and retained native RR reconstruction ownership.
- Corrected RR/native-SR input, logical-size, render-subrect, and DLAA transition handling.
- Added NGX feature-identity tracing to distinguish RR, SR, and related handles during diagnosis.
- Added Present Image-Only as a separate final-image NR route.
- Added direct D3D12 Present processing for supported RGBA8 SDR swapchains.
- Added D3D12 R10 SDR conversion into and out of the private NR image.
- Added a synchronized D3D11-to-D3D12 Present bridge for supported RGBA8 and R10 SDR targets.
- Added explicit Present admission checks for swap effect, sample count, format, color space, dirty rectangles, scrolling, device identity, shared-resource support, and private-resource creation.
- Added visible Present compatibility paths, success counters, and fallback reasons.
- Added continuous Present temporal history across completed frames instead of resetting every evaluation.
- Added history invalidation after unsupported targets, failed presents, copy/bridge/conversion failures, interruptions, and route or workload changes.
- Added immutable Present conversion bindings per resource generation to prevent descriptor mutation while earlier GPU work can still be executing.
- Added Present Enhanced as a third NR route.
- Added bounded Native depth and motion capture for Present Enhanced, including supported D32S8 depth-plane handling without disturbing independently stored stencil.
- Added matching of guide pairs by observed Present interval, swapchain identity, backbuffer, output size, queue submission, and generation.
- Added forwarding of depth direction, reset state, motion scale, jitter, exact guide dimensions, and independent depth/motion render-subrect origins.
- Added strict refusal for missing, stale, ambiguous, differently queued, unsupported, or otherwise unsafe guides.
- Removed the blanket rejection of Frame Generation, Ray Reconstruction, NR Multipass, and DX11 Present Enhanced attempts while retaining all actual guide/resource admission checks.
- Added experimental-combination tracking so FG, RR, or Multipass changes invalidate Present history and emit one transition diagnostic rather than flooding the log per frame.
- Allowed DX11 Present Enhanced to reach real guide matching through the existing shared D3D12 bridge when the producer, device, queue, resources, and captured metadata are compatible.
- Preserved the game HUD in the final Present image while documenting that scene guides do not describe individual HUD pixels.

### Resolution, workload, and image-performance controls

- Added explicit per-mode DLSS preset routing so Performance and Ultra Performance can use their own presets.
- Kept per-mode selections distinct from `USE GLOBAL`, `NVIDIA DEFAULT`, and the global preset.
- Added Native Temporal Performance placement to reduce the model’s input pixel count before native Super Resolution.
- Added route-specific NR resolution policies and migrated the earlier Present workload setting.
- Added **Follow native render resolution**, derived from fresh game render-subrect metadata rather than a preset name.
- Added **Always full output resolution**.
- Added **Custom scale** choices at 100%, 77%, 67%, 58%, 50%, and 33%.
- Added actual NR and output dimensions to the overlay.
- Stored Present Image-Only and Present Enhanced policies and custom scales independently.
- Added migration from legacy `PresentWorkload` values while preserving explicit new settings.
- Preserved hidden custom-scale choices while another resolution policy is active.
- Added per-axis motion and jitter scaling for reduced Present Enhanced workloads.
- Added safe history reset and resource rebuild when effective NR or output dimensions change.
- Added clean fallback while old work is still in flight or fresh native-size metadata is unavailable.

### Multipass Neural Rendering

- Added a safe experimental second composed NR layer.
- Added a dedicated Multipass panel and independent second-layer configuration.
- Added separate model session, creation signature, work surfaces, temporal history, build/evaluation counters, and failure state for additional passes.
- Added independent model resolution, resampler, enlargement behavior, model preset/style, tuning, composition, skin-mask, highlight, and model-application controls for child passes.
- Added one-time seeding of older profiles so the second layer can inherit the previously loaded first-pass picture before later edits diverge.
- Expanded the implementation and interface to as many as ten passes.
- Added an adaptive replay-safe composition pool sized for Multipass demand instead of relying on the earlier fixed-capacity behavior.
- Corrected Multipass presentation so the final result is presented once after the selected chain.
- Corrected motion scaling through additional passes.
- Added optional allocation fallback and safe effective-chain reduction when later work cannot be admitted.
- Added shared Model Strength and Detail Strength controls for Pass 2 through the selected final pass.
- Preserved Pass 1 and unselected child profiles during shared edits.
- Added mixed-value reporting, independent resets, release-to-commit slider behavior, and stale-edit cancellation when targets or profiles change.
- Added stronger active-tab styling, selected-pass navigation, disabled-state guidance, and experimental-cost warnings.
- Kept Multipass disabled and one pass selected by default in the integrated source profile.

### GPU completion, ownership, and lifetime safety

- Added command-list recording tickets connected to actual queue submissions and private completion fences.
- Added replay-aware tracking when a closed command list is submitted more than once or across observed queues.
- Required recording invalidation plus completion of every observed execution before CPU-side reuse.
- Prevented unsubmitted cancellation, device loss, failed tracking, and unknown hook combinations from being mistaken for successful completion.
- Added bounded fail-closed behavior when tracking, descriptor, upload, capture, timing, scaler, composition, or retirement capacity is exhausted.
- Added completion-gated reuse for composition slots, output scalers, timers, meter/calibration rings, exposure readbacks, capture surfaces, and retired NR objects.
- Added submission-aware GPU timing using the submitting queue’s frequency and suppression of ambiguous multi-queue samples.
- Added completion-aware retirement for private DLAA, proxy features, model outputs, scratch resources, scalers, and parameter blocks.
- Added controlled shutdown admission and bounded completion draining before native NGX teardown.
- Added code-lifetime protection for late command-list and queue callbacks.
- Added explicit device-identity checks so resources are not silently reused across incompatible devices.
- Added idempotent shutdown paths and process-restart requirements after terminal teardown/device-loss states that cannot be proven safe.

### Configuration, toggle, and allocation robustness

- Added a shared process-lifetime configuration lock for NR settings.
- Added immutable per-upscale configuration snapshots so one native upscale cannot observe two conflicting NR placements.
- Added transactional publication for route, enable, and resume-generation changes.
- Added synchronized keybind updates and safe NR enable/disable transitions.
- Added fresh Feature 18 retirement and recreation when NR is re-enabled.
- Added complete allocation transactions for size-dependent scratch resources, meter/scanner rings, guide clones, readbacks, and Vulkan image bundles.
- Preserved the last valid live state when a replacement allocation fails.
- Added a dispatch-local state ledger so early-return and failure paths restore only the resources transitioned by that dispatch.
- Prevented failed encode, downsample, model, composition, or resolve work from being counted as a successful result.
- Added safe capture persistence through temporary unsupported shapes and safe replacement after size or format changes.
- Added synchronized exposure-scanner selection, bounds, retained readbacks, and status text.
- Hardened the optional proxy path so it follows the same resolve/cleanup contract and does not claim success before output composition.

### Readiness, telemetry, and diagnostics

- Added separate total NR, model, composition/copy, and associated timing information.
- Added input/output/working dimensions, selected mode, frame/reset state, feature builds and rebuilds, evaluation failures, and structured troubleshooting logs.
- Added status distinctions for disabled, loaded, waiting, reset, quarantined, failed, and successfully evaluating states.
- Hid stale model timing when the requested route or generation is not actually ready.
- Added route-specific Present active/fallback reporting and sustained history counters.
- Added compact guide capture, match, evaluation, and refusal diagnostics for Present Enhanced.
- Added cross-game Present capability diagnostics and the synchronized D3D11 bridge work originally targeted at Baldur’s Gate 3.
- Added Vulkan model/cache status and clearer reporting when Present routes are unavailable on that API.
- Added NeuRotic update-status UI for release visibility.
- Added clearer Neural Rendering toggle-key guidance.

### Vulkan NR hardening and routing

- Closed Vulkan NR generations before native NGX shutdown.
- Added device-identity checks before reading retained or mapped Vulkan resources.
- Added cleanup for partially created images, memory, views, readbacks, samplers, layouts, pools, and descriptor resources.
- Added validation of required sampler, layout, pool, descriptor, and resource inputs before evaluation.
- Increased replay-safe capacity for command-list-heavy game behavior.
- Added primary presented Ray Reconstruction selection.
- Excluded auxiliary and native-frame-generation evaluations from NR ownership.
- Added duplicate-evaluation suppression.
- Added typed creation, tuning, frame, and resource parameters.
- Added caching for eight retained model/tuning configurations.
- Added checked resource pointers through the Vulkan forwarder interface.
- Added history reset and checked teardown for Vulkan NR route changes.
- Retained Native/Present route guards and clear reporting that Present NR is unavailable on Vulkan in this build.
- Kept native Vulkan NR single-pass with Frame Hold unavailable.

### Overlay, usability, and localization

- Reorganized common and Neural Rendering controls into clearer top-level tabs and sections.
- Kept performance graphs and primary actions visible near the top of the overlay.
- Grouped route, model, tuning, Multipass, bridge, and diagnostic controls more coherently.
- Fixed first-open horizontal growth and stabilized dragged menu height.
- Anchored the initial menu to the upper-right and made expanded sections grow downward.
- Added text wrapping that accounts for columns and nested-section indentation.
- Disambiguated duplicated widget/control identifiers.
- Added clearer route descriptions, active-state messages, tooltips, cost warnings, mixed-value notices, and reset behavior.
- Added stronger selected route/pass tab styling and easier Multipass navigation.
- Moved NR Enable to the start of the workflow and hid inactive Native-only mode controls while a Present route is selected.
- Added concise Present status and dimension reporting with Advanced Data and Diagnostics collapsed until requested.
- Added fresh-profile defaults that select Present Enhanced with Follow native resolution while leaving NR itself disabled.
- Added directly authored localization infrastructure and strings for English, Spanish, French, German, and Portuguese.
- Added localized dynamic values, graph labels, status text, and UTF-8 font/glyph handling across supported UI scales.

### Installation, update, packaging, and recovery

- Added a NeuRotic-branded customer Setup entry point and support engine.
- Added selection among `dxgi.dll`, `winmm.dll`, `version.dll`, `dbghelp.dll`, `d3d12.dll`, `wininet.dll`, `winhttp.dll`, `OptiScaler.asi`, and `OptiScaler.dll`.
- Added immediate installation when the selected proxy name is free.
- Added backed-up replacement, choose-another, and no-write cancellation for occupied ordinary proxy names.
- Added explicit DXGI choices for replacement, ReShade64 rename/compatibility, choosing another name, or canceling.
- Added encoding-preserving targeted `LoadReshade=true` edits only when the user explicitly selects the ReShade path.
- Added package inventory and hash verification before destination changes.
- Added exact recording of original names, hashes, and configuration bytes.
- Added atomic rollback across proxy replacement/rename, configuration edits, payload copies, and final verification.
- Added one-click standalone Restore generated into the installation backup.
- Added restore refusal when later runtime changes would otherwise be overwritten.
- Added retention of test-session configuration in an undo location when restoring original configuration bytes.
- Added exact fresh-install cleanup when no previous configuration existed.
- Preserved an existing private NVIDIA NR model and existing configuration during ordinary updates.
- Added a reviewed fresh-install profile with NR and Multipass disabled and file logging off by default.
- Added customer and Present Enhanced package builders and repeatable installer fixtures.

### Documentation, identity, and project maintenance

- Established the NeuRotic project identity and clearly separated it from official OptiScaler and the parent DLSS-NR fork.
- Added NeuRotic-specific README, installation guidance, release notes, configuration notes, feature handoffs, compatibility records, and recovery instructions.
- Clarified that GitHub automatic source archives are not compiled NeuRotic install packages.
- Documented the distinction between the proprietary `nvngx_dlssnr.dll` model and the included `nvngx.dll_dlssnr.dll` forwarder.
- Added retained provenance for experiments, parent commits, branches, builds, binaries, configuration, installer behavior, test evidence, limitations, and runtime gates.

### Automated verification added by NeuRotic

- Added focused D3D12 WARP tests for GPU completion, replay, retirement, cancellation, capture resize, failure, and device loss.
- Added guide-copy, matching, subrect, depth/stencil, queue, and capacity tests for Present Enhanced.
- Added production configuration migration, save/load, precedence, concurrency, and snapshot tests.
- Added Present resolution and per-axis scaling tests.
- Added real conversion-shader delayed-submission and pixel round-trip tests.
- Added forwarder lifecycle, restart, origin, motion, jitter, reset, and legacy-interface tests.
- Added Multipass display, composition-pool, second-layer, ten-pass, pending-edit, and configuration-isolation tests.
- Added readiness, toggle-burst, dispatch-resource, exposure-guard, exposure-scan, and robustness tests.
- Added Vulkan frame-parameter, tuning, RR-routing, resource, and lifecycle tests.
- Added UI contract, integration contract, localization catalog, dynamic-value, widget-ID, glyph, and multi-scale tests.
- Added full installer fixtures covering all nine proxy names, Unicode paths, multiple INI encodings, conflicts, cancellation, tampering, forced failures, rollback, update, restore, and changed-runtime refusal.

## Important boundaries

The inventory above describes changes present in the source ancestry. It does not mean every route has equal runtime coverage or works in every game.

- Present Enhanced allows experimental Frame Generation, Ray Reconstruction, NR Multipass, and DX11 attempts; exposure is not validation, and every frame still has to pass the real guide/resource checks.
- D3D12 SDR with Frame Generation, Ray Reconstruction, and Multipass off remains the named comparison control rather than a mandatory configuration.
- DX11 attempts depend on the existing shared D3D12 bridge and support only admitted swapchain formats, device relationships, queue ordering, resources, and synchronization paths.
- HDR Present Enhanced remains unavailable because the current Present resource path requires SDR.
- Vulkan remains native single-pass in this build and does not support the Present routes.
- Multipass pass counts above two become progressively more experimental and expensive.
- The private NVIDIA Feature 18 model is not included.
- Combined image-quality, sustained-session, transition, and cross-game evidence remains narrower than the offline test matrix.

## Valuable experiments not included in this comparison target

The workspace contains other branches and retained experiments, but they are not ancestors of checkpoint `a57e4456` and should not be advertised as part of it. Notable exclusions include the later screenshot-capture experiments, independent MFG unlock work, separate Crimson frame-generation diagnostics/hardening, and other unmerged research branches.

## Credits

NeuRotic builds on [official OptiScaler](https://github.com/optiscaler/OptiScaler) and the [OptiScaler DLSS-NR fork](https://github.com/Dagherbou/OptiScaler_DLSSNR). I am grateful to their contributors for creating and sharing the foundation that makes this work possible. Their licenses, attribution, and original contributions remain an essential part of NeuRotic.

Special thanks to Tommy Creo for serving as a tester on the Vulkan issues and helping turn a difficult game-specific report into a concrete fix path.
