# Alpha 0.9.6 Advisor/input experiment

Parent/control: d3fcd123b88f37f2c12779816fc4324880bb3eed, with its exact packaged integration INI.
Branch: exp/0.9.6-advisor-safety-opticlip. This is an isolated extension explicitly authorized by the user, including source and default INI changes in the same experiment. Existing candidate/package, dirty persistent checkout and locked stable control remain preserved.

Hypothesis: single-route measurements with bounded readiness and verified native cadence avoid misleading Advisor recommendations; independently selectable gameplay devices and simpler NR sizing make menu behavior predictable. Embedded PNG provenance keeps new screenshot output image-only.

| Contribution | Integration and conflict disposition | Evidence gate |
|---|---|---|
| OptiClip dc14661c | Mechanically selected core/layout diff from authoritative UI 602d2eb4. Original common-menu hunk failed strict check at line 7125 and was not applied. Regenerated core patch applied strictly; common-menu integration authored against this parent. Fixed candidate input behavior excluded. | Controller tests, localized layout checks; real-game visual acceptance pending |
| Advisor | One route per click, temporary model suppression, matching fresh native observations, readiness/warmup/sample limits, stall refusal and restoration. No resolution sweep. | Sampling and config fixtures; exact-binary hitch/FG acceptance pending |
| Input | Shared policy under existing input mutex; independent controller flag, immediate cursor release, separate device release ownership, DirectInput capability classification. | Eight policies and hook review; hardware/game transport acceptance pending |
| Resolution | Existing persisted numeric route modes retained. UI/Advisor share StageUi policy; renderer retains PresentResolution sizing. New profiles follow game input; explicit legacy profiles preserved. Native After cannot match game input and Native Before cannot use final output without changing placement, so those choices are refused. | Load/save, route memory, sizing and Multipass suites |
| PNG | WIC pixels retained; versioned iTXt provenance added to staged PNGs; non-overwriting batch publication and owned-file cleanup. Existing screenshots and sidecars remain untouched. | GPU capture fixture, metadata and decoded-pixel verification |

No brightness manipulation, game installation, public replacement or stable promotion is included. Runtime classification remains Inconclusive. Decision: keep experimental. This record will be accompanied by final build/package manifests and test evidence; the initial stage is not a runtime acceptance claim.
