# Alpha 0.9.6 release-seam review

Parent and runtime control: `bb308435f6f8c8c2d8a078789245e1d87bdada9b`.
Branch: `exp/0.9.6-release-seams`. The user reports BG3, DD2 and Cyberpunk
"all good and working" for the preceding feedback build. Duration, API, GPU,
driver and full transition/feature settings were not supplied; do not infer them.

Hypothesis: refusal-only Advisor analysis should preserve live settings and its
explanations, while FG override wording should agree with actual renderer policy.

Changes: run pure preflight before capturing/mutating temporary settings; seed
all-route context even when every route is skipped; share that capture with real
trials; remove "Probably don't need this" from the FG checkbox in all languages.
This does not enable experimental settings, alter any INI/default, change the
five-second initial feedback wait, or change renderer/FG/HDR/readiness behavior.

Verification: existing compiled Advisor and localization suites, extended
production wiring contract, strict source reconstruction, canonical ordinary and
repeat Release/x64 builds, matched package hashes and disposable installer checks.
Exact results belong in the external release review report after completion.

Result: source review found bounded UI/controller seams; new binary runtime is
Inconclusive until tested. Decision: keep experimental; no public/stable promotion
or game deployment. Preserve the accepted parent and all prior packages.
