# Supplemental CPU source controls

`tools/nr/lab/SourceCaseControls.cpp` is a standalone Windows CPU diagnostic.
It calls the current shipped helper headers directly and emits one JSON result:

```text
SourceCaseControls.exe --case NLAB-SIG-005 --output <new-json-file>
SourceCaseControls.exe --case NLAB-MUL-001 --output <new-json-file>
SourceCaseControls.exe --case NLAB-API-004 --output <new-json-file>
```

The output parent must already exist. Existing result and synthetic PNG paths
are refused. Unknown cases do not publish a result. Exit 0 means all local
checks passed; exit 1 means a check failed; argument/output conflicts return 2;
infrastructure failures return 3. The result includes `case_id`, `success`,
`checks`, `observations`, `covered_scope`, and `unproved`. Every result has
`scope=CPU_SOURCE_ONLY`, `catalog_coverage=PARTIAL`, and either
`case_status=PASS_SCOPED` or `case_status=FAIL`.

## Exact scope

| Case | Local checks | Actual source | Unproved catalog requirements |
|---|---:|---|---|
| NLAB-SIG-005 | 14 | NativeTemporalInputs Fits/GuideRect; ResourceView structural mip/layer checks; WritePng padded-row conversion with independent decoded RGB oracle | Full S08 adapter binding, acquisition provenance, GPU subresource sampling |
| NLAB-MUL-001 | 21 | BasicMultipass Load/Normalize/Count/Strength/Derive on owned value fields | Renderer-private RequestedPassCount, recipe publication, actual model calls or delivery |
| NLAB-API-004 | 12 | PresentCompatibility Admit with explicit D3D11/D3D12 positive and refusal controls | Vulkan extension/hardware/provider preflight, Linux/Proton qualification, device discovery |

The projection input uses production `CustomOptional` value types. It does not
construct a production Config singleton, lifecycle issuer or renderer. The
ResourceView checks are structural only; empty test evidence is never presented
as owner qualification. WIC encodes and decodes a tiny synthetic PNG entirely on
the CPU. No D3D device, WARP, provider, model or game is invoked.

These supplemental results must not replace full NLAB catalog status. In
particular, passing Windows D3D capability controls cannot qualify Vulkan or
Linux/Proton.

## Build and verification

Run `source_case_controls_build.py --out <fresh-directory>` using Python 3.12.
The builder snapshots the local include closure, builds with MSVC 14.44.35207
using `/MT /O2 /std:c++20 /W4 /WX`, runs all three cases and four process tests,
and builds six deliberately changed source snapshots. The WIC links are
`windowscodecs.lib` and `ole32.lib`.

The six mutations test acceptance of an empty region, unsigned subrect wrap,
the mip-count boundary, fractional pass rounding, advanced-setting overwrite,
and missing D3D11 shared-resource refusal. Each must exit with an executed
failed check at its specific expected oracle. Compile failures, crashes and
missing results do not count as killed mutations.

The sealed package contains the executable, source snapshot, logs, baseline
observations and mutation-result summary. `SOURCE_BUILD_MANIFEST.json` binds
the source and executable hashes. `SOURCE_CONTROLS_MANIFEST.json` pins every
package file except itself. Mutant executables and build intermediates are
excluded from the package.

To run the process tests separately, set `NR_SOURCE_CASE_EXE` to the fresh
executable and run unittest discovery with `-s tests/nr/lab -p
source_case_controls_test.py`. The full build does this automatically.

Verified on 2026-09-26: 47 local checks, 4 process tests, 6/6 mutants; package
`C:/NeuRotic/builds/source-case-controls-20260926-final/package`. All 23 package
file hashes and all 13 source snapshot hashes were checked against the final
files. The catalog coverage remains PARTIAL for every case.
