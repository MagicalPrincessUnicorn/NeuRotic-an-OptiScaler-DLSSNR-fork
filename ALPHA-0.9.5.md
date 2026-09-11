# NeuRotic Alpha 0.9.5 — Present Enhanced

> **Release-note bundle:** `NR-BUNDLE-ALPHA-095-A57E4456-R01`
> **Implementation:** `a57e445604e3aa42c32d92908074ef0b565f04b6`

## A Message From Me

Hi everyone. Thanks for checking out NeuRotic and for giving it a try. Seeing people get good results from it—and hearing where it still needs work—has been genuinely motivating.

This release is built around the thing I care about most with Neural Rendering: **better visuals without treating performance as an afterthought**. OptiScaler and its DLSS-NR fork gave NeuRotic a strong foundation; Alpha 0.9.5 builds on that work with more ways to pursue a stronger image while deciding exactly how much workload you are willing to spend getting there.

That is the point of the new Present routes, the resolution controls, and the expanded Multipass stack. I want it to be easier to push for more clarity, stronger detail, and a more convincing final image without being locked into one vague quality preset or one expensive all-or-nothing setting. The right balance will still depend on the game, your hardware, and the scene in front of you—but NeuRotic should now give you far more useful ways to find it.

Present Enhanced is the part I am most excited about. It is designed to work on the final presented image while still using the Native temporal information that makes NR decisions meaningful. The aim is enhanced final-image detail and clarity, with the HUD still present in the image, while the new NR-resolution options make the performance cost visible and adjustable instead of mysterious.

Multipass follows the same philosophy. Extra NR layers are there for anyone who wants to experiment with a more heavily enhanced image, but every layer needs to earn its cost. That is why the new stack gives you per-layer controls and shared tuning instead of asking you to accept one giant performance hit just to see what another pass can do.

This update took many hours of compiling, bug fixing, testing, and chasing down problems that were supposed to be much smaller than they turned out to be. NeuRotic is still something I work on in my spare time alongside a full-time job, so updates cannot always arrive as quickly as I would like. I would rather spend the time making an update genuinely worthwhile than push something out just to keep a schedule.

Thanks to everyone testing the odd corners, reporting what games do differently, sharing screenshots, and asking for controls that expose the real behavior. This release exists because that feedback keeps turning into ideas worth chasing. The rabbit hole continues.

This one is big.

0.9.4 was the last release, and a lot of the things people have been asking for since then finally came together here: more control over where Neural Rendering runs, a flexible Multipass stack, stronger per-pass tuning, safer installation, and a new final-image route to experiment with. I kept adding “just one more thing” from the request pile, and at some point this stopped being a small update.

This is probably the clearest example yet of community feedback directly shaping NeuRotic. The goal throughout has been to make the renderer more useful and easier to understand without turning performance controls into a pile of vague labels. Present Enhanced is the biggest of those rabbit holes, but it is only one part of the update.

## Present Enhanced: a new final-image route

This update adds three distinct NR routes:

- **Native Temporal** keeps the existing Quality and Performance behavior, including its current model-resolution controls.
- **Present Image-Only** processes the final presented image without using the game’s Native temporal guides.
- **Present Enhanced** also runs on the final presented image, but pairs it with captured Native depth, motion, jitter, reset, and render-area information.

The practical goal of Present Enhanced is to combine final-image placement—including the game HUD—with better temporal information from the Native rendering path. It is not simply Native Temporal moved later in the frame, and it does not promise that individual HUD pixels will remain untouched. The HUD stays in the image being processed instead of being discarded or replaced.

## NR resolution replaces Present workload

Both Present routes now have their own **NR resolution** setting:

- **Follow native render resolution** uses the game’s current render area. It follows fresh render-subrect metadata rather than guessing from a DLSS preset name, so it can respond to real resolution changes and dynamic resolution.
- **Always full output resolution** runs NR at the final output size.
- **Custom scale** reveals 100%, 77%, 67%, 58%, 50%, and 33% choices for anyone who wants to trade some NR workload for performance.

NeuRotic shows the actual NR dimensions alongside the output dimensions, so “67%” is no longer an abstract label. At 3840×2160 output, for example, the current aligned 67% workload is 2576×1448, while 50% is 1920×1080.

Present Image-Only and Present Enhanced remember their resolution policy and custom percentage independently. Switching between them should not force both routes into the same workload. Existing `PresentWorkload` settings also migrate forward: 100% becomes **Always full output resolution**, while lower values become the matching **Custom scale**.

Native Temporal has not been folded into this new system. Its existing Quality/Performance behavior and model-resolution controls remain intact.

