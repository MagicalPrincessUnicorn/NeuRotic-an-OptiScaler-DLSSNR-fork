# Generation-scoped Pre-FG readiness experiment

Parent and named control: `e6a4d28ba9f76877b53f54660de00ea2fcdb50ca`, the user-selected
Alpha 0.9.6 release candidate. Branch: `exp/0.9.6-prefg-readiness-certificate`.
The later installer-only parent-branch changes are not inherited.

Hypothesis: a completed private model probe plus an exact native FG wait acknowledgement
can replace eight consecutive successful frames while retaining fail-closed GPU ownership.
The changed variable is Present Image-Only/Enhanced admission with native Streamline FG.
INI, model, native temporal math, UI and provider options are unchanged.

The per-swapchain state is AwaitingFrame -> ProbeSubmitted -> AwaitingProof -> Ready.
A single-use Probe packet owns its fence and native-hook wait status. It carries the
same exact backbuffer, token, sequence, reservation and provider/native-instance checks
as output. The probe restores the input resource state and exits before changing FG
tags, recording a screenshot or copying to the visible backbuffer. It does not complete
output history or increment completed-output counters. Private model submission telemetry
still counts the actual work.

Readiness requires the probe's successful original Present, healthy device, completed
non-sentinel fence, successful native wait binding, and observation of the actual queue
Wait. A later coherent frame must still match swapchain/device/queue, provider and native
FG identity, exact effective NR configuration, resume/resource/model generation, output
and work dimensions, format, sample count/quality, route and color space. A model rebuild
inside evaluation converts that work into a new probe before any copyback.

One missing/duplicate frame token refuses only that frame. Older completed packets are
retired before forwarding an unchanged fallback; unfinished GPU dependencies are retained.
Provider/instance changes, model/submission/handoff failures, failed Present, device loss,
resize, incompatible configuration and explicit invalidation revoke permission. No frame
count or timeout grants readiness. Native wait cancellation before submission also revokes
permission. The certificate requires actual queue-wait observation, which is stronger than
binding alone and may add latency if a provider delays submission.

Deterministic coverage lives in `nr_pre_fg.cpp` and `nr_gpu_safety.cpp`: 10,000 stalled
polls; separate Present, fence, bound/applied proof; each identity field; the retained
523-attempt DD2 metadata sequence; exact/duplicate claims; actual WARP queue waits;
cancellation; failure-epoch propagation; completed packet retirement; and device loss.
The DD2 replay supplies synthetic completion evidence and is not a runtime timing result.

Run from this worktree:

```powershell
.\tests\Run-NrPreFg.cmd
.\tests\Run-NrGpuSafety.cmd
.\tests\Run-NrRobustness.cmd
```

Runtime acceptance remains **Inconclusive**; decision **keep experimental**. Expected
two-to-three-frame recovery is a hypothesis, not a measured result. Before any candidate
selection, test the exact built pair with unchanged matched INI/model in DD2 dialogue,
shop, Inventory, menus and FG Off/On (both Present routes); Cyberpunk RR/FG transitions;
and MHWilds N/N+1 association. Include low/high FPS, NR/route changes, focus, fullscreen,
resize and long sessions. Reconcile token/sequence/probe/wait/output trace events and
actual generated frames, compare image quality, and require healthy Presents/devices.

This experiment is build-only. No game deployment, candidate replacement, baseline
promotion, release tag or public package is selected by implementing it.
