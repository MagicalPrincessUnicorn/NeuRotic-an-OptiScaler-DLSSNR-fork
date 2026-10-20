# API adapter verification — 2026-10-05

These checks ran in the approved `universal-connections-experimental` checkout using VS 18 Community / MSVC 14.44 and the checkout's current FidelityFX flow library. The adapter fixture used Microsoft Basic Render Driver, adapter LUID `00000000:00005104`.

Reproduce from the checkout root:

```bat
tests\universal_connections\run_adapter_tests.cmd
addons\prepared-guides\build.cmd x64
addons\prepared-guides\build.cmd x86
```

All three commands completed with exit code 0 after the final source changes. Final logs contain no compiler warnings or errors:

- `builds/universal-guides/adapter-tests/adapter-run.log`
- `builds/universal-guides/adapter-tests/addon-x64.log`
- `builds/universal-guides/adapter-tests/addon-x86.log`

The GPU fixture exercises production adapter Capture, `Processor::ProcessTextures`, `CpuMainlineClient::ProcessResident`, an explicit shader recorder that consumes all four guide textures, and adapter Finish with byte-checked game texture copyback. It does not load the private NR model.

Verified paths:

- D3D12 RGBA8 with R32 depth and typed D24 depth without an SRV binding. RG32 motion and R32 mask convert to canonical RG16F/R8. Motion Y inversion is checked.
- D3D11 RGBA8/BGRA8 with R32, D16, D24 and D32 depth. Full context state preservation includes a viewport check. Supplied nonzero RG32 vectors `(2,-3)` survive private RG16 conversion and transport with both signs preserved. Full tuple processing and exact output copyback pass.
- D3D10.0 and D3D10.1 legacy/event relay, including typed D24 and full tuple processing. Nonzero RG32 vectors pass through the D3D11 relay conversion. Exact source output pixels pass.
- Negative checks include unknown D3D12 states, pending producer/output completion, deferred D3D11 contexts, replayed tickets, retained canonical history occupying both slots, oversized rasters, unsupported motion units, aggregate allocation exhaustion, and allocation-token retirement after owners release.
- Actual external-feeder export tests run against the addon object on x64 and x86: malformed headers, unsupported producer versions, invalid/replayed capture pairs, missing runtime registration without pointer dereference, and GL physical-origin admission to the runtime gate.
- Production GL ownership guard sequences cover Finish, Cancel and clean refusal returning the complete import set, repeated release/reacquire rejection, and unknown completion forbidding reuse. These are protocol-state checks; they do not substitute for the skipped GPU interop run.
- Existing addon session-policy, capture-codec, depth-binding and capture-input tests pass on both architectures.

Explicit qualification limits:

- OpenGL positive interop is skipped because this machine does not expose the required memory/semaphore functions. Clean capability refusal and unchanged source pixels are verified. Production GL code contains exact-format private GL→D3D12→GL pixel and semaphore proof, including the absent-LUID-query candidate path; that path still requires a capable driver run.
- A second D3D12CreateDevice call returned the same COM identity, so the distinct-logical-device negative is explicitly skipped.
- The live external-export positive path is integrated but has not been exercised in a ReShade game. GPU fixtures directly exercise the production adapters/processor and use an explicit recorder; they are not private-model or game qualification.

Ownership and bounds:

- GL transfers all 3/5 imported images to D3D12 at Capture, including the future output target. Finish, Cancel and clean inner Capture refusal each prove the host fence on the CPU, wait the complete set back into GL, and prove local completion before reuse. Thus the next Capture never writes an input still owned by D3D12, and the next Finish never writes an output still owned by GL.
- The addon claims the selected source/transport before adapter Capture. A new cleanly refused claim releases private adapter pools before relinquishing the core owner; uncertainty retains the claim and resources.
- One transport ticket and two independently leased immutable canonical slots per connection. GPU output leases retire only after exact source copyback completes. Uncertain work retains the owner and allocation reservations and requires restart.
- Raster admission precedes owned texture allocation: maximum 4096×2160, maximum 8,388,608 pixels; derived flow requires at least 256×64.
- `GpuBudget.h` tracks owned allocations with shared RAII reservations, default 1 GiB per module; remote producer/host clamp to 512 MiB each. D3D12/GL sizes use actual allocation queries. D3D11/D3D10 allocations use equivalent D3D12 descriptors; the ledger covers known owned resources, not driver-internal allocation overhead.
- `shared_format_probe.cpp` records the observed driver distinction: D3D11-originated NT sharing rejects RG32F, while RG16F works. D3D11 therefore performs a real private shader conversion before sharing; D3D10 legacy RG32F remains supported on this device.

Completed compiler intermediates were removed from the three owned output directories. Storage fell from 14,385,653 to 3,924,508 bytes (10,461,145 bytes removed). Binaries, libraries, logs and `storage-receipt.json` remain. No commit, merge, push, game install or new worktree was performed.
