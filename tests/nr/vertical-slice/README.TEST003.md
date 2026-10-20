# TEST-003 evidence evaluator — partial W03 implementation

The same evaluator also reads `spectre-closure-v1-candidate` manifests against
the checked-in 58-case map and construction-scope policy. This separate
versioned scope preserves all 129 recovered original layer/axis cells and
reports missing results without changing the 39-case Native G2 reader below.
Its CLI never grants G2 or W04. Synthetic complete fixtures are accepted only
through the test-only function parameter; product evaluation refuses
`TEST_ONLY` as PASS evidence. See `docs/ai/live/SP08_SOURCE_BINDING.md`.

`gate_evaluator.py` validates a local, immutable evidence bundle. It does not run a model, qualify the Native adapter, or approve G2. Current production Native integration is blocked on the authentic early-Native-to-final-resource lineage publication; see the workspace continuation ledger. A successful synthetic evaluator test is protocol/tooling evidence only.

Run with the project Python runtime:

```powershell
& 'python' -B 'tests/nr/vertical-slice/gate_evaluator.py' '<absolute bundle manifest path>'
```

Exit 0 means `G2 EVIDENCE COMPLETE`; exit 1 means incomplete; exit 2 is invalid command usage. Human G2 review is always required. The evaluator never launches a referenced executable or loads a DLL. All referenced artifacts must be collected inside the bundle and match their recorded size/SHA256. JSON duplicates, wrong types, unknown schema versions, missing required results and unresolved artifacts fail structural validation before substantive evaluation.

The version 1 `NeuRotic.G2.NativeEvidence` envelope has `profile: D3D12-Native-SDR`, `artifacts`, `results`, `identities`, `comparison_ref`, and `coverage_ref`. An artifact has `id`, relative `path`, `sha256`, integer `bytes`, `role`, and `collection_state: COLLECTED`. Identities resolve source/build/configuration/environment/runtime/model/provider/host documents, each containing its kind, stable identity and exact hash. Private/missing identities cannot be invented to complete a bundle.

Each result uses `NeuRotic.C15.Test003Result` version 1. The TEST-owned JSON envelope extends C15 evidence packaging; it does not change canonical C++ contracts or enums. It contains exact fixture/profile, command argv/cwd/UTC times/numeric exit/raw output references, linked identities, expected and observed invariants, missing evidence, owner proof references and all four axes. Canonical execution states: NOT_RUN, ATTEMPTED, PARTIAL, COMPLETED, BLOCKED_BY_ENVIRONMENT. Canonical verdicts: PASS, FAIL, SKIP_UNSUPPORTED, INCONCLUSIVE, INFRA_ERROR. Each axis retains its own execution state, applicability, required status, reason and evidence. Unexecuted PASS is invalid.

The reviewed evaluator's `CASES` table pins 39 required cases from TEST-003 H–L/T/V/W. Input flags cannot waive them. CPU requires protocol; WARP requires protocol and resource/lifetime within that fixture; Native-Live requires all four. This is a conservative initial Native policy, not a claim that these cases have run. T30 and live comparisons additionally require references to C06/C07/C03/C13, capture, timing and owner-invariant artifacts. Hashes verify the bytes reviewed; they cannot authenticate an owner's assertion or independently prove image quality. Review the underlying typed owner records and captures.

T49 requires at least five independent immutable A/A runs and the frozen recommended three counterbalanced ABBA/BAAB/ABBA blocks for both same-candidate-binary OFF/ON and pinned-control/candidate comparisons. The tool deliberately supports only this ordering and the declared source/build/artifact/NativeProtocol difference set. A justified alternative needs an explicit reviewed policy extension; the input cannot relax it. Each arm remains immutable, the candidate is identical across both forms, and **pinned control and candidate hashes may differ**. Captures, timing, command and canonical alignment references are mandatory; frames are not independent repetitions. T60 gaps remain blockers.

Identity manifests use `NeuRotic.Test003.Identity` version 1 and pin actual `payload_ref`/`sha256`. Source payloads require exact HEAD, branch, working-state/index hashes, difference references and submodule inventory; build payloads require the same source identity, successful numeric exit, command/toolchain and output references. Configuration retains raw and effective references; environment identifies OS/adapter/driver/D3D12. Root control and candidate identities/artifacts are distinct. All T49 arm hashes and runtime/configuration links are checked against those identities.

Comparison evidence uses versioned `NeuRotic.Test003.ComparisonReport` (variance, pairing, alignment) and `NeuRotic.Test003.RunReceipt` reports. Exact run membership, identity references, raw captures/timing and successful command/report outcomes must agree. This initial schema requires separate run receipts/capture files/alignment reports; aggregate capture slicing is not supported. Required invariant IDs are the reviewed case IDs, not caller-replaceable assertion names.

Live C03/C06/C07/C13 roles resolve `NeuRotic.Test003.OwnerValidation` reports, not an arbitrary blob labeled with a contract name. Each pins a raw typed record hash, complete referenced-metadata closure, successful `NeuRotic.Test003.ValidatorReceipt`, exact source/build identity, run, evaluation and base-real-frame IDs. TEST reports are evidence envelopes, not substitute operational contracts. The evaluator does not implement a canonical C++ owner-record validator or model host: those remain prerequisite producers of real reports. It checks that their completed reports and all referenced bytes/identities agree, and cannot verify the honesty of fabricated evidence. No valid production reports exist for the current blocked Native path.

`test_gate_evaluator.py` constructs plainly synthetic documents to test these decisions, including a synthetic complete packet. Never use that fixture packet as campaign evidence. Run it directly with Python `-B`; all temporary fixtures are isolated and removed by the test runner. Retained command/test/mutation receipts belong in the workspace artifact run, not the source directory.