## Performance consequences: control the pixels, control the cost

Present NR no longer asks you to guess what a workload label means. Full output resolution spends the most pixels; Follow native render resolution follows what the game is actually rendering; Custom scale lets you choose the trade-off directly. Exact performance gains depend on the game, output resolution, GPU, driver, scene, model, and the rest of your rendering stack, but the controls now expose the decision instead of hiding it.

Present Enhanced is built to favor a clean refusal over a bad temporal guess. During a resize, route change, reset, or missing-guide condition, NeuRotic may leave a frame unprocessed while it waits for a safe, compatible state. That is intentional. One ordinary original-image frame is a much better outcome than forcing the model through mismatched motion or depth data and hoping the artifacts are subtle.

## Multipass: more room to experiment

The Multipass stack has received a major expansion. NeuRotic now exposes up to ten NR passes, with independent per-pass state and controls rather than treating every added layer as a copy of the first one.

Each additional layer can be tuned for model resolution and strength, while shared Model Strength and Detail Strength controls make it possible to adjust a selected group of child passes together. Individual pass edits remain independent, so one broad adjustment does not erase a stack you have already dialed in. Slider previews also stay out of the configuration file until the setting is deliberately committed.

More passes are a toolbox, not a promise that “more” is always better. Start with two total passes, use three or four as advanced territory, and treat five through ten as experimental. Extra layers are not a free performance upgrade; check both the cost and the image in the game you are actually playing.

The Neural Rendering interface has been reorganized around that workflow, making the primary route, Multipass controls, and child-pass tuning easier to find. The shared controls apply from Pass 2 through the pass you select, while preserving Pass 1 and any unselected saved profiles.

## A safer, clearer installer

Setup now lets you choose from the nine proxy names NeuRotic actually supports, so you can select the name your game loads instead of relying on a one-size-fits-all guess. A free name installs immediately. If an ordinary selected name is already occupied, Setup offers to back it up and replace it, choose a different name, or cancel without writing anything.

`dxgi.dll` gets one additional, explicit option for ReShade users. If you know the existing file is ReShade, Setup can rename it to `ReShade64.dll` and enable the matching NeuRotic compatibility setting. NeuRotic does not try to identify someone else’s DLL automatically; the choice remains yours. An existing `ReShade64.dll` stops the setup before it changes anything.

Restore has been rebuilt around the same idea. It records the original files and configuration, restores them from the backup, and refuses to overwrite a later unrelated runtime change. Your private NVIDIA model remains yours: it is not bundled with NeuRotic and is left alone by setup and restore.

## Installation

1. Download **`NeuRotic-Alpha-0.9.5.zip`** from this release. GitHub's automatic source-code archives are not install packages.
2. Extract the entire NeuRotic folder somewhere outside the game directory and keep its `payload` and `support` folders together.
3. Fully close the game, then run **`NeuRotic-Setup.cmd`**.
4. Select the game's real executable and the proxy filename that game loads. DirectX 12 games commonly use `dxgi.dll`, but compatibility varies by title.
5. Supply your own licensed NVIDIA **`nvngx_dlssnr.dll`** model in the game folder. It is not included. The similarly named **`nvngx.dll_dlssnr.dll`** included with NeuRotic is the required forwarder, not the model.
6. Start the game, open the OptiScaler overlay, enable Neural Rendering, and confirm that the status and evaluation counters are actually advancing before judging the image.

Setup preserves an existing NeuRotic/OptiScaler configuration and private model during an ordinary update. Fresh installs begin with NR off, Present Enhanced and Follow native render resolution selected, Multipass off, and file logging off. The generated backup contains a standalone `Restore.cmd` for returning to the exact pre-install files.

## Vulkan and the wider stack

This build also carries the integrated Vulkan NR component and related UI/routing work alongside the DirectX 12 changes. Vulkan remains a separate, game-specific compatibility path, so please report results rather than assuming that a DirectX 12 success translates directly.

## Enhanced guides, with a deliberately strict safety rule

Present Enhanced uses captured Native depth and motion information only when NeuRotic can identify one fresh, unique, correctly matched guide pair for the presented frame.

If that match is missing, stale, ambiguous, from an unsupported format, or unsafe to use, NeuRotic does not guess. It leaves the original image in place and shows the reason for refusing the Enhanced pass. Route changes, resolution changes, resets, and output-size changes also invalidate history and rebuild through the existing resource-lifetime system.

That fail-closed behavior matters. A clean fallback is much better than feeding a temporal model the wrong motion or depth and hoping the artifacts are subtle.

