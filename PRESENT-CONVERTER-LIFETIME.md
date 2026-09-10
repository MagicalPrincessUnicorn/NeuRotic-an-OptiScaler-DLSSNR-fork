# Present converter lifetime experiment

Parent/control: d079ccd72003470662270af086de73ea442da45d.
Branch: exp/neurotic-present-converter-lifetime.

Hypothesis: immutable R10/RGBA8 conversion bindings remove a CPU/GPU descriptor
mutation hazard that may contribute to Present flicker. Visual causality is unproven.
Adapted only the converter ownership portion of sibling 038957ed38ecdcd3625db829d9c346f9ca31ff4e.

Each Present resource generation binds its two fixed texture pairs once. Dispatch
reuses those descriptors without writing them and rejects a changed pair. Existing
generation completion checks gate recreation. Other converter callers retain
their existing dynamic path. No new history, model, shader, INI or UI policy.
Continuous history from the accepted control is retained; aa594656 is not a parent.

Verification: production converter/shader WARP test with eight queued submissions
held behind a GPU fence, known R10 pixels checked after an RGBA8 round trip,
rebind rejection and fresh 64/80-pixel resource generations after drain. Debug
layer is required. Run tests/Run-NrPresentConversion.cmd and the existing
tests/Run-NrRobustness.cmd, then the canonical Release/x64 immutable builder.
The fixture substitutes application configuration/logging/timing dependencies;
it does not execute the proprietary NR model or reproduce visible game flicker.

Runtime gate: same scene, workload, tuning and INI; Apply Model on. Begin with one
NR layer and FG off, check stillness and camera movement in bright/dark areas,
then the second NR layer and route/workload transitions. Record Better/Worse/
Equivalent/Inconclusive. No claim of a flicker fix until runtime evidence.

Test bundle preserves live INI and model, backs up the replaced DLL pair, and
verifies installed hashes. This replaces the per-frame-reset bundle as the next
test handoff only, not a baseline/candidate promotion. Retain both experiments.
