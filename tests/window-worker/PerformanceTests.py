"""Production child-process diagnostic contract; no private model or game launch."""
import json, subprocess, sys, tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
exe = root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'
commands = [dict(command='set-comparison', revision=1, split=.42),
            dict(command='stop', revision=2), dict(command='quit')]
try:
    with tempfile.TemporaryDirectory(prefix='nr-perf-') as folder:
        trace = Path(folder)/'trace.jsonl'
        run = subprocess.run([str(exe), '--control', '--trace', str(trace)],
            input='\n'.join(map(json.dumps, commands))+'\n', capture_output=True,
            text=True, encoding='utf-8', timeout=20)
        assert run.returncode == 0, run.stderr
        events = [json.loads(line) for line in run.stdout.splitlines()]
        initial = events[0]
        assert initial.get('capabilities', {}).get('performanceTelemetry'), 'performance telemetry capability missing'
        assert initial['performance']['lastFrame'] is None, 'no invented frame before capture'
        assert initial['performance']['sourceMediaFps'] is None
        assert initial['performance']['displayedFps'] is None
        disabled=subprocess.run([str(exe),'--control','--no-gpu-timing'],input='{"command":"quit"}\n',capture_output=True,text=True,timeout=20)
        assert json.loads(disabled.stdout.splitlines()[0])['performance']['gpuTimingEnabled'] is False
        comparison = next(e for e in events if e['event']=='comparison')
        assert comparison['performance']['comparison']['revision'] == 1
        assert comparison['performance']['comparison']['publicationsAtApply'] == 0
        stopped = next(e for e in events if e['event']=='stopped')
        assert stopped['performance']['stopTrigger'] == 'command:stop'
        assert stopped['performance']['comparison'] == comparison['performance']['comparison']
        assert trace.exists(), 'opt-in metadata trace not created'
        records = [json.loads(line) for line in trace.read_text().splitlines()]
        assert any(r.get('record_type')=='control' and r.get('command')=='set-comparison' for r in records)
        assert not any(r.get('record_type')=='frame_result' for r in records)
        # CREATE_NEW: diagnostics must never replace an existing file.
        original = trace.read_bytes()
        again = subprocess.run([str(exe), '--control', '--trace', str(trace)],
            input='{"command":"quit"}\n',capture_output=True,text=True,timeout=20)
        assert again.returncode == 0 and trace.read_bytes() == original
        assert json.loads(again.stdout.splitlines()[0])['performance']['trace']['error'] != 0
    print('PASS performance protocol: unknowns, comparison/stop lineage, trace, no-clobber')
except Exception as error:
    print('FAIL performance:', error)
    sys.exit(1)