## Experimental compatibility scope

Present Enhanced is no longer blanket-blocked when Frame Generation, Ray Reconstruction, or NR Multipass is active. Those combinations are exposed so people can experiment with them, individually or together, while the same fresh-guide, resource, queue, identity, subrect, and GPU-lifetime checks continue to decide whether a frame can actually be processed.

DirectX 11 can also attempt Present Enhanced through its existing shared D3D12 resource bridge. This is an experimental attempt path, not a compatibility guarantee. It still needs a compatible producer/Present device relationship, ordered queue behavior, supported resources, and a fresh matching guide pair.

The established D3D12 SDR configuration with Frame Generation, Ray Reconstruction, and Multipass off remains the named comparison control—not a required user setting. Changing any of those active feature flags invalidates Present history so a new combination starts from fresh temporal state.

Vulkan Present remains unavailable because this checkpoint does not add a Vulkan Present adapter. HDR Present Enhanced also remains unavailable because the current Present resource path still requires SDR output. Removing the blanket guards does not pretend those missing implementations exist.

## Known oddities and sensible cautions

NeuRotic is experimental rendering middleware. Results can vary with the game, GPU, driver, rendering API, DLSS files, output mode, and any other injector or overlay in the chain. A good result in one title is useful evidence, not a universal compatibility certificate.

- **“No fresh guide match” is not a crash.** It means Present Enhanced deliberately kept the original image because it could not prove that the captured Native data belonged to the current Present image.
- **More passes are not automatically more detail.** If the next pass costs more than it improves, use fewer passes. Two is the sensible place to start.
- **Give transitions a moment.** Route, resolution, reset, resize, and dynamic-resolution changes rebuild temporal state. Let the scene settle before judging the image or switching again.
- **Keep third-party tools reversible.** ReShade, HDR tools, frame-generation tools, overlays, and other injectors can each introduce their own compatibility issues. Make a backup, change one major variable at a time, and avoid overwriting an unrelated loader blindly.
- **The NVIDIA NR model is still proprietary.** NeuRotic does not include it. Do not confuse your `nvngx_dlssnr.dll` model with NeuRotic’s included `nvngx.dll_dlssnr.dll` forwarder; they are different files with unfortunately similar names.

## What has been validated so far

The current unlock checkpoint has passed its offline configuration, migration, save/load, guide-copy, matching, conversion, resource-lifetime, UI, localization, GPU-safety, Release/x64 build, incremental-build, packaging, and installer/restore checks. The captured-guide path has also been exercised on WARP and an RTX 4080 SUPER.

Those checks establish that the plumbing behaves as intended. They do not prove image quality, compatibility across games, or sustained in-game stability with the private NVIDIA model. Present Enhanced remains experimental until that evidence exists.

## Release identity

- Public release: **NeuRotic Alpha 0.9.5**
- Public branch and tag: `alpha-0.9.5`
- Implementation branch: `exp/present-enhanced-experimental-unlock`
- Implementation commit: `a57e445604e3aa42c32d92908074ef0b565f04b6`
- Status: public experimental Alpha; not a promotion of the frozen stable control
- Windows build: clean Release/x64, packaged as a matched OptiScaler/NR-forwarder pair

## The testing pass that matters next

The important comparison is **Present Image-Only → Present Enhanced → Present Image-Only** in the same scene, with the same model, tuning, and NR dimensions. That A/B/A pass needs to be repeated at:

- Always full output resolution / 100%
- Follow native render resolution
- Custom 67%
- Custom 50%

After each switch, give temporal history time to settle. A successful Enhanced result requires sustained capture, match, and evaluation counters together with active Present Enhanced placement and successful output. Falling back to the original image is safe, but it is not a successful Enhanced sample.

The most useful comparisons will include faces, cloth, highlights, camera pans, moving objects, disocclusions, and HUD behavior. I am also especially interested in transitions: menus, loading screens, camera cuts, NR toggles, route changes, dynamic-resolution changes, and output resizing.

There are enough moving parts here that compatibility reports will be unusually valuable. If something behaves strangely, the actual NR/output dimensions, the visible fallback reason, the route and resolution settings, and a matched screenshot or short video will help far more than “it looked broken.”

Special thanks to Tommy Creo for serving as a tester on the Vulkan issues and helping track down and fix a stubborn problem in that path.

If NeuRotic has improved a game for you and you ever feel like buying me a coffee while I keep tinkering with it, I would be very grateful. There is no expectation whatsoever—testing, feedback, screenshots, and bug reports already make a real difference.
