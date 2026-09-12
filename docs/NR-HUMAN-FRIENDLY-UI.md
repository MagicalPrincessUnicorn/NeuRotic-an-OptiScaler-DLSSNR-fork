# English NR layout and Basic Multipass experiment

Parent/control: `3a6f34d32d4d0728560014b65d42bd9bfc7a5c6e`.
Branch: `exp/0.9.5-nr-human-friendly-ui`.
The user explicitly approved extending this independently built, runtime-unaccepted UI parent.
No newer FG/RR experiments are inherited. The frozen control and earlier experiment remain intact.

Hypothesis: sentence-style controls, ordered tuning/readouts and cumulative Basic Multipass
make NR easier to configure while preserving stored Main and Advanced profiles.
This is the explicitly approved integration of layout, Present resolution and Multipass behavior.

The NR page uses a scoped English presentation override; saved language preferences and existing
translation catalogs remain untouched. Enable lives in the section heading. The main pass-count
duplicate is removed. Basic notes and Apply precede the final collapsed advanced area.

Automatic means the selected processing stage at 100%. Present Manual accepts 25–200%, with
independent memory per method. Existing fixed modes display equivalent Manual values without
rewriting their old keys merely by opening the menu; an explicit resolution edit adopts the continuous
encoding. Legacy Follow Native retains its original resolution and fresh-guide requirements until
Automatic or Manual is selected. Existing mode values 0–2 remain compatible; new values 3 and 4
store Manual and Automatic respectively, with the remembered percentage in CustomScale.
An existing NR profile with implicit Enhanced Follow Native remains Legacy; an absent NR profile
uses output resolution. Unsupported model dimensions above 8192 pixels are refused, retaining
the original image. Existing resampling, history, guide matching and queue guards remain in use.

Basic profile keys live in `[DlssNrBasic]`: Advanced (0/1), MaximumPasses, Resolution,
Downscaler, ModelStrength and DetailStrength. Strengths are ratios: 2.3 means 230%.
Fresh Basic defaults are disabled Multipass, maximum 1, resolution 1, both totals 1.
Existing explicit Multipass/second-layer/pass settings select Advanced on first preview load.
Main and Advanced saved fields are never overwritten by Basic. A render snapshot owns the
derived pass settings. Native Vulkan retains its single-pass profile and placement restriction.

Each total distributes as clamp(total - passIndex, 0, 1); the larger total determines actual
pass count. Zero totals bypass evaluation without releasing loaded resources and invalidate
history for resumption. Ordinary unused trailing passes follow existing safe retirement rules.
Apply hides every pass's effect but keeps processing and resource tracking. Present skips final
conversion/copyback when hidden, preserving the exact game image; Pre-SR skips the final handoff.
Advanced per-pass switches remain saved and subordinate to the global Apply control.

Verification combines CPU profile/snapshot and INI tests, actual ImGui sentence controls and
navigation at 0.5–2.0 scale in both themes, source integration checks, and existing GPU/resource,
readiness and Present regressions. Full in-game layout, vendor model supersampling, performance,
image quality and temporal transitions remain runtime acceptance gates.

Handoff: canonical Release/x64 build only, followed by incremental verification and recorded
source identity, DLL hashes and logs. No game deployment, INI installation, release, candidate,
baseline or stable promotion. Result remains Inconclusive; keep experimental and pause after build.
