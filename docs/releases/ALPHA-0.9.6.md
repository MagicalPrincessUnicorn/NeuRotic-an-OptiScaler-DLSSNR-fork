# NeuRotic Alpha 0.9.6 — Release Notes

> **Release status:** Experimental Alpha
>
> NeuRotic modifies live rendering pipelines. Compatibility and results vary by game, GPU, driver, graphics API, display mode, DLSS files, and other graphics tools.

## A Note from the Creator

Hi again, everyone.

NeuRotic Alpha 0.9.6 is here.

First, thank you to everyone who downloaded, tested, and shared feedback on Alpha 0.9.5. I also want to apologize because, in hindsight, I feel I released the new rendering modes before they were ready. Both Present modes turned out not to be in the state I believed they were in when I released them.

Going forward, I will give major rendering changes more time for testing and validation before including them in a public release. Your feedback on Alpha 0.9.5 helped me understand where more work was needed, and much of Alpha 0.9.6 is the direct result of what we learned.

This release addresses those issues while also bringing substantial improvements to performance, compatibility, stability, installation, and everyday usability. It became a much larger release than I initially expected, and I hope you see a meaningful difference—not only in how NeuRotic performs, but in how quickly the project is beginning to take shape.

My goal remains to make Neural Rendering practical in real games. Better visuals matter, but those improvements must come with performance that allows people to actually play. Not everyone owns an RTX 5090—or even a 50-series card—and maximum image quality means very little if the game cannot run well enough to enjoy it.

Even at this early Alpha stage, I firmly believe NeuRotic has achieved the core of that goal in the configurations where it has been successfully tested: improving Neural Rendering visuals and effectiveness while also increasing performance.

There is still plenty to do, but Alpha 0.9.6 gives NeuRotic a much stronger foundation for everything that comes next.

Thank you for trying it. I hope you enjoy the release.

— Espiownage

---

## Table of Contents

