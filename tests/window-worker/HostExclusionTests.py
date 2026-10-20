"""Actual catalog: the host process must be refused without entering capture/NR."""
import ctypes, json, subprocess, sys
from pathlib import Path
root = Path(__file__).resolve().parents[2]
source = subprocess.Popen([str(root/'builds/hub-anything/SessionFixture.exe')], stdout=subprocess.PIPE, text=True, encoding='utf-8')
try:
    identity = json.loads(source.stdout.readline())
    exe = root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'
    def catalog(args):
        result = subprocess.run([str(exe), '--catalog', *args], capture_output=True, text=True, encoding='utf-8', timeout=10)
        assert result.returncode == 0, result.stderr
        return json.loads(result.stdout)['windows']
    baseline = catalog([])
    assert any(w['hwnd'] == identity['hwnd'] for w in baseline), 'Fixture must actually be eligible before exclusion'
    excluded = catalog(['--host-pid', str(identity['pid'])])
    assert not any(w['pid'] == identity['pid'] for w in excluded), 'FAIL host PID is still eligible for capture'
    print('PASS actual eligible host process excluded from worker catalog; no capture/provider entry')
finally:
    ctypes.windll.user32.PostMessageW(ctypes.c_void_p(identity['hwnd']) if 'identity' in locals() else None, 0x10, 0, 0)
    try: source.wait(timeout=3)
    except subprocess.TimeoutExpired: source.terminate(); source.wait(timeout=3)
