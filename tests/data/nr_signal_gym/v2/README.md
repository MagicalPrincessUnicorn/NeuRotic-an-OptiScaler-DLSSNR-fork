# Specification fixtures — not live-game captures

`00_signal_gym.schema.json` is the single canonical v2 schema. Every other JSON document in this directory is a schema-checked example. `corpus_manifest.json` selects the 16 independently authored seed cases. `environment_cpu.json` requests only L0/L1. The source adapter descriptor and NOT_READY replay-result example illustrate wire formats; neither claims an implemented or executed adapter.

The requested examples are `frames/minimal-frame.json`, `frames/stale-signal-frame.json`, and `frames/contradictory-generation-frame.json`. Each has an independent expected sidecar. The latter two preserve wrong-frame/generation facts while a simple geometry helper still returns Fits=true; that is not freshness, generation, or admission approval.

The remaining seeds cover zero/overflow/padded rectangles, known-zero/absent/unknown flags, source Present policy success/refusal, stale readiness, a history transition sequence, explicit absence, conflicting provenance, and explicit NaN corruption. See `../12_SUPPORTING_EVIDENCE/seed_catalog.json` for titles and prior-fixture links. These links preserve prior research identity; they are not full migrated equivalents.

`mutation_overlay.json` is a GYM-MUT-016 wrong-frame edit applied to the first seed. Its pointers address the case input tree `/frames/0/...`. It changes producer frame 100 to 101 while preserving the original claim frame 100, intentionally introducing disagreement. Its explicit expected sidecar is `expected/GYM-OVERLAY-WRONG-FRAME-001.json`. The derived ID and input hash are computed under 10, not guessed from a future run. It asserts transport preservation and source geometry only.

No game texture, raw private log, model binary, external download, or secret is included. Payload states are metadata-only in the selected seeds. Procedural/raw payload contracts are defined in 02 and schema; future GPU cases must supply the required bytes rather than silently allocate zeros.

All examples retain EspiOwnage attribution. Contract IDs prefixed source-* describe inspected baseline helper scopes; unbound-chat-d explicitly does not claim to be an accepted SEM contract. Actual owner/provider evidence remains separate.
