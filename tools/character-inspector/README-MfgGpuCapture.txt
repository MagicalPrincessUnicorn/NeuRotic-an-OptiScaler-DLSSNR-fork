NeuRotic - Crimson Desert NR + 6x GPU capture

Keep the installed 11dad02e MFG real-frame build. This is a diagnostic launcher, not an installer or a rendering fix.

1. Extract ALL files to a local folder. Start with NR OFF in your saved settings.
2. Fully exit Crimson Desert and Steam (Steam > Exit). The helper will not stop them for you.
3. Run Start-MfgGpuCapture.cmd and select the installed CrimsonDesert.exe in bin64.
4. Leave this console open. It starts the game through its installed Steam entry.
5. Check the console for GPU TRACE READY before enabling NR. Keep the same MFG and Inspector settings, then test NR and the resolution-mode changes you want to check.
6. After the crash, or when your test is finished, close the game. The console produces a NeuRotic-GPU-....zip beside this tool. Send that ZIP back.
7. Fully exit Steam after the capture. Steam was launched with temporary tracing variables; exiting it removes them from later game launches.

If readiness is not verified within two minutes, close the game and send the archive without reproducing. The saved log might be disabled or redirected. Missing/partial evidence is inconclusive, not a successful capture. If startup fails, send the console error and any archive produced.

The tool preserves installed files and saved settings. It writes diagnostic files beside itself. It exports only its session record, lifecycle/DRED logs, the default OptiScaler.log, the NR admission record, and before/after copies of OptiScaler.ini. It does not export DLLs, private models or memory dumps, change the registry, change persistent environment settings, or upload anything. Inspect the archive before sharing if desired.

DRED asks Windows to record GPU breadcrumbs and page-fault information before device creation. This can change timing/performance. A report may localize the failure but cannot by itself prove the faulty component. Only the current session/PID is accepted for readiness. Earlier devices outside the hooks remain unqualified. No automatic retry loop is performed.
