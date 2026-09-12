# Wilds frame-association diagnostic

Independent diagnostic child of control `4f79b1973c0e488f92bbd1be993622584fdee68c`.
The 2.14.1 package trial failed the dropout gate and was restored. This experiment
keeps the original 2.7.32 runtime, live INI and NR model. It does not incorporate
the tag-compatibility experiment or change admission, startup gate or rendering.
The exact compiled/installed commit and hashes are in BUILD-MANIFEST.json.

## Test

Launch Wilds through the existing Steam entry; no launch-option changes are needed
after the matched deployment is installed. Wait for TRACE READY. Load with NR off.
Use Present Enhanced, FG 2x, RR off and NR Multipass off. Keep resolution, upscaling
and NR tuning unchanged. Enable NR and wait for TRACE VERIFIED.

Move until one dropout, or for 60 seconds if none appears. After a dropout keep
moving for five seconds to capture recovery. Turn FG off for ten seconds while NR
stays enabled, then turn NR off and exit normally. Wait for CAPTURE SAVED. Report
whether a dropout occurred and whether the FG-off portion looked stable.
Stop early on a severe fault. No additional mode cycling is needed.

Sessions are saved under `C:\OptiScaler-NR-Dev\logs\mhwilds-frame-association-sessions`.
The helper records profile identity and presence/missing coverage of API, ledger,
PCL and pre-FG events. Missing coverage or an exhausted budget is not success.

## Why this capture

The retained 2.14.1 run showed a missing-tag/constant refusal followed by a
duplicate-token refusal and startup requalification. The control ledger keeps the
latest constants and tags separately and consumes fresh tokens even when unmatched.
An offline overlapping-token case reproduces that behavior; it does not identify
the game frame the provider was actually presenting.

NVIDIA's bundled ProgrammingGuideDLSS_G section 8 requires PresentStart/PresentEnd
frame indices to match the common constants. The global resource-tagging guide
also permits multiple CPU frames in flight. This diagnostic records those marker
indices, provider API enter/return, and snapshots before/after each ledger mutation
under its existing lock. Raw API frame N corresponds to ledger key N+1; zero is
reserved for missing identity. Thread IDs, QPC and trace sequences support ordering.
Legacy tags have no explicit frame; their ledger assignment is recorded as such.

`NEUROTIC_FRAME_TRACE_PROFILE=frame-association` filters out unrelated command-list
traffic before consuming the same 65,536-event budget. It retains API/ledger/PCL,
pre-FG forwards, admission, fallback and NGX FG-input events. Unknown profiles
refuse capture. With no profile, existing full capture remains available.
The profile starts on NR enable and is one-shot. Coverage duration depends on rate;
do not infer events beyond trace-ended. Native-guide/resource details omitted by
this profile must be investigated separately if the ordering evidence requires it.

Observation can alter CPU timing, especially logging under the ledger lock. The
diagnostic adds no GPU work or resource retention and preserves the control's
matching/consumption rules. An opt-in PCL forwarding observer preserves the exact
arguments and returned result; existing quirk/provider hooks keep precedence.
No frame is selected from PCL by this build. A successful compile is not a fix.

## Acceptance and next step

Review one captured dropout with constants/tag publication, Present markers and
claim snapshots. Distinguish genuinely missing data, overlapping producer frames,
late legacy assignment and incorrect claim selection before choosing a correction.
Preserve resource lifetime, exactly-once output, generation checks and refusal of
ambiguous/stale frames. Startup-latch changes remain separate.

Use `Start-MHWilds-Trace.ps1 -CheckOnly` with Windows PowerShell 5.1 for read-only
installation verification. The existing Steam wrapper will forward to this file
after installation. BUILD-MANIFEST.json records the canonical previous pair backup;
STEAM-ENTRY-RECEIPT.json records the old launcher backup. Restoration keeps the live
INI/model and original NVIDIA runtime. Source/worktrees and failed package history
are retained. Runtime result Inconclusive; keep experimental, no promotion.