- [Release overview](#release-overview)
- [Feature status at a glance](#feature-status-at-a-glance)
- [Read this first: Experimental Mode](#read-this-first-experimental-mode)
- [Rendering and compatibility](#rendering-and-compatibility)
  - [Present Compatibility](#present-compatibility)
  - [Present Enhanced](#present-enhanced)
  - [Present and Frame Generation recovery](#present-and-frame-generation-recovery)
  - [Native Temporal](#native-temporal)
  - [DirectX 11 through the DirectX 12 bridge](#directx-11-through-the-directx-12-bridge)
  - [Experimental HDR handling](#experimental-hdr-handling)
  - [Frame Generation and Ray Reconstruction](#frame-generation-and-ray-reconstruction)
  - [Resolution and workload controls](#resolution-and-workload-controls)
  - [Multipass Neural Rendering](#multipass-neural-rendering)
  - [Comparison screenshots](#comparison-screenshots)
- [Interface and usability](#interface-and-usability)
  - [Neural Rendering Advisor](#neural-rendering-advisor)
  - [DLSS Neural Rendering window](#dlss-neural-rendering-window)
  - [Basic and Advanced Multipass](#basic-and-advanced-multipass)
  - [Experimental Options](#experimental-options)
  - [OptiClip](#opticlip)
  - [Input, brightness, and localization](#input-brightness-and-localization)
- [Performance, stability, and safeguards](#performance-stability-and-safeguards)
- [Configuration and upgrade behavior](#configuration-and-upgrade-behavior)
- [Installation, updating, and uninstallation](#installation-updating-and-uninstallation)
  - [Required NVIDIA model](#required-nvidia-model)
  - [Installer safeguards](#installer-safeguards)
  - [Uninstalling NeuRotic](#uninstalling-neurotic)
- [Known limitations](#known-limitations)
- [Roadmap](#roadmap)
- [Before reporting a bug](#before-reporting-a-bug)
- [Support NeuRotic](#support-neurotic)
- [Special thanks and credits](#special-thanks-and-credits)

---

## Release Overview

Alpha 0.9.6 strengthens the rendering foundation introduced in Alpha 0.9.5 and combines the work that followed into one release line.

The largest additions and changes are:

- A prominent Experimental Mode for deliberately unlocking implemented but less-tested features while hard safety blocks remain in force.
- A clearer Neural Rendering Advisor with Before/After analysis, faster initial feedback, and direct instructions for recognized experimental prerequisites.
- Major corrections to Present Compatibility and Present Enhanced.
- Stronger Present behavior around overlays, dialogue, menus, focus changes, and Frame Generation transitions.
- DirectX 11 Native Temporal and Present support through a synchronized DirectX 12 bridge.
- Experimental HDR detection, state tracking, and guarded Present access.
- A new opt-in Experimental Options system.
- The Neural Rendering Advisor.
- A redesigned DLSS Neural Rendering interface.
- Basic and Advanced Multipass controls.
- Simplified resolution and workload controls.
- Matched comparison screenshots with embedded provenance.
- An optional experimental camera-cut preservation mode.
- Independent gameplay-input controls while the interface is open.
- Adjustable interface brightness.
- OptiClip, NeuRotic's Clippy-inspired assistant.
- A complete Setup and Uninstall experience with rollback and recovery safeguards.

---

## Feature Status at a Glance

| Feature | Alpha 0.9.6 status |
| --- | --- |
| Experimental Mode | Included; gateway to implemented but less-tested paths |
| Native Temporal | Available; compatibility remains game-dependent |
| Present Compatibility | Available; substantially revised since 0.9.5 |
| Present Enhanced | Available; **Experimental** |
| DirectX 12 | Primary and most developed foundation |
| DirectX 11 through the D3D12 bridge | Included; **Experimental and game-dependent** |
| HDR observation and guarded Present attempts | Included; **Experimental** |
| Multipass | Available; higher pass counts remain **Experimental** |
| Neural Rendering Advisor | Included; Before/After scope and actionable experimental guidance |
| DLSS Neural Rendering window redesign | Included |
| Experimental camera-cut preservation | Included; off by default |
| Comparison screenshots | Included for supported routes |
| Vulkan Native path | Limited; requires more testing and development |
| Vulkan Present modes | Not ready |
| Anti-cheat compatibility | Separate problem; not guaranteed |

---

# Read This First: Experimental Mode

Experimental Mode is the gateway to many of Alpha 0.9.6's newest and least-established features. It is also one of the first things to check if Neural Rendering says **BLOCKED**, refuses to start, or falls back to the original game image.

NeuRotic's normal out-of-box behavior is intentionally conservative. Some implemented combinations involving Present Enhanced, Frame Generation, HDR, Multipass, DirectX 11, and other newer paths are withheld until the related experimental override is deliberately enabled. Ray Reconstruction has its own route and placement restrictions, and combinations NeuRotic cannot safely support may remain unavailable even after Experimental Mode is enabled.

## How to Unlock an Experimental Feature

1. Open the **Neural Rendering** page.
2. Scroll below Multipass to **Neural Rendering — Experimental Overrides**.
3. Enable **Unlock Experimental Mode**.
4. Read and accept the warning.
5. Enable only the override needed for the feature you want to test.
6. Select **Save experimental settings** or use the global **Save Settings** button. An unsaved checkbox change is not an active override.
7. Read any BLOCKED, waiting, or fallback explanation shown by NeuRotic. When the Advisor recognizes a supported experimental prerequisite, it names the setting that must be enabled and saved.

If Neural Rendering still refuses to run, the interface should explain why. Experimental Mode does not blindly force every path. Device, format, guide, synchronization, ownership, completion, route, and resource checks remain mandatory.

## What Experimental Mode Does

- Unlocks implemented features and combinations that need more real-world testing.
- Lets experienced or adventurous users try newer NeuRotic work before it is ready for the default experience.
- Keeps the normal installation safer and less confusing for users who only want a straightforward setup.
- Makes the additional risk explicit before experimental settings are applied.
- Automatically disables the experimental master switch and all overrides after an unclean session.

## What Experimental Mode Does Not Do

- It does not promise that an unlocked combination will work in a particular game.
- It does not bypass missing resources or unsupported rendering contracts.
- It does not disable NeuRotic's hard GPU-resource safety checks.
- It does not unlock paths associated with known GPU-resource corruption or another confirmed hard failure.
- It does not turn an unsupported Ray Reconstruction placement, Vulkan Present route, or incompatible provider path into a supported one.

> **Personal commentary:** I patched, corrected, or isolated every issue I could find and reproduce on my own system. I also put Alpha 0.9.6 through as many hardening and validation passes as I reasonably could. I believe NeuRotic has received unusually thorough hardening for a project at this stage, and I am very proud of that.
>
> At the same time, I am one person with one computer. I cannot test every game, GPU, driver, display, or combination of Frame Generation, Ray Reconstruction, HDR, Multipass, and third-party graphics tools. My honest answer is that you may still find bugs. If you do, I am sorry. I tried my best to catch and contain as much as possible, but this is exactly why NeuRotic remains an Alpha.
>
> Experimental Mode is meant to protect the normal out-of-box experience while still giving interested users access to the frontier work I have already put time into. Some of those paths may work beautifully. Others may crash, behave strangely, or still have a few kinks to work out. What it will not do is unlock something I already know can enter an unsafe GPU-resource state or another confirmed hard failure. Those paths remain locked while I work on them for a future release. I apologize for the inconvenience, but I hope you appreciate that I am trying to keep the default experience safe.

---

# Rendering and Compatibility

## Present Compatibility

Present Compatibility processes the completed presented image without relying on the game's Native depth and motion guides. Earlier development material and some internal status messages may call this route Present Image-Only; **Present Compatibility** is its current user-facing name.

Alpha 0.9.6 substantially revises this route following the problems identified in Alpha 0.9.5. Changes include:

- Improved final-image capture and processing.
- Corrected GPU-resource ownership and lifetime handling.
- Better temporal-history continuity and invalidation.
- Safer handling of route changes, resizing, focus changes, interruptions, and failed Present operations.
- Improved SDR format handling.
- More useful compatibility and fallback reporting.
- Better synchronization around Frame Generation boundaries.
- Route-specific workload and resolution controls.
- DirectX 11 bridge support in compatible games.
- Original-image fallback whenever NeuRotic cannot prove that a processed frame is safe to return.
- Recovery across supported graphics and compute Frame Generation handoffs.
- Corrected behavior when Frame Generation turns off during dialogue, menu, or overlay transitions.

> **Personal commentary:** Present Compatibility was one of the modes I released too quickly in Alpha 0.9.5. A considerable amount of Alpha 0.9.6's development time went into rebuilding its assumptions, synchronization, and fallback behavior instead of merely hiding the problems behind additional restrictions.

## Present Enhanced

Present Enhanced processes the final presented image while attempting to use a freshly matched pair of Native depth and motion guides.

This route is intended to provide more useful temporal information than Present Compatibility while keeping the game's HUD in the processed output.

Alpha 0.9.6 adds or improves:

- Bounded capture of compatible Native depth and motion guides.
- Matching based on frame, swapchain, backbuffer, output size, queue submission, provider identity, and resource generation.
- Independent handling of depth, motion, render, and output dimensions.
- Forwarding of motion scale, jitter, depth direction, reset state, and guide subrect origins.
- Automatic rejection of missing, stale, ambiguous, differently queued, mismatched, out-of-bounds, or unsupported guides.
- History resets when route, workload, Frame Generation, Ray Reconstruction, or Multipass conditions change.
- Original-image fallback when the required guide relationship cannot be trusted.
- Experimental access to combinations that were blocked outright in Alpha 0.9.5.
- DirectX 11 guide matching through the shared bridge when the complete resource contract is compatible.
- Fresh-state requirements following resize, swapchain recreation, HDR descriptor changes, and provider transitions.

Present Enhanced remains **Experimental**. The HUD is retained in the final image, but scene depth and motion guides do not describe individual HUD pixels.

> **Personal commentary:** Alpha 0.9.5 attempted to protect users by broadly blocking combinations I had not validated. Those restrictions served a purpose, but they also prevented people from trying combinations that might work. Alpha 0.9.6 performs the actual resource and guide checks first, while Experimental Options allow uncertain combinations to be tested deliberately.

## Present and Frame Generation Recovery

Alpha 0.9.6 includes substantial work on the relationship between Present Neural Rendering, native Frame Generation, overlays, dialogue, and menu transitions.

The release now:

- Associates Present captures with the active provider only while Frame Generation is actually active.
- Uses ordered Present intervals when Frame Generation is not active.
- Preserves unfinished GPU dependencies during invalidation and recovery.
- Supports verified graphics and compute Frame Generation command-list handoffs.
- Waits for compatible native queue ownership before returning new processed output.
- Suspends Neural Rendering output on unsupported paths while allowing ordinary frames to continue.
- Requires fresh readiness after incompatible provider, queue, swapchain, resize, or HDR descriptor changes.
- Retires completed dependencies without discarding work that is still owned by the GPU.
- Keeps Native Temporal isolated from Present-only display-recovery behavior.
- Corrects the persistent Present dropout previously observed after some Dragon's Dogma 2 dialogue and overlay transitions.

The accepted Dragon's Dogma 2 component test continued gameplay instead of freezing. A very brief original-image flash can still appear while Present Neural Rendering safely requalifies after a transition.

This release does not add a fade, reuse stale output, or guess that an earlier GPU state is still compatible simply because it looks similar.

## Native Temporal

Native Temporal continues to use the game's original temporal inputs and native Neural Rendering placement.

Alpha 0.9.6 improves:

- Native input-size and render-subrect handling.
- Independent depth and motion origins.
- Low- and high-resolution motion-vector dimensions.
- Refusal of out-of-bounds guide contracts.
- Selection of the exact wrapped DLSS backend.
- Preservation of feature-owned creation flags.
- Placement changes between Pre-SR and Post-SR operation.
- Resolution-change recovery.
- Temporal-history resets.
- Model-session retirement and replacement.
- Native Ray Reconstruction awareness.
- DirectX 11 transport.
- Active, waiting, fallback, and failure reporting.
- Route-specific GPU timing and delivery diagnostics.

Performance-oriented placement can run Neural Rendering before the game's final Super Resolution stage, reducing the model's pixel workload while retaining the game's Native temporal information.

> **Personal commentary:** Native Temporal was already the most established of NeuRotic's rendering routes, but Alpha 0.9.6 still includes several smaller and important improvements. I tightened how it handles the game's original inputs, guide dimensions and origins, route and resolution changes, and incompatible data. The goal was to improve the route without changing what makes Native Temporal useful in the first place.

### Preserve NR During Camera Cuts

Alpha 0.9.6 includes an optional experimental **Preserve NR During Camera Cuts** setting for compatible Native DirectX 12 Pre-SR operation.

When enabled, reset-only camera-cut bursts can retain the existing Neural Rendering session while still discarding old-scene temporal history. Structural changes—including incompatible resources, format or size changes, feature replacement, device transitions, or failures—continue to use conservative reset behavior.

This option:

- Is off by default.
- Requires Experimental Mode.
- Does not apply to every route or API.
- Keeps hard readiness and resource checks in force.
- Remains experimental pending wider scene-transition testing.

## DirectX 11 Through the DirectX 12 Bridge

Alpha 0.9.6 introduces DirectX 11 Neural Rendering support through a synchronized DirectX 12 bridge.

The bridge transports compatible DirectX 11 resources into NeuRotic's existing DirectX 12 Neural Rendering backend and returns the processed result to the original pipeline.

This work includes:

- Native DirectX 11 input and output transport.
- Support for independently sized depth, motion, render, and output regions.
- Pre-SR and Post-SR Native Temporal paths.
- DirectX 11 Present Compatibility processing.
- Experimental DirectX 11 Present Enhanced guide matching.
- Correct restoration of game-owned resources and parameters.
- Completion-aware resource retirement.
- Resolution-change and placement-transition handling.
- Support for compatible SDR output formats.
- Padded high-resolution resource handling.
- Independent output-resolution motion-vector transport.

DirectX 11 compatibility remains game-dependent and should still be considered **Experimental**. The Native transport has positive evidence in Baldur's Gate 3 and Satisfactory configurations, but that does not guarantee every DirectX 11 game or route.

> **Personal commentary:** DirectX 11 support is an important expansion for NeuRotic, but it is not a promise that every DirectX 11 game will immediately work. Please report which game, route, resolution policy, and graphics features you were using if you encounter a problem.

## Experimental HDR Handling

Alpha 0.9.6 includes the first stage of NeuRotic's HDR compatibility work.

Previous behavior relied too heavily on inferred, process-wide HDR state. The new path observes each real swapchain independently and records only successful color-space and resize changes.

It can distinguish observed:

- SDR.
- HDR10/PQ.
- scRGB.
- HLG.
- Rec.2020 gamma.
- Other or unknown color spaces.

The implementation also:

- Preserves the last successful color interpretation when a color-space request fails.
- Tracks format and successful resize generations.
- Keeps separate state for multiple swapchains.
- Removes state when a swapchain is destroyed so a reused pointer cannot inherit old HDR information.
- Records bounded metadata evidence without modifying the game's metadata payload.
- Uses a copied descriptor for each Present operation.
- Returns the original image during an active resize transition.
- Requires fresh readiness after color round trips, successful resize, or swapchain recreation.
- Preserves pending GPU ownership when older readiness becomes invalid.
- Avoids continually resetting readiness for metadata-only updates, failed color-space calls, or repeated successful calls that do not change the descriptor.

The HDR component was reported working well in Monster Hunter Wilds and Baldur's Gate 3, including a Monster Hunter Wilds Frame Generation run. HDR remains **Experimental** and requires the appropriate saved override.

This is not a universal HDR implementation or a color-calibration claim. Alpha 0.9.6 does not add a new PQ decoder, gamut converter, tone mapper, forced color space, automatic exposure rule, brightness correction, or game-image brightness slider.

## Frame Generation and Ray Reconstruction

Alpha 0.9.6 improves NeuRotic's awareness of native Frame Generation and Ray Reconstruction ownership.

Changes include:

- Better identification of real-frame boundaries.
- Improved synchronization before native DLSS Frame Generation.
- Preservation of Frame Generation inputs when NeuRotic refuses a frame.
- Safer retirement of submitted work.
- Corrected behavior when Neural Rendering is turned off while Frame Generation remains active.
- Support for compatible graphics and compute Frame Generation command-list paths.
- Suspension rather than repeated unsafe probing on unsupported paths.
- Ray Reconstruction-aware placement decisions.
- Explicit handling of logical sizes, subrects, and DLAA transitions.
- History invalidation when Frame Generation or Ray Reconstruction conditions change.
- Removal of several blanket compatibility blocks in favor of actual resource checks.
- Improved provider, queue, frame, and swapchain identity tracking.

Unverified combinations may require **Experimental Options**. A combination being accessible does not mean it has been validated in every game.

## Resolution and Workload Controls

NeuRotic is designed around practical performance, not simply running every effect at the maximum possible resolution.

Alpha 0.9.6 provides shared, human-readable resolution choices:

- **Match Game Render — Recommended**
- **Always Full Output**
- **Manual — Advanced / Low-end**

Manual scaling supports a broad 25–200% range. Very low values can remove fine detail or make the Neural Rendering effect difficult to see; values above full resolution increase cost substantially.

The interface reports the actual Neural Rendering and output dimensions so users can understand the performance tradeoff they are making.

Additional improvements include:

- Preservation of existing explicit user settings during upgrades.
- Safe resource rebuilding when effective dimensions change.
- Correct motion and jitter scaling at reduced workloads.
- Fallback while required Native-size information is unavailable.
- Placement-aware refusal of resolution choices that cannot be represented safely.
- Retention of explicit legacy profiles instead of silently replacing them.
- Shared resolution choices between the interface and Advisor.

Maximum resolution is an option—not the definition of success.

## Multipass Neural Rendering

NeuRotic supports as many as ten Neural Rendering passes.

Additional passes can maintain their own:

- Model session.
- Resolution and scaling behavior.
- Temporal history.
- Model preset and style.
- Model and detail strength.
- Composition settings.
- Skin and highlight controls.
- Failure and rebuild state.

Alpha 0.9.6 also improves:

- Replay-safe resource allocation.
- Motion scaling between passes.
- Final presentation after the selected pass chain.
- Fallback when later passes cannot be admitted safely.
- Shared editing of selected additional passes.
- Preservation of Pass 1 as an independent foundation.
- Clearer pass navigation and active-state styling.
- Safer cancellation of stale profile edits.
- More approachable Basic-mode controls.
- Advisor behavior that temporarily returns analysis to one pass and restores the original Multipass configuration afterward.

Two passes are the recommended starting point. Three and four are advanced configurations. Five through ten should be treated as **Experimental** and may have a substantial performance cost.

> **Personal commentary:** Multipass takes frames. There is no way around that. I have added performance controls to make it more practical, but it is still something you should tune around your own game and hardware. I really like how it turned out, so please give it a try.

## Comparison Screenshots

Alpha 0.9.6 includes tools for capturing matched Neural Rendering comparisons on supported routes.

Changes include:

- Capturing the final output without the NeuRotic interface obscuring the image.
- Matched naming for original and Neural Rendering images.
- Route-aware capture availability.
- Preservation of the selected scene when generating a comparison.
- Atomic screenshot batches that do not remove older user captures.
- PNG-only output without separate required sidecar files.
- Embedded, versioned `NeuRotic.CaptureManifest` information inside generated PNG files.
- Version, route, configuration, and capture provenance for easier comparison and bug reporting.
- Cleanup limited to files owned by a failed new batch.
- Preservation of existing images and older manifests.

DirectX 11 supports active Present Compatibility and Present Enhanced comparisons. Native Temporal and Neural Rendering-off comparisons currently require DirectX 12.

---

# Interface and Usability

## Neural Rendering Advisor

The **Neural Rendering Advisor** helps users choose an appropriate NeuRotic configuration for the current game.

Alpha 0.9.6 now gives the Advisor its own rendering-stage selector:

- **After upscaling** is the default and exposes Native Temporal, Present Compatibility, and Present Enhanced when their requirements can be met.
- **Before upscaling** focuses on Native Temporal because the Present routes operate on the final presented image.
- The Advisor's stage choice is temporary analysis scope. It does not silently change the user's normal saved rendering configuration.

The Advisor can consider:

- Graphics API and rendering path.
- Detected graphics hardware.
- Output dimensions and format.
- Depth and motion-guide availability.
- Guide-matching confidence.
- Frame Generation state and provider information.
- Observed frame cadence.
- Neural Rendering GPU cost.
- The user's target performance.
- The user's preferred Quality, Balanced, or Performance goal.

**Analyze All Routes** tests each compatible route in sequence. **Test This Route** measures one selected route. Unsupported routes are skipped with an explanation.

When a route cannot begin, the Advisor now provides useful initial feedback after no more than five seconds. A current rendering-policy refusal can appear immediately without waiting for that timeout. The normal warmup and measurement periods remain intact so a quick error message does not become a rushed or misleading performance result.

For Present Enhanced with Frame Generation, the route card can now name the exact missing prerequisites: **Unlock Experimental Mode**, **Override FG Guardrails**, and **Save experimental settings**. The Advisor never enables or saves an experimental option automatically. Unsaved draft choices do not count as active settings.

Present Compatibility and Native Temporal do not inherit Present Enhanced's Frame Generation override requirement. HDR guidance is shown only for the implemented compatible 10-bit PQ path; other HDR formats, missing guides, unsupported providers, device problems, and unverified timing retain their own explanations instead of being mislabeled as something Experimental Mode will fix.

Advisor trials:

- Use the selected resolution preference.
- Apply the chosen Before/After scope consistently during preflight and measurement.
- Temporarily reduce the analysis to one pass.
- Separate frame cadence from Neural Rendering GPU cost.
- Require fresh, route-specific measurements.
- Reject stale or unknown cadence.
- Refuse stalled or incomplete samples.
- Avoid claiming a performance target was met when it was not.
- Report **Best of tested routes** when complete route coverage is unavailable.
- Restore the user's original settings after completion, cancellation, resize, shutdown, menu closure, or another interruption.
- Restore temporary placement and resolution hints as well as the visible route settings.

Changing rendering routes may still incur a driver or model transition cost. Frame Generation settings alone do not prove that generated frames were actually presented, so the Advisor avoids claiming a verified presentation rate without an observed source.

> **Personal commentary:** Neural Rendering has a lot of choices, and not every combination makes sense for every game or GPU. The Advisor is meant to give people a useful starting point. You can still ignore its advice and experiment—I probably would—but it should make NeuRotic far easier to approach.

## DLSS Neural Rendering Window

The main **DLSS Neural Rendering** window has been reorganized to make the most important information easier to find.

Improvements include:

- Essential enable and Apply controls placed first.
- Clear active, waiting, fallback, paused, and failure states.
- Visible timing and working dimensions.
- Stage, method, and resolution controls arranged in pipeline order.
- Clear explanations for unavailable combinations.
- Neural Rendering sections that report ON, OFF, or BLOCKED state.
- Advanced diagnostics moved into collapsible areas.
- Improved control spacing and responsive layouts.
- Clearer tooltips and warnings.
- More consistent saved-setting behavior.
- Improved presentation across supported interface scales.
- Clear separation between ordinary settings and Experimental Overrides.

## Basic and Advanced Multipass

Alpha 0.9.6 divides Multipass configuration into two approachable levels.

### Basic Multipass

Set the **Maximum passes** limit, then use the shared **Model Strength** and **Detail Strength** sliders to adjust the selected additional passes with minimal clicking.

The sliders visually divide their range across the selected pass count. Releasing a slider applies the change. Pass 1 remains independent.

New installations begin in Basic Multipass mode.

### Advanced Multipass

Advanced mode retains:

- Individual pass tabs.
- Per-pass resolution controls.
- Independent model styles and presets.
- Per-pass strength and composition settings.
- Detailed child-pass diagnostics.
- Fine control over each layer in the stack.

## Experimental Options

Alpha 0.9.6 adds **Unlock Experimental Mode** beneath Multipass in the Neural Rendering page.

This mode exposes features or combinations that exist in NeuRotic but have not received enough testing to be enabled normally. Experimental categories include Multipass combinations, HDR overrides, Frame Generation combinations, and camera-cut reset behavior.

Experimental Mode includes several safeguards:

- Activation requires a warning confirmation.
- Changes do not apply until **Save experimental settings** or the global **Save Settings** succeeds.
- Closing the interface with unsaved changes discards them.
- An unclean previous session automatically disables the master switch and every experimental override on the next launch.
- Known unsafe resource combinations remain blocked even when an override is saved.

When Experimental Options are enabled, users should be prepared for:

- Crashes.
- Visual artifacts.
- Incorrect output.
- Inconsistent behavior.
- Features that do not work in a particular game.

The distinction is intentional:

- Untested or uncertain combinations may be unlockable.
- Confirmed unsafe combinations remain unavailable.

Please experiment—but save your game first.

## OptiClip

NeuRotic now includes **OptiClip**, its own Clippy-inspired assistant.

OptiClip appears in the lower-left interface footer beside **Send Coffee** and makes contextual comments about things happening inside NeuRotic. Many of his comments were written by me, while some were generated simply for fun.

His dialogue and avatar use separate click areas so their transparent space does not cover unrelated settings.

Does he perform an essential Neural Rendering function? No.

Is he here because someone dared me to add him? Yes.

OptiClip can be hidden from the General page, although doing so may hurt his feelings. Please make that decision responsibly.

## Input, Brightness, and Localization

### Independent gameplay input

The General page now provides separate controls for allowing mouse, keyboard, and controller input to continue reaching the game while the NeuRotic interface is open.

New-installation defaults are:

- Mouse blocked from the game.
- Keyboard allowed to the game.
- Controller allowed to the game.

Input-policy edits remain pending until **Save Input Settings** or global **Save Settings** succeeds. Closing the menu discards unsaved input-policy changes.

Allowed keyboard or controller input remains with gameplay and does not navigate the interface. Configured NeuRotic and OptiScaler shortcuts retain their independent handling.

Standard XInput and DirectInput paths are covered. Custom DirectInput formats, GameInput, Windows.Gaming.Input, and raw HID controllers may bypass this handling.

### Interface brightness

**Interface brightness** is available under Tools and changes only the NeuRotic interface colors. It does not alter the game image, HDR output, Neural Rendering processing, or screenshot pixels.

The setting defaults to `1x`, persists through Save Settings, and can reduce color contrast at unusually high values.

### Localization and layout

Alpha 0.9.6 includes:

- English, Spanish, French, German, and Portuguese interface catalogs.
- Corrected translated Advisor explanations.
- Responsive layouts across supported interface scales.
- Improved text wrapping.
- Stable control identities.
- Clearer selected, disabled, and experimental states.
- Support for both light and dark themes.

---

# Performance, Stability, and Safeguards

Alpha 0.9.6 includes extensive work intended to make NeuRotic's rendering paths safer and more predictable.

Highlights include:

- GPU-completion-aware resource reuse.
- Improved command-list and queue-submission tracking.
- Safer handling of repeated submissions.
- Completion-aware retirement of models, work surfaces, descriptors, uploads, readbacks, scalers, and timing resources.
- Bounded failure behavior when required resource capacity is unavailable.
- Device-identity validation.
- Transactional route and setting changes.
- Immutable settings during an active upscale operation.
- Safer Neural Rendering enable and disable transitions.
- Improved resolution and output-resize handling.
- Controlled shutdown and teardown behavior.
- Better protection against stale work after route, device, provider, swapchain, or configuration changes.
- Original-image fallback when NeuRotic cannot prove that a Present operation is safe.
- More precise failure reasons instead of silently producing questionable output.
- Per-swapchain readiness proof that cannot survive incompatible HDR descriptor or swapchain lifetimes.
- Preservation of unfinished GPU ownership even when an older readiness state is revoked.
- Safe suspension on unsupported Frame Generation paths instead of repeated failing submissions.
- Independent generation tracking for rendering routes, model sessions, resources, and submitted work.

These safeguards are intended to prevent NeuRotic from continuing through GPU states it cannot safely understand. They do not guarantee compatibility with every game or graphics modification.

---

# Configuration and Upgrade Behavior

- New installations begin with Neural Rendering, Multipass, and file logging off.
- New installations begin in Basic Multipass mode.
- The default DLSS Performance and Ultra Performance presets remain Preset L.
- Match Game Render is the recommended starting resolution policy for new profiles.
- Existing explicit settings are preserved during ordinary managed updates.
- Existing explicit route and Manual resolution values remain available.
- Present and Native placement restrictions are explained rather than silently rewritten.
- Experimental settings require an explicit save.
- An unclean session clears saved Experimental Mode activation on the next launch.
- Interface brightness defaults to `1x`.
- OptiClip is visible by default and may be hidden in General.
- ReShade coexistence changes only the required `LoadReshade` setting and preserves the INI's encoding.
- The installer preserves the user-supplied `nvngx_dlssnr.dll` model.

---

# Installation, Updating, and Uninstallation

Close the game before installing, updating, repairing, changing proxy, or uninstalling NeuRotic.

## Installing

1. Download and extract the complete Alpha 0.9.6 release package somewhere outside the game directory.
2. Run `NeuRotic-Setup.cmd`.
3. Select the game's actual executable rather than its launcher.
4. Select the appropriate proxy filename when prompted.
5. Read any detected-file decision carefully. Setup never guesses who owns an existing DLL.
6. Wait for **full installation verified**.
7. Make sure a legitimate `nvngx_dlssnr.dll` is available beside the game executable.
8. Launch the game and press `Insert` to open NeuRotic unless another shortcut is saved.

Rerunning Setup on a managed installation offers Update/Repair, Change Proxy, Uninstall, or Cancel. Updates reuse the recorded proxy automatically.

## Required NVIDIA Model

NeuRotic does not distribute NVIDIA's proprietary `nvngx_dlssnr.dll` model.

The included `nvngx.dll_dlssnr.dll` is NeuRotic's forwarder and is not the same file. Neural Rendering requires both.

Obtain a genuine NVIDIA-signed copy from a game installation or NVIDIA driver package that legitimately contains it, then place it beside the game executable.

The third-party [DLSS Version Toolkit](https://github.com/scubamount/dlss-version-toolkit) may help locate, inspect, and manage NVIDIA DLLs already available on your computer. It is not affiliated with NeuRotic, and availability of the Neural Rendering model through external services is not guaranteed.

Avoid downloading proprietary DLLs from random mirrors.

## Installer Safeguards

The Alpha 0.9.6 installer includes:

- Package and file verification.
- Automatic backups of replaced files.
- Preservation of existing NeuRotic settings during ordinary updates.
- Preservation of the user-supplied NVIDIA model.
- Recorded proxy selection, release, install time, and restore history.
- Explicit ReShade coexistence handling.
- Automatic rollback when an installation cannot be completed.
- Recovery from interrupted operations.
- Protection against overwriting files changed after installation.
- Support for repeated updates and proxy changes.
- Repair of a missing managed loader.
- Adoption and recovery of supported older NeuRotic installation records.
- Long-path and nested-game-directory recovery.
- Refusal to follow unsafe linked paths outside the selected game directory.
- Refusal to proceed while an earlier recovery or cleanup operation is unresolved.
- Checks that recorded files still match before restoring or removing them.

Supported proxy choices include:

- `dxgi.dll`
- `winmm.dll`
- `version.dll`
- `dbghelp.dll`
- `d3d12.dll`
- `wininet.dll`
- `winhttp.dll`
- `OptiScaler.asi`
- `OptiScaler.dll`

DirectX games commonly use `dxgi.dll`. Vulkan games commonly use `winmm.dll`.

When an occupied `dxgi.dll` is confirmed by the user to be ReShade, Setup can preserve it as `ReShade64.dll` and enable NeuRotic's ReShade compatibility setting. If `ReShade64.dll` already exists, Setup stops rather than overwriting it.

The installer will not assume an occupied DLL belongs to ReShade or another known tool simply because of its filename.

## Uninstalling NeuRotic

Run either:

- `NeuRotic-Uninstall.cmd` from the downloaded package, or
- `Uninstall NeuRotic.cmd` from the game directory.

The game-directory copy already knows which installation it belongs to. The downloaded copy asks you to select the same game executable used during Setup.

The uninstaller offers:

- **Keep Settings — Recommended**
- **Remove Settings**
- **Full Cleanup**

Keep Settings restores the game's recorded pre-install state while retaining the current NeuRotic configuration under `NeuRotic\UserData` for a future reinstall.

Remove Settings also discards the saved NeuRotic configuration. Full Cleanup additionally removes recorded installer history and verified recovery data. User-added files, modified backup files, and nonempty unrelated folders are preserved.

The user-supplied NVIDIA model is preserved unless its removal is requested separately and explicitly confirmed.

If an uninstall is interrupted, run the uninstaller again and choose recovery first. Keep its recovery folders until recovery completes.

You are completely free to uninstall NeuRotic.

I will take it personally—but only jokingly.

Mostly.

---

# Known Limitations

- NeuRotic remains experimental rendering middleware.
- Present Enhanced remains experimental.
- DirectX 11 compatibility varies between games.
- HDR support is experimental and is not a guarantee of universal HDR formats or calibrated color accuracy.
- Frame Generation, Ray Reconstruction, HDR, and Multipass combinations may require Experimental Mode.
- A brief original-image flash can occur while Present routes safely recover after some Frame Generation, overlay, or dialogue transitions.
- Higher Multipass counts can carry a substantial performance cost.
- The optional camera-cut preservation path is experimental, off by default, and limited to compatible Native DirectX 12 Pre-SR operation.
- Vulkan has received less testing and implementation attention than DirectX.
- Vulkan Present processing is not currently ready.
- Native Temporal comparison reference images may appear darker than expected because the reference display conversion is not yet correct.
- Performance screenshot trials use fresh temporary history and do not reproduce accumulated live temporal history.
- Neural Rendering Advisor route changes can incur driver or model transition delays.
- Native Temporal After-upscaling analysis requires Full Output or Manual resolution; Before-upscaling analysis requires Match Game Render or Manual. Experimental Mode does not bypass these placement limits.
- Comparison-screenshot support depends on the active API and rendering route.
- Some uncommon controller systems may bypass menu input handling.
- Linux/Proton runtime compatibility has been reported by users, but the supplied Setup and Uninstall tools are for Windows.
- Anti-cheat systems may prevent NeuRotic or similar middleware from loading. Anti-cheat compatibility is a separate problem and is not guaranteed.
- No single game result should be treated as proof of universal compatibility.

---

# Roadmap

After Alpha 0.9.6, development will continue in several areas:

- Continued strengthening of the existing DirectX rendering foundation.
- Additional Vulkan implementation and testing.
- A future Vulkan Present adapter.
- Broader HDR format and color-pipeline compatibility.
- Broader Ray Reconstruction compatibility.
- More Frame Generation combinations and transition testing.
- Smoother Present recovery without weakening resource ownership or reusing stale images.
- Expanded DirectX 11 game coverage.
- Continued Present Enhanced guide-matching improvements.
- Further Neural Rendering Advisor refinement.
- More game-specific compatibility work.
- Continued performance tuning.
- Interface cleanup and accessibility improvements.
- Additional diagnostics and comparison tools.
- Continued refinement of NeuRotic's broader rendering framework.

Vulkan is an important direction for the project, but I want to establish a dependable DirectX foundation before expanding too aggressively into paths I currently have fewer games available to test.

---

# Before Reporting a Bug

Please include:

- The game and exact game version when known.
- Graphics API.
- GPU and driver version.
- NeuRotic Alpha version.
- Selected Neural Rendering route and placement.
- Resolution policy.
- Frame Generation and Ray Reconstruction state.
- HDR state and display mode when relevant.
- Multipass count.
- Whether Experimental Mode and any overrides were enabled.
- `OptiScaler.log` captured after reproducing the issue.
- Screenshots or a short video when the problem is visual.
- Steps that reliably reproduce the issue.

Most importantly, ask:

**Does the same problem still happen when NeuRotic is completely turned off—or when Neural Rendering is turned off?**

If the answer is yes, please do not report it as a NeuRotic bug. It is not being caused by NeuRotic.

For NeuRotic-specific problems, ideas, major bugs, and unusual compatibility results, join the [RenoDX Discord](https://discord.gg/Ce9bQHQrSV) and let me know how the release is holding up.

---

# Support NeuRotic

NeuRotic has taken a considerable amount of personal time, testing, and development work to build. I have also personally covered the project's development costs this month.

If you enjoy the project and would like to help support its continued development and longevity, you can:

- [Support me on Ko-fi](https://ko-fi.com/espiownage)
- [Star the NeuRotic repository](https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork)
- Share NeuRotic with someone who may enjoy it
- Test the release and provide useful feedback
- Join the [RenoDX Discord](https://discord.gg/Ce9bQHQrSV)

Any support is deeply appreciated, but never a requirement.

---

# Special Thanks and Credits

Special thanks to:

- The OptiScaler maintainers and contributors.
- Dagherbou and the contributors to the OptiScaler DLSS-NR fork.
- **Tommy Creo**, for testing Vulkan issues and helping investigate a particularly stubborn problem.
- **JuanTacos**, for testing an unusual scene-transition bug and helping isolate its conditions.
- The RenoDX community.
- Everyone who has tested NeuRotic, collected logs, recorded videos, submitted screenshots, shared ideas, or reported bugs.
- The developers and maintainers of the open-source projects and dependencies that make this work possible.

NeuRotic is a forked branch of [OptiScaler DLSS-NR](https://github.com/Dagherbou/OptiScaler_DLSSNR), which is built on the work of the official [OptiScaler project](https://github.com/optiscaler/OptiScaler).

Please refer to [LICENSE](../../LICENSE), the included license files, and the upstream projects for their respective licensing and attribution requirements.
